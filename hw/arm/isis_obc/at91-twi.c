/*
 * AT91 Two-Wire Interface (I2C).
 *
 * See at91-twi.h for details.
 *
 * Copyright (c) 2019-2020 KSat e.V. Stuttgart
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or, at your
 * option, any later version. See the COPYING file in the top-level directory.
 */

// Overview of TODOs:
// - Slave mode (only master mode implemented).
// - Software-reset (CR_SWRST) not implemented.

#include "at91-twi.h"
#include "exec/address-spaces.h"
#include "qemu/error-report.h"
#include "qapi/error.h"
#include "hw/irq.h"
#include "hw/qdev-properties.h"

#define IOX_CAT_DATA            0x01
#define IOX_CAT_FAULT           0x02

#define IOX_CID_DATA_IN         0x01
#define IOX_CID_DATA_OUT        0x02
#define IOX_CID_CTRL_START      0x03
#define IOX_CID_CTRL_STOP       0x04

#define IOX_CID_FAULT_OVRE      0x01
#define IOX_CID_FAULT_NACK      0x02
#define IOX_CID_FAULT_ARBLST    0x03

#define TWI_CR          0x00
#define TWI_MMR         0x04
#define TWI_SMR         0x08
#define TWI_IADR        0x0C
#define TWI_CWGR        0x10
#define TWI_SR          0x20
#define TWI_IER         0x24
#define TWI_IDR         0x28
#define TWI_IMR         0x2C
#define TWI_RHR         0x30
#define TWI_THR         0x34

#define CR_START        BIT(0)
#define CR_STOP         BIT(1)
#define CR_MSEN         BIT(2)
#define CR_MSDIS        BIT(3)
#define CR_SVEN         BIT(4)
#define CR_SVDIS        BIT(5)
#define CR_SWRST        BIT(7)

#define MMR_IADRSZ(s)   (((s)->reg_mmr >> 8) & 0x03)
#define MMR_DADR(s)     (((s)->reg_mmr >> 16) & 0x7f)
#define MMR_MREAD       BIT(12)

#define SMR_SADR(s)     (((s)->reg_smr >> 16) & 0x7f)

#define CWGR_CLDIV(s)   ((s)->reg_cwgr& 0xff)
#define CWGR_CHDIV(s)   (((s)->reg_cwgr >> 8) & 0xff)
#define CWGR_CKDIV(s)   (((s)->reg_cwgr >> 16) & 0x07)

#define SR_TXCOMP       BIT(0)
#define SR_RXRDY        BIT(1)
#define SR_TXRDY        BIT(2)
#define SR_SVREAD       BIT(3)
#define SR_SVACC        BIT(4)
#define SR_GACC         BIT(5)
#define SR_OVRE         BIT(6)
#define SR_NACK         BIT(8)
#define SR_ARBLST       BIT(9)
#define SR_SCLWS        BIT(10)
#define SR_EOSACC       BIT(11)
#define SR_ENDRX        BIT(12)
#define SR_ENDTX        BIT(13)
#define SR_RXBUFF       BIT(14)
#define SR_TXBUFE       BIT(15)


__attribute__ ((__packed__))
struct start_frame {
    uint8_t dadr;
    uint8_t iadrsz;
    uint8_t iadr0;
    uint8_t iadr1;
    uint8_t iadr2;
};


static void twi_update_irq(TwiState *s)
{
    qemu_set_irq(s->irq, !!(s->reg_imr & s->reg_sr));
}

static void twi_update_clock(TwiState *s)
{
    unsigned ldiv = (CWGR_CLDIV(s) * (1 << CWGR_CKDIV(s))) + 4;
    unsigned hdiv = (CWGR_CHDIV(s) * (1 << CWGR_CKDIV(s))) + 4;
    s->clock = s->mclk / (ldiv + hdiv);

    if (s->clock) {     // avoid issues during initialization
        ptimer_transaction_begin(s->chrtx_timer);
        ptimer_set_freq(s->chrtx_timer, s->clock);
        ptimer_transaction_commit(s->chrtx_timer);
    }
}

