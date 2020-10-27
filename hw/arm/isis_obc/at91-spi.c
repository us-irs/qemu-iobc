/*
 * AT91 Serial Peripheral Interface.
 *
 * See at91-spi.h for details.
 *
 * Copyright (c) 2019-2020 KSat e.V. Stuttgart
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or, at your
 * option, any later version. See the COPYING file in the top-level directory.
 */

// Overview of TODOs:
// - Slave mode (only master mode is implemented).
// - Transmission delays (transmissions are currently sent instantaneous, size
//   does not have an impact on the time it takes to send them).
// - Chip-selects are implemented on a per-transfer basis, NPCS lines are not
//   directly simulated. This includes LASTXFER having no effect.

#include "at91-spi.h"
#include "exec/address-spaces.h"
#include "sysemu/cpus.h"
#include "qapi/error.h"
#include "qemu/bswap.h"
#include "qemu/error-report.h"
#include "qemu/log.h"
#include "hw/irq.h"
#include "hw/qdev-properties.h"


#define IOX_CAT_DATA            0x01
#define IOX_CAT_FAULT           0x02

#define IOX_CID_DATA_IN         0x01
#define IOX_CID_DATA_OUT        0x02

#define IOX_CID_FAULT_MODF      0x01
#define IOX_CID_FAULT_OVRES     0x02


#define SPI_CR          0x00
#define SPI_MR          0x04
#define SPI_RDR         0x08
#define SPI_TDR         0x0C
#define SPI_SR          0x10
#define SPI_IER         0x14
#define SPI_IDR         0x18
#define SPI_IMR         0x1C
#define SPI_CSR0        0x30
#define SPI_CSR1        0x34
#define SPI_CSR2        0x38
#define SPI_CSR3        0x3C

#define CR_SPIEN        BIT(0)
#define CR_SPIDIS       BIT(1)
#define CR_SWRST        BIT(7)
#define CR_LASTXFER     BIT(24)

#define MR_MSTR         BIT(0)
#define MR_PS           BIT(1)
#define MR_PCSDEC       BIT(2)
#define MR_MODFDIS      BIT(4)
#define MR_LLB          BIT(7)
#define MR_PCS(s)       (((s)->reg_mr >> 16) & 0x0F)
#define MR_DLYBCS(s)    (((s)->reg_mr >> 24) & 0xFF)

#define SR_RDRF         BIT(0)
#define SR_TDRE         BIT(1)
#define SR_MODF         BIT(2)
#define SR_OVRES        BIT(3)
#define SR_ENDRX        BIT(4)
#define SR_ENDTX        BIT(5)
#define SR_RXBUFF       BIT(6)
#define SR_TXBUFE       BIT(7)
#define SR_NSSR         BIT(8)
#define SR_TXEMPTY      BIT(9)
#define SR_SPIENS       BIT(16)

#define SR_IRQ_MASK     0x3FF


// SPEC:
// The end of transfer is indicated by the TXEMPTY flag in the SPI_SR. If a
// transfer delay (DLYBCT) is greater than 0 for the last transfer, TXEMPTY is
// set after the completion of said delay. The master clock (MCK) can be
// switched off at this time.
//
// The transfer of received data from the Shift Register in SPI_RDR is
// indicated by the RDRF bit (Receive Data Register Full) in the Status
// Register (SPI_SR). When the received data is read, the RDRF bit is cleared.
//
// If the SPI_RDR (Receive Data Register) has not been read before new data is
// received, the Overrun Error bit (OVRES) in SPI_SR is set. As long as this
// flag is set, data is loaded in SPI_RDR. The user has to read the status
// register to clear the OVRES bit.


static void update_irq(SpiState *s)
{
    qemu_set_irq(s->irq, !!(s->reg_sr & s->reg_imr & SR_IRQ_MASK));
}

void at91_spi_set_master_clock(SpiState *s, unsigned mclk)
{
    s->mclk = mclk;
}