void at91_twi_set_master_clock(TwiState *s, unsigned mclk)
{
    s->mclk = mclk;
    twi_update_clock(s);
}


static void xfer_send_frame_start(TwiState *s)
{
    struct start_frame data = {
        .dadr = MMR_DADR(s) | ((s->reg_mmr & MMR_MREAD) >> 5),
        .iadrsz = MMR_IADRSZ(s),
        .iadr0 = s->reg_iadr & 0xff,
        .iadr1 = (s->reg_iadr >> 8) & 0xff,
        .iadr2 = (s->reg_iadr >> 16) & 0xff,
    };

    iox_send_data_new(s->server, IOX_CAT_DATA, IOX_CID_CTRL_START,
                      sizeof(struct start_frame), (uint8_t *)&data);
}

static void xfer_send_frame_stop(TwiState *s)
{
    iox_send_command_new(s->server, IOX_CAT_DATA, IOX_CID_CTRL_STOP);
}


static int iox_send_chars(TwiState *s, uint8_t* data, unsigned len)
{
    if (!s->server)
        return 0;

    return iox_send_data_multiframe_new(s->server, IOX_CAT_DATA, IOX_CID_DATA_OUT, len, data);
}

static int xfer_dma_tx_do_tcr(TwiState *s)
{
    uint8_t *data = g_new0(uint8_t, s->pdc.reg_tcr);
    if (!data)
        return -ENOMEM;

    MemTxResult result = address_space_rw(&address_space_memory, s->pdc.reg_tpr,
                                          MEMTXATTRS_UNSPECIFIED, data, s->pdc.reg_tcr, false);
    if (result) {
        g_free(data);
        error_report("at91.twi: failed to read memory: %d", result);
        return -EIO;
    }

    int status = iox_send_chars(s, data, s->pdc.reg_tcr);
    g_free(data);

    s->pdc.reg_tpr += s->pdc.reg_tcr;
    s->pdc.reg_tcr = 0;

    return status;
}

static void xfer_chrtx_timer_tick(void *opaque)
{
    TwiState *s = opaque;

    // If we reach this point, we assuem that the transmission writes to THR
    // are complete. Send all buffered data with start and stop frames.

    xfer_send_frame_start(s);
    iox_send_chars(s, s->sendbuf.buffer, s->sendbuf.offset);
    xfer_send_frame_stop(s);

    buffer_reset(&s->sendbuf);

    ptimer_transaction_begin(s->chrtx_timer);
    ptimer_stop(s->chrtx_timer);
    ptimer_transaction_commit(s->chrtx_timer);

    s->reg_sr |= SR_TXCOMP;
    twi_update_irq(s);
}

static void xfer_chr_transmit(TwiState *s, uint8_t value)
{
    buffer_reserve(&s->sendbuf, 1);
    buffer_append(&s->sendbuf, &value, 1);

    // the actual send happens when all data has been gathered in the send task
    // resets timer if already running
    ptimer_transaction_begin(s->chrtx_timer);
    ptimer_set_limit(s->chrtx_timer, 2 /* load-to-shift, send shift */ , true);
    ptimer_run(s->chrtx_timer, true);
    ptimer_transaction_commit(s->chrtx_timer);

    s->reg_sr |= SR_TXRDY;
    twi_update_irq(s);
}

static void xfer_chr_receive(TwiState *s, uint8_t chr)
{
    if (s->reg_sr & SR_RXRDY) {
        s->reg_sr |= SR_OVRE;
    }

    // The last character is transferred into US_RHR and overwrites the previous one.
    s->reg_rhr = chr;
    s->reg_sr |= SR_RXRDY;

    twi_update_irq(s);
}

static void xfer_receiver_next(TwiState *s)
{
    if (buffer_empty(&s->rcvbuf))
        return;

    if (s->reg_sr & SR_RXRDY)
        return;

    uint8_t chr = s->rcvbuf.buffer[0];
    buffer_advance(&s->rcvbuf, 1);

    xfer_chr_receive(s, chr);
}


static void xfer_receiver_dma_updreg(TwiState *s)
{
    // if first DMA buffer is full, set its flag
    if (!s->pdc.reg_rcr)
        s->reg_sr |= SR_ENDRX;

    // if there is no second buffer, indicate all buffers full
    if (!s->pdc.reg_rcr && !s->pdc.reg_rncr)
        s->reg_sr |= SR_RXBUFF;

    // move to next buffer if we have RNCR and RCR is zero
    if (!s->pdc.reg_rcr && s->pdc.reg_rncr) {
        s->pdc.reg_rpr = s->pdc.reg_rnpr;
        s->pdc.reg_rnpr = 0;

        s->pdc.reg_rcr = s->pdc.reg_rncr;
        s->pdc.reg_rncr = 0;
    }
}

static void xfer_receiver_dma_rcr(TwiState *s)
{
    MemTxAttrs attrs = MEMTXATTRS_UNSPECIFIED;

    uint16_t len = s->rcvbuf.offset < s->pdc.reg_rcr ? s->rcvbuf.offset : s->pdc.reg_rcr;
    uint8_t *data = s->rcvbuf.buffer;

    MemTxResult result = address_space_rw(&address_space_memory, s->pdc.reg_rpr, attrs, data, len, true);
    if (result) {
        error_report("at91.twi: failed to write memory: %d", result);
        abort();
    }

    buffer_advance(&s->rcvbuf, len);
    s->pdc.reg_rpr += len;
    s->pdc.reg_rcr -= len;
}

static void xfer_receiver_dma_rhr(TwiState *s)
{
    MemTxAttrs attrs = MEMTXATTRS_UNSPECIFIED;
    uint8_t chr = s->reg_rhr;

    MemTxResult result = address_space_rw(&address_space_memory, s->pdc.reg_rpr, attrs, &chr, 1, true);
    if (result) {
        error_report("at91.usart: failed to write memory: %d", result);
        abort();
    }

    s->pdc.reg_rpr += 1;
    s->pdc.reg_rcr -= 1;
    s->reg_sr &= ~SR_RXRDY;
}

static void __xfer_receiver_dma(TwiState *s)
{
    // read from RHR
    if (s->reg_sr & SR_RXRDY) {
        xfer_receiver_dma_rhr(s);
        xfer_receiver_dma_updreg(s);
    }

    // early return if both DMA buffers are full or all data processed
    if (!s->pdc.reg_rcr || buffer_empty(&s->rcvbuf))
        return;

    // read from buffer to rcr
    xfer_receiver_dma_rcr(s);
    xfer_receiver_dma_updreg(s);

    // early return if both DMA buffers are full or all data processed
    if (!s->pdc.reg_rcr || buffer_empty(&s->rcvbuf))
        return;

    // read from buffer to rncr
    xfer_receiver_dma_rcr(s);

    // if both buffers are full, indicate this
    if (!s->pdc.reg_rcr) {
        s->reg_sr |= SR_ENDRX;
        s->reg_sr |= SR_RXBUFF;
    }
}

static void xfer_receiver_dma(TwiState *s)
{
    __xfer_receiver_dma(s);
    twi_update_irq(s);

    // DMA needs to be re-enabled if buffer is full
    if (!s->pdc.reg_rcr)
        s->dma_rx_enabled = false;

    // if both DMA buffers are full and we still have data, read to RHR
    if (!s->pdc.reg_rcr && !s->pdc.reg_rncr)
        xfer_receiver_next(s);
}


static void xfer_dma_rx_start(void *opaque)
{
    TwiState *s = opaque;

    s->dma_rx_enabled = true;
    xfer_receiver_dma(s);
}

static void xfer_dma_rx_stop(void *opaque)
{
    TwiState *s = opaque;
    s->dma_rx_enabled = false;
}