inline static uint8_t pcs_to_nr_nopcsdec(uint8_t pcs)
{
    // SPEC: When operating without decoding, the SPI makes sure that in any
    // case only one chip select line is activated, i.e. driven low at a time.
    // If two bits are defined low in a PCS field, only the lowest numbered
    // chip select is driven low.

    if ((pcs & 0b0001) == 0b0000)
        return 0;
    if ((pcs & 0b0011) == 0b0001)
        return 4;
    if ((pcs & 0b0111) == 0b0011)
        return 8;
    if ((pcs & 0b1111) == 0b0111)
        return 12;

    error_report("at91.spi: invalid PCS value 0x%x", pcs);
    abort();
}

inline static uint8_t pcs_to_nr(SpiState *s, uint8_t pcs)
{
    if (!(s->reg_mr & MR_MSTR))
        return 0x0F;

    if (s->reg_mr & MR_PCSDEC)
        return pcs;

    return pcs_to_nr_nopcsdec(pcs);
}

inline static uint8_t pcnr_to_cs(SpiState *s, uint8_t pcnr)
{
    if (!(s->reg_mr & MR_MSTR))
        return 0x00;

    if (s->reg_mr & MR_PCSDEC)
        return pcnr;

    return ~(pcnr + 1);
}

inline static uint8_t num_transmit_bits(SpiState *s, uint8_t pcnr)
{
    uint8_t bits = ((s->reg_csr[pcnr/4] >> 4) & 0x0F) + 8;

    if (bits > 16) {
        error_report("at91.spi: cannot transmit %d bit units", bits);
        abort();
    }

    return bits;
}

inline static uint32_t to_xfer_unit(uint8_t pcnr, uint8_t bits, uint16_t data)
{
    return ((uint32_t)pcnr) << 24 | ((uint32_t)bits - 8) << 16 | data;
}


inline static void xfer_master_wait_receive_finish(SpiState *s);

inline static void xfer_master_wait_receive_start_dma(SpiState *s, uint32_t n)
{
    s->wait_rcv.n = n;
    s->wait_rcv.ty = AT91_SPI_WAIT_RCV_DMA;

    // pause execution until data has been recieved to avoid timeouts       // TODO: remove?
    pause_all_vcpus();

    // if no server set up or it doesn't have a client, we already prepared rcvbuf
    if (!s->server || !s->server->client)
        xfer_master_wait_receive_finish(s);
}

inline static void xfer_master_wait_receive_start_tdr(SpiState *s)
{
    s->wait_rcv.n = 1;
    s->wait_rcv.ty = AT91_SPI_WAIT_RCV_TDR;

    // pause execution until data has been recieved to avoid timeouts       // TODO: remove?
    pause_all_vcpus();

    // if no server set up or it doesn't have a client, we already prepared rcvbuf
    if (!s->server || !s->server->client)
        xfer_master_wait_receive_finish(s);
}


static uint32_t xfer_master_unit_to_tdr(SpiState *s, uint32_t unit)
{
    uint8_t pcnr = (unit >> 24) & 0x0F;
    if (pcnr >= 16) {
        error_report("at91.spi: received invalid chip-select number: %d", pcnr);
        abort();
    }

    uint8_t bits = num_transmit_bits(s, pcnr);
    uint8_t bits_unit = ((unit >> 16) & 0xFF) + 8;

    if (bits != bits_unit) {
        error_report("at91.spi: received invalid number of bits: got %d, expected %d", bits_unit, bits);
        abort();
    }

    uint16_t data = unit & ((1 << bits) - 1);
    return pcnr_to_cs(s, pcnr) << 16 | data;
}