static void xfer_dma_tx_start(void *opaque)
{
    TwiState *s = opaque;

    if (!s->pdc.reg_tcr)
        return;

    xfer_send_frame_start(s);

    if (s->pdc.reg_tcr) {
        int status = xfer_dma_tx_do_tcr(s);
        if (status) {
            error_report("at91.twi: dma transfer failed");
            abort();
        }
    }

    if (s->pdc.reg_tncr) {
        s->pdc.reg_tcr = s->pdc.reg_tncr;
        s->pdc.reg_tncr = 0;

        s->pdc.reg_tpr = s->pdc.reg_tnpr;
        s->pdc.reg_tnpr = 0;

        int status = xfer_dma_tx_do_tcr(s);
        if (status) {
            error_report("at91.twi: dma transfer failed");
            abort();
        }
    }

    xfer_send_frame_stop(s);

    s->reg_sr |= SR_ENDTX | SR_TXBUFE | SR_TXCOMP | SR_TXRDY;
    twi_update_irq(s);
}

static void xfer_dma_tx_stop(void *opaque)
{
    /* no-op */
}


static int iox_receive_data(TwiState *s, struct iox_data_frame *frame)
{
    bool in_progress = !buffer_empty(&s->rcvbuf);

    buffer_reserve(&s->rcvbuf, frame->len);
    buffer_append(&s->rcvbuf, frame->payload, frame->len);
    int status = iox_send_u32_resp(s->server, frame, 0);
    if (status)
        return status;

    if (in_progress)
        return 0;

    if (s->dma_rx_enabled)
        xfer_receiver_dma(s);
    else
        xfer_receiver_next(s);

    return 0;
}

static void iox_receive(struct iox_data_frame *frame, void *opaque)
{
    TwiState *s = opaque;
    int status = 0;

    switch (frame->cat) {
    case IOX_CAT_DATA:
        switch (frame->id) {
        case IOX_CID_DATA_IN:
            status = iox_receive_data(s, frame);
            break;
        }
        break;

    case IOX_CAT_FAULT:
        switch (frame->id) {
        case IOX_CID_FAULT_OVRE:
            s->reg_sr |= SR_OVRE;
            break;

        case IOX_CID_FAULT_NACK:
            // SPEC: Set at the same time as TXCOMP.
            s->reg_sr |= SR_NACK | SR_TXCOMP;
            break;

        case IOX_CID_FAULT_ARBLST:
            // SPEC: TXCOMP is set at the same time.
            s->reg_sr |= SR_ARBLST | SR_TXCOMP;
            break;
        }
        break;
    }

    if (status) {
        error_report("error handling command frame: cat: %d, id: %d", frame->cat, frame->id);
        abort();
    }
}


static uint64_t twi_mmio_read(void *opaque, hwaddr offset, unsigned size)
{
    TwiState *s = opaque;

    switch (offset) {
    case TWI_MMR:
        return s->reg_mmr;

    case TWI_SMR:
        return s->reg_smr;

    case TWI_IADR:
        return s->reg_iadr;

    case TWI_CWGR:
        return s->reg_cwgr;

    case TWI_SR:
        {
            uint32_t sr = s->reg_sr;
            s->reg_sr &= ~(SR_GACC | SR_OVRE | SR_NACK | SR_ARBLST | SR_EOSACC);
            twi_update_irq(s);
            return sr;
        }

    case TWI_IMR:
        return s->reg_imr;

    case TWI_RHR:
        s->reg_sr &= ~SR_RXRDY;
        twi_update_irq(s);
        return s->reg_rhr;

    case PDC_START...PDC_END:
        return at91_pdc_get_register(&s->pdc, offset);
        return 0;

    default:
        error_report("at91.twi: illegal read access at 0x%02lx", offset);
        abort();
    }
}