static uint32_t xfer_master_copy_to_rpr(SpiState *s, uint8_t *buf, uint32_t num_units, uint8_t unit_size)
{
    MemTxAttrs attrs = MEMTXATTRS_UNSPECIFIED;
    uint32_t n = num_units * unit_size;

    if (n > s->pdc.reg_rcr)
        n = s->pdc.reg_rcr;

    if (n == 0)
        return 0;

    // check for full units
    if (s->pdc.reg_rcr - (n / unit_size) * unit_size > 0) {
        error_report("at91.spi: invalid DMA buffer length %d", s->pdc.reg_rcr);
        abort();
    }

    MemTxResult result = address_space_rw(&address_space_memory, s->pdc.reg_rpr, attrs, buf, n, true);
    if (result) {
        error_report("at91.usart: failed to write memory: %d", result);
        abort();
    }

    s->pdc.reg_rpr += n;
    s->pdc.reg_rcr -= n;

    return n;
}

static void xfer_master_copy_to_dma(SpiState *s, uint8_t *buf, uint32_t num_units, uint8_t unit_size)
{
    uint32_t n = 0;

    n += xfer_master_copy_to_rpr(s, buf, num_units, unit_size);
    if (s->pdc.reg_rcr == 0)
        s->reg_sr |= SR_ENDRX;

    if (s->pdc.reg_rcr == 0 && s->pdc.reg_rncr != 0) {
        s->pdc.reg_rpr = s->pdc.reg_rnpr;
        s->pdc.reg_rnpr = 0;

        s->pdc.reg_rcr = s->pdc.reg_rncr;
        s->pdc.reg_rncr = 0;

        n += xfer_master_copy_to_rpr(s, buf + n, num_units, unit_size);
    }

    if (s->pdc.reg_rcr == 0)
        s->reg_sr |= SR_RXBUFF;

    if (n < num_units * unit_size)
        s->reg_sr |= SR_OVRES;
}

static void xfer_master_read_to_dma_varps(SpiState *s)
{
    uint32_t *buf = g_new0(uint32_t, s->wait_rcv.n);

    for (int i = 0; i < s->wait_rcv.n; i++) {
        uint32_t unit = ((uint32_t *)s->rcvbuf.buffer)[i];
        uint32_t tdr = xfer_master_unit_to_tdr(s, unit);
        buf[i] = tdr;
    }

    xfer_master_copy_to_dma(s, (uint8_t *)buf, s->wait_rcv.n, sizeof(uint32_t));

    // ensure RDR and serializer have correct values
    uint32_t unit = ((uint32_t *)s->rcvbuf.buffer)[s->wait_rcv.n - 1];
    uint32_t tdr = xfer_master_unit_to_tdr(s, unit);
    s->serializer = tdr & 0xFFFF;
    s->reg_rdr = tdr & 0xFFFF;
}

static void xfer_master_read_to_dma_novarps8(SpiState *s)
{
    uint8_t *buf = g_new0(uint8_t, s->wait_rcv.n);

    for (int i = 0; i < s->wait_rcv.n; i++) {
        uint32_t unit = ((uint32_t *)s->rcvbuf.buffer)[i];
        uint32_t tdr = xfer_master_unit_to_tdr(s, unit);
        buf[i] = tdr & 0xFF;
    }

    xfer_master_copy_to_dma(s, buf, s->wait_rcv.n, sizeof(uint8_t));

    // ensure RDR and serializer have correct values
    uint32_t unit = ((uint32_t *)s->rcvbuf.buffer)[s->wait_rcv.n - 1];
    uint32_t tdr = xfer_master_unit_to_tdr(s, unit);
    s->serializer = tdr & 0xFFFF;
    s->reg_rdr = tdr & 0xFFFF;
}

static void xfer_master_read_to_dma_novarps16(SpiState *s)
{
    uint16_t *buf = g_new0(uint16_t, s->wait_rcv.n);

    for (int i = 0; i < s->wait_rcv.n; i++) {
        uint32_t unit = ((uint32_t *)s->rcvbuf.buffer)[i];
        uint32_t tdr = xfer_master_unit_to_tdr(s, unit);
        buf[i] = tdr & 0xFFFF;
    }

    xfer_master_copy_to_dma(s, (uint8_t *)buf, s->wait_rcv.n, sizeof(uint16_t));

    // ensure RDR and serializer have correct values
    uint32_t unit = ((uint32_t *)s->rcvbuf.buffer)[s->wait_rcv.n - 1];
    uint32_t tdr = xfer_master_unit_to_tdr(s, unit);
    s->serializer = tdr & 0xFFFF;
    s->reg_rdr = tdr & 0xFFFF;
}

static void xfer_master_read_to_tdr(SpiState *s)
{
    uint32_t unit = ((uint32_t *)s->rcvbuf.buffer)[s->wait_rcv.n - 1];
    uint32_t tdr = xfer_master_unit_to_tdr(s, unit);

    s->serializer = tdr & 0xFFFFF;
    s->reg_rdr = tdr;
    s->reg_sr |= SR_RDRF;
}

static void xfer_transmit_tdr_master_finish(SpiState *s);
static void xfer_dma_do_tcr_master_finish(SpiState *s);

inline static void xfer_master_wait_receive_finish(SpiState *s)
{
    if (s->reg_sr & SR_RDRF) {
        s->reg_sr |= SR_OVRES;
    }

    if (s->dma_rx_enabled) {
        if (s->reg_mr & MR_PS) {
            xfer_master_read_to_dma_varps(s);
        } else {
            uint8_t pcnr = pcs_to_nr(s, (s->reg_mr >> 16) & 0x0F);
            uint8_t bits = num_transmit_bits(s, pcnr);

            if (bits == 8) {
                xfer_master_read_to_dma_novarps8(s);
            } else {
                xfer_master_read_to_dma_novarps16(s);
            }
        }
    } else {
        xfer_master_read_to_tdr(s);
    }

    if (s->wait_rcv.ty == AT91_SPI_WAIT_RCV_TDR)
        xfer_transmit_tdr_master_finish(s);
    else if (s->wait_rcv.ty == AT91_SPI_WAIT_RCV_DMA)
        xfer_dma_do_tcr_master_finish(s);

    s->wait_rcv.ty = AT91_SPI_WAIT_RCV_NONE;
    s->wait_rcv.n = 0;

    // all data recieved, resume CPU        // TODO: remove?
    resume_all_vcpus();

    update_irq(s);
}


static void iox_transmit_units(SpiState *s, uint32_t *units, uint32_t n)
{
    uint8_t *data = (uint8_t *)units;
    uint32_t len = n * sizeof(uint32_t);

    if (!s->server)
        return;

    int status = iox_send_data_multiframe_new(s->server, IOX_CAT_DATA, IOX_CID_DATA_OUT, len, data);
    if (status) {
        error_report("at91.spi: failed to transmit data: %d", status);
        abort();
    }
}

static uint32_t xfer_transmit_dmabuf_varps(SpiState *s, void *dmabuf, uint32_t len)
{
    // data is 32 bit full TDR format
    uint32_t num_units = len / sizeof(uint32_t);
    uint32_t *units;

    if (len - num_units * sizeof(uint32_t) > 0) {
        error_report("at91.spi: invalid transmit data length %d", len);
        abort();
    }

    units = g_new0(uint32_t, num_units);
    if (!units) {
        error_report("at91.spi: out of memory");
        abort();
    }

    for (uint32_t i = 0; i < num_units; i++) {
        uint32_t tdr = le32_to_cpu(((uint32_t *)dmabuf)[i]);        // XXX: assumes little-endian
        uint8_t pcnr = pcs_to_nr(s, (tdr >> 16) & 0x0F);
        uint8_t bits = num_transmit_bits(s, pcnr);
        uint16_t data = tdr & ((1 << ((uint32_t)bits)) - 1);

        // TODO: lastxfer?

        units[i] = to_xfer_unit(pcnr, bits, data);
    }

    // if no server set up or it doesn't have a client: echo data to rcvbuf
    if (!s->server || !s->server->client) {
        buffer_reserve(&s->rcvbuf, num_units * sizeof(uint32_t));
        buffer_append(&s->rcvbuf, units, num_units * sizeof(uint32_t));
    }

    xfer_master_wait_receive_start_dma(s, num_units);
    iox_transmit_units(s, units, num_units);
    g_free(units);

    return num_units;
}