static void twi_mmio_write(void *opaque, hwaddr offset, uint64_t value, unsigned size)
{
    TwiState *s = opaque;

    switch (offset) {
    case TWI_CR:
        if (value & CR_START) {
            if (s->mode != AT91_TWI_MODE_MASTER || !(s->reg_mmr & MMR_MREAD))
                warn_report("at91.twi: sending start frame when not in master-read mode");

            xfer_send_frame_start(s);

            // SPEC: A frame beginning with a START bit is transmitted
            // according to the features defined in the mode register.
            //
            // This action is necessary when the TWI peripheral wants to read
            // data from a slave. When configured in Master Mode with a write
            // operation, a frame is sent as soon as the user writes a
            // character in the Transmit Holding Register (TWI_THR).
        }
        if (value & CR_STOP) {
            if (s->mode != AT91_TWI_MODE_MASTER)
                warn_report("at91.twi: sending stop frame when not in master mode");

            xfer_send_frame_stop(s);

            // SPEC: STOP Condition is sent just after completing the current
            // byte transmission in master read mode.
            //
            // – In single data byte master read, the START and STOP must both
            //   be set.
            // – In multiple data bytes master read, the STOP must be set after
            //   the last data received but one.
            // – In master read mode, if a NACK bit is received, the STOP is
            //   automatically performed.
            // – In multiple data write operation, when both THR and shift
            //   register are empty, a STOP condition is automatically sent.
        }
        if ((value & CR_MSEN) && !(value & CR_MSDIS)) {
            bool txc = s->reg_sr & SR_TXCOMP;

            if (s->mode == AT91_TWI_MODE_OFFLINE || (txc && s->mode == AT91_TWI_MODE_SLAVE)) {
                info_report("at91.twi: enabling master mode");
                s->mode = AT91_TWI_MODE_MASTER;

                // SPEC: TXRDY is also set when MSEN is set.
                s->reg_sr |= SR_TXRDY;
                twi_update_irq(s);
            } else if (s->mode == AT91_TWI_MODE_SLAVE) {
                error_report("at91.twi: switching from slave to master mode only allowed if SR_TXCOMP is set");
                abort();
            }
        }
        if (value & CR_MSDIS) {
            if (s->mode == AT91_TWI_MODE_MASTER) {
                info_report("at91.twi: disabling master mode");

                // SPEC: The master mode is disabled, all pending data is
                // transmitted. The shifter and holding characters (if it
                // contains data) are transmitted in case of write operation.
                // In read operation, the character being transferred must be
                // completely received before disabling.

                s->mode = AT91_TWI_MODE_OFFLINE;
            } else if (s->mode == AT91_TWI_MODE_SLAVE) {
                warn_report("at91.twi: calling CR_MSDIS while TWI in slave mode");
            }

        }
        if ((value & CR_SVEN) && !(value & CR_SVDIS)) {
            bool txc = s->reg_sr & SR_TXCOMP;

            if (s->mode == AT91_TWI_MODE_OFFLINE || (txc && s->mode == AT91_TWI_MODE_MASTER)) {
                info_report("at91.twi: enabling slave mode");
                s->mode = AT91_TWI_MODE_SLAVE;
            } else if (s->mode == AT91_TWI_MODE_MASTER) {
                error_report("at91.twi: switching from master to slave mode only allowed if SR_TXCOMP is set");
                abort();
            }
        }
        if (value & CR_SVDIS) {
            if (s->mode == AT91_TWI_MODE_SLAVE) {
                info_report("at91.twi: disabling slave mode");

                // SPEC: The slave mode is disabled. The shifter and holding
                // characters (if it contains data) are transmitted in case of
                // read operation. In write operation, the character being
                // transferred must be completely received before disabling.

                s->mode = AT91_TWI_MODE_OFFLINE;
            } else if (s->mode == AT91_TWI_MODE_MASTER) {
                warn_report("at91.twi: calling CR_SVDIS while TWI in master mode");
            }
        }
        if (value & CR_SWRST) {
            // SPEC: Equivalent to a system reset.
            // TODO: what exactly does this mean?
            warn_report("at91.twi: CR_SWRST unimplemented");
        }
        break;

    case TWI_MMR:
        s->reg_mmr = value;
        break;

    case TWI_SMR:
        s->reg_smr = value;
        break;

    case TWI_IADR:
        s->reg_iadr = value;
        break;

    case TWI_CWGR:
        s->reg_cwgr = value;
        twi_update_clock(s);
        break;

    case TWI_IER:
        s->reg_imr |= value;
        twi_update_irq(s);
        break;

    case TWI_IDR:
        s->reg_imr &= ~value;
        twi_update_irq(s);
        break;

    case TWI_THR:
        xfer_chr_transmit(s, value);
        break;

    case PDC_START...PDC_END:
        {
            At91PdcOps ops = {
                .opaque = s,
                .dma_rx_start = xfer_dma_rx_start,
                .dma_rx_stop  = xfer_dma_rx_stop,
                .dma_tx_start = xfer_dma_tx_start,
                .dma_tx_stop  = xfer_dma_tx_stop,
                .update_irq   = (void (*)(void*))twi_update_irq,
                .flag_endrx   = SR_ENDRX,
                .flag_endtx   = SR_ENDTX,
                .flag_rxbuff  = SR_RXBUFF,
                .flag_txbufe  = SR_TXBUFE,
                .reg_sr       = &s->reg_sr,
            };

            at91_pdc_generic_set_register(&s->pdc, &ops, offset, value);
            twi_update_irq(s);
        }
        break;

    default:
        error_report("at91.twi: illegal write access at 0x%02lx", offset);
        abort();
    }
}