static uint32_t xfer_transmit_dmabuf_novarps(SpiState *s, void *dmabuf, uint32_t len)
{
    // data is 8 to 16 bit raw data, stored in either 8 or 16 bit units

    uint8_t pcnr = pcs_to_nr(s, (s->reg_mr >> 16) & 0x0F);
    uint8_t bits = num_transmit_bits(s, pcnr);
    uint32_t num_units;
    uint32_t *units;

    if (bits > 8) {     // 16bit storage
        num_units = len / sizeof(uint16_t);
        if (len - num_units * sizeof(uint16_t) > 0) {
            error_report("at91.spi: invalid transmit data length %d", len);
            abort();
        }
    } else {            // 8bit storage
        num_units = len / sizeof(uint8_t);
    }

    units = g_new0(uint32_t, num_units);
    if (!units) {
        error_report("at91.spi: out of memory");
        abort();
    }

    if (bits > 8) {     // 16bit storage
        uint16_t mask = ((1 << ((uint32_t)bits)) - 1);
        for (uint32_t i = 0; i < num_units; i++) {
            uint16_t data = le16_to_cpu(((uint16_t*)dmabuf)[i]);    // XXX: assumes little-endian
            units[i] = to_xfer_unit(pcnr, bits, data & mask);
        }

    } else {            // 8bit storage
        for (uint32_t i = 0; i < num_units; i++) {
            units[i] = to_xfer_unit(pcnr, bits, ((uint8_t *)dmabuf)[i]);
        }
    }

    // if no server set up or it doesn't have a client: echo data to rcvbuf
    if (!s->server || !s->server->client) {
        buffer_reserve(&s->rcvbuf, num_units * sizeof(uint32_t));
        buffer_append(&s->rcvbuf, units, num_units * sizeof(uint32_t));
    }

    xfer_master_wait_receive_start_dma(s, num_units);
    iox_transmit_units(s, units, num_units);
    g_free(units);

    return num_units;
}

inline static uint32_t xfer_transmit_dmabuf(SpiState *s, void *dmabuf, uint32_t len)
{
    if (s->reg_mr & MR_PS)
        return xfer_transmit_dmabuf_varps(s, dmabuf, len);
    else
        return xfer_transmit_dmabuf_novarps(s, dmabuf, len);
}

static void xfer_transmit_tdr_master_finish(SpiState *s)
{
        s->reg_sr |= SR_TDRE;
        s->reg_sr |= SR_TXEMPTY;
        update_irq(s);
}

static void xfer_transmit_tdr(SpiState *s)
{
    if (s->reg_mr & MR_MSTR) {              // master mode
        uint8_t pcnr = pcs_to_nr(s, (((s->reg_mr & MR_PS) ? s->reg_tdr : s->reg_mr) >> 16) & 0x0F);
        uint8_t bits = num_transmit_bits(s, pcnr);
        uint16_t data = s->reg_tdr & ((1 << ((uint32_t)bits)) - 1);
        uint32_t unit = to_xfer_unit(pcnr, bits, data);

        s->serializer = s->reg_tdr;

        // if no server set up or it doesn't have a client: echo data to rcvbuf
        if (!s->server || !s->server->client) {
            buffer_reserve(&s->rcvbuf, sizeof(uint32_t));
            buffer_append(&s->rcvbuf, &unit, sizeof(uint32_t));
        }

        // TODO: lastxfer?

        xfer_master_wait_receive_start_tdr(s);
        iox_transmit_units(s, &unit, 1);
    } else {                                // slave mode
        // Master needs to initiate transfer. It is possible to fill serializer
        // and transmit data register in preparation.

        s->reg_sr &= ~SR_TDRE;

        if (s->reg_sr & SR_TXEMPTY) {       // if serializer empty, load serializer
            s->serializer = s->reg_tdr;
            s->reg_sr &= ~SR_TXEMPTY;
        }
    }
}


static void xfer_dma_rx_start(void *opaque)
{
    SpiState *s = opaque;
    s->dma_rx_enabled = true;
}

static void xfer_dma_rx_stop(void *opaque)
{
    SpiState *s = opaque;
    s->dma_rx_enabled = false;
}

static void xfer_dma_do_tcr_master_start(SpiState *s)
{
    uint8_t *data = g_new0(uint8_t, s->pdc.reg_tcr);
    if (!data) {
        error_report("at91.spi: out of memory");
        abort();
    }

    MemTxResult result = address_space_rw(&address_space_memory, s->pdc.reg_tpr,
                                          MEMTXATTRS_UNSPECIFIED, data, s->pdc.reg_tcr, false);

    if (result) {
        g_free(data);
        error_report("at91.spi: failed to read memory: %d", result);
        abort();
    }

    xfer_transmit_dmabuf(s, data, s->pdc.reg_tcr);
    g_free(data);
}

static void xfer_dma_do_tcr_master_finish(SpiState *s)
{
    s->pdc.reg_tpr += s->pdc.reg_tcr;
    s->pdc.reg_tcr = 0;

    if (s->pdc.reg_tncr) {
        s->pdc.reg_tcr = s->pdc.reg_tncr;
        s->pdc.reg_tncr = 0;

        s->pdc.reg_tpr = s->pdc.reg_tnpr;
        s->pdc.reg_tnpr = 0;

        xfer_dma_do_tcr_master_start(s);
    } else {
        s->dma_tx_enabled = false;
        s->reg_sr |= SR_TXBUFE;
    }

    s->reg_sr |= SR_ENDTX;
    update_irq(s);
}

static void xfer_dma_tx_start(void *opaque)
{
    SpiState *s = opaque;

    if (s->dma_tx_enabled)      // might be setting TNCR/TPCR
        return;

    s->dma_tx_enabled = true;

    if (!(s->reg_mr & MR_MSTR))
        return;     // slave mode: master needs to initiate transmission

    if (!s->pdc.reg_tcr && s->pdc.reg_tncr) {
        s->pdc.reg_tcr = s->pdc.reg_tncr;
        s->pdc.reg_tncr = 0;

        s->pdc.reg_tpr = s->pdc.reg_tnpr;
        s->pdc.reg_tnpr = 0;
    }

    if (s->pdc.reg_tcr)
        xfer_dma_do_tcr_master_start(s);
}

static void xfer_dma_tx_stop(void *opaque)
{
    SpiState *s = opaque;
    s->dma_tx_enabled = false;
}


static void iox_receive_data(SpiState *s, struct iox_data_frame *frame)
{
    if (s->wait_rcv.ty == AT91_SPI_WAIT_RCV_NONE) {
        warn_report("at91.spi: not expecting any data, dropping it");
        return;
    }

    buffer_reserve(&s->rcvbuf, frame->len);
    buffer_append(&s->rcvbuf, frame->payload, frame->len);

    if (s->rcvbuf.offset >= s->wait_rcv.n * sizeof(uint32_t)) {
        if (s->rcvbuf.offset > s->wait_rcv.n * sizeof(uint32_t))
            warn_report("at91.spi: received more data than expected, dropping overflow");

        xfer_master_wait_receive_finish(s);
        buffer_reset(&s->rcvbuf);
    }
}

static void iox_receive(struct iox_data_frame *frame, void *opaque)
{
    SpiState *s = opaque;

    switch (frame->cat) {
    case IOX_CAT_DATA:
        switch (frame->id) {
        case IOX_CID_DATA_IN:
            iox_receive_data(s, frame);
            break;
        }
        break;

    case IOX_CAT_FAULT:
        switch (frame->id) {
        case IOX_CID_FAULT_MODF:
            s->reg_sr |= SR_MODF;
            update_irq(s);
            break;

        case IOX_CID_FAULT_OVRES:
            s->reg_sr |= SR_OVRES;
            update_irq(s);
            break;
        }
        break;
    }
}