static const MemoryRegionOps twi_mmio_ops = {
    .read = twi_mmio_read,
    .write = twi_mmio_write,
    .impl.min_access_size = 4,
    .impl.max_access_size = 4,
    .valid.min_access_size = 4,
    .valid.max_access_size = 4,
    .endianness = DEVICE_NATIVE_ENDIAN,
};


static void twi_device_init(Object *obj)
{
    SysBusDevice *sbd = SYS_BUS_DEVICE(obj);
    TwiState *s = AT91_TWI(obj);

    sysbus_init_irq(sbd, &s->irq);

    memory_region_init_io(&s->mmio, OBJECT(s), &twi_mmio_ops, s, "at91.twi", 0x4000);
    sysbus_init_mmio(SYS_BUS_DEVICE(s), &s->mmio);

    s->chrtx_timer = ptimer_init(xfer_chrtx_timer_tick, s, PTIMER_POLICY_DEFAULT);
}

static void twi_reset_registers(TwiState *s)
{
    s->mode = AT91_TWI_MODE_OFFLINE;

    s->reg_mmr  = 0;
    s->reg_smr  = 0;
    s->reg_iadr = 0;
    s->reg_cwgr = 0;
    s->reg_sr   = 0xF009;
    s->reg_imr  = 0;
    s->reg_rhr  = 0;

    s->dma_rx_enabled = false;

    twi_update_clock(s);
}

static void twi_device_realize(DeviceState *dev, Error **errp)
{
    TwiState *s = AT91_TWI(dev);

    twi_reset_registers(s);

    buffer_init(&s->rcvbuf, "at91.twi.rcvbuf");
    buffer_reserve(&s->rcvbuf, 1024);

    buffer_init(&s->sendbuf, "at91.twi.sendbuf");
    buffer_reserve(&s->sendbuf, 256);

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
        info_report("at91.twi: listening on %s", s->socket);
    }
}

static void twi_device_unrealize(DeviceState *dev, Error **errp)
{
    TwiState *s = AT91_TWI(dev);

    if (s->server) {
        iox_server_free(s->server);
        s->server = NULL;
    }

    buffer_free(&s->rcvbuf);
}

static void twi_device_reset(DeviceState *dev)
{
    TwiState *s = AT91_TWI(dev);
    twi_reset_registers(s);
}

static Property twi_device_properties[] = {
    DEFINE_PROP_STRING("socket", TwiState, socket),
    DEFINE_PROP_END_OF_LIST(),
};

static void twi_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    dc->realize = twi_device_realize;
    dc->unrealize = twi_device_unrealize;
    dc->reset = twi_device_reset;
    device_class_set_props(dc, twi_device_properties);
}

static const TypeInfo twi_device_info = {
    .name = TYPE_AT91_TWI,
    .parent = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(TwiState),
    .instance_init = twi_device_init,
    .class_init = twi_class_init,
};

static void twi_register_types(void)
{
    type_register_static(&twi_device_info);
}

type_init(twi_register_types)