static uint64_t spi_mmio_read(void *opaque, hwaddr offset, unsigned size)
{
    SpiState *s = opaque;

    switch (offset) {
    case SPI_MR:
        return s->reg_mr;

    case SPI_RDR:
        s->reg_sr &= ~SR_RDRF;
        return s->reg_rdr;

    case SPI_SR:
        {
            uint32_t tmp = s->reg_sr;
            s->reg_sr &= ~(SR_MODF | SR_OVRES | SR_NSSR);
            update_irq(s);
            return tmp;
        }

    case SPI_IMR:
        return s->reg_imr;

    case SPI_CSR0:
        return s->reg_csr[0];

    case SPI_CSR1:
        return s->reg_csr[1];

    case SPI_CSR2:
        return s->reg_csr[2];

    case SPI_CSR3:
        return s->reg_csr[3];

    case PDC_START...PDC_END:
        return at91_pdc_get_register(&s->pdc, offset);

    default:
        error_report("at91.spi: illegal read access at 0x%02lx", offset);
        abort();
    }
}

static void spi_mmio_write(void *opaque, hwaddr offset, uint64_t value, unsigned size)
{
    SpiState *s = opaque;

    switch (offset) {
    case SPI_CR:
        if (value & CR_SPIEN && !(value & CR_SPIDIS)) {
            s->reg_sr |= SR_SPIENS | SR_TDRE | SR_TXEMPTY;
        }
        if (value & CR_SPIDIS) {
            s->reg_sr &= ~(SR_SPIENS | SR_TDRE | SR_TXEMPTY);
        }
        if (value & CR_SWRST) {
            // TODO: keep enabled?

            // SPEC: Reset the SPI. A software-triggered hardware reset of the
            // SPI interface is performed. The SPI is in slave mode after
            // software reset.

            s->reg_mr     = 0x00;
            s->reg_rdr    = 0x00;
            s->reg_tdr    = 0x00;
            s->reg_sr     = 0xC0 | (s->reg_sr & 0x30);
            s->reg_imr    = 0x00;
            s->reg_csr[0] = 0x00;
            s->reg_csr[1] = 0x00;
            s->reg_csr[2] = 0x00;
            s->reg_csr[3] = 0x00;

            s->serializer = 0x00;

            // SPEC: PDC channels are not affected by software reset.
        }
        if (value & CR_LASTXFER) {
            // SPEC: The current NPCS will be deasserted after the character
            // written in TD has been transferred. When CSAAT is set, this
            // allows to close the communication with the current serial
            // peripheral by raising the corresponding NPCS line as soon as TD
            // transfer has completed.

            // currently ignored as NPCS lines are not emulated
        }
        update_irq(s);
        break;

    case SPI_MR:
        s->reg_mr = value;
        break;

    case SPI_TDR:
        s->reg_tdr = value;
        xfer_transmit_tdr(s);
        break;

    case SPI_IER:
        s->reg_imr |= value;
        update_irq(s);
        break;

    case SPI_IDR:
        s->reg_imr &= ~value;
        update_irq(s);
        break;

    case SPI_CSR0:
        s->reg_csr[0] = value;
        break;

    case SPI_CSR1:
        s->reg_csr[1] = value;
        break;

    case SPI_CSR2:
        s->reg_csr[2] = value;
        break;

    case SPI_CSR3:
        s->reg_csr[3] = value;
        break;

    case PDC_START...PDC_END:
        {
            At91PdcOps ops = {
                .opaque = s,
                .dma_rx_start = xfer_dma_rx_start,
                .dma_rx_stop  = xfer_dma_rx_stop,
                .dma_tx_start = xfer_dma_tx_start,
                .dma_tx_stop  = xfer_dma_tx_stop,
                .update_irq   = (void (*)(void*))update_irq,
                .flag_endrx   = SR_ENDRX,
                .flag_endtx   = SR_ENDTX,
                .flag_rxbuff  = SR_RXBUFF,
                .flag_txbufe  = SR_TXBUFE,
                .reg_sr       = &s->reg_sr,
            };

            at91_pdc_generic_set_register(&s->pdc, &ops, offset, value);
            update_irq(s);
        }
        break;

    default:
        error_report("at91.spi: illegal read access at 0x%02lx", offset);
        abort();
    }
}

static const MemoryRegionOps spi_mmio_ops = {
    .read = spi_mmio_read,
    .write = spi_mmio_write,
    .impl.min_access_size = 4,
    .impl.max_access_size = 4,
    .valid.min_access_size = 4,
    .valid.max_access_size = 4,
    .endianness = DEVICE_NATIVE_ENDIAN,
};


static void spi_device_init(Object *obj)
{
    SysBusDevice *sbd = SYS_BUS_DEVICE(obj);
    SpiState *s = AT91_SPI(obj);

    sysbus_init_irq(sbd, &s->irq);

    memory_region_init_io(&s->mmio, OBJECT(s), &spi_mmio_ops, s, "at91.spi", 0x4000);
    sysbus_init_mmio(SYS_BUS_DEVICE(s), &s->mmio);
}

static void spi_reset_registers(SpiState *s)
{
    s->reg_mr     = 0x00;
    s->reg_rdr    = 0x00;
    s->reg_tdr    = 0x00;
    s->reg_sr     = 0xF0;
    s->reg_imr    = 0x00;
    s->reg_csr[0] = 0x00;
    s->reg_csr[1] = 0x00;
    s->reg_csr[2] = 0x00;
    s->reg_csr[3] = 0x00;

    s->dma_rx_enabled = false;
    s->dma_tx_enabled = false;

    s->serializer = 0x00;

    at91_pdc_reset_registers(&s->pdc);
}

static void spi_device_realize(DeviceState *dev, Error **errp)
{
    SpiState *s = AT91_SPI(dev);
    spi_reset_registers(s);

    buffer_init(&s->rcvbuf, "at91.spi.rcvbuf");
    buffer_reserve(&s->rcvbuf, 1024);

    if (s->socket) {
        SocketAddress addr;
        addr.type = SOCKET_ADDRESS_TYPE_UNIX;
        addr.u.q_unix.path = s->socket;

        IoXferServer *srv = iox_server_new();
        if (!srv) {
            error_set(errp, ERROR_CLASS_GENERIC_ERROR, "cannot allocate server");
            return;
        }

        iox_server_set_handler(srv, iox_receive, s);

        if (iox_server_open(srv, &addr, errp))
            return;

        s->server = srv;
        info_report("at91.spi: listening on %s", s->socket);
    }
}

static void spi_device_unrealize(DeviceState *dev, Error **errp)
{
    SpiState *s = AT91_SPI(dev);

    if (s->server) {
        iox_server_free(s->server);
        s->server = NULL;
    }

    buffer_free(&s->rcvbuf);
}

static void spi_device_reset(DeviceState *dev)
{
    SpiState *s = AT91_SPI(dev);
    spi_reset_registers(s);
}

static Property spi_device_properties[] = {
    DEFINE_PROP_STRING("socket", SpiState, socket),
    DEFINE_PROP_END_OF_LIST(),
};

static void spi_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    dc->realize = spi_device_realize;
    dc->unrealize = spi_device_unrealize;
    dc->reset = spi_device_reset;
    device_class_set_props(dc, spi_device_properties);
}

static const TypeInfo spi_device_info = {
    .name = TYPE_AT91_SPI,
    .parent = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(SpiState),
    .instance_init = spi_device_init,
    .class_init = spi_class_init,
};

static void spi_register_types(void)
{
    type_register_static(&spi_device_info);
}

type_init(spi_register_types)
