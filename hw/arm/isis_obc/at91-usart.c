/*
 * AT91 Universal Synchronous/Asynchronous Receiver/Transmitter.
 *
 * See at91-usart.h for details.
 *
 * Copyright (c) 2019-2020 KSat e.V. Stuttgart
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or, at your
 * option, any later version. See the COPYING file in the top-level directory.
 */

// Overview of TODOs:
// - No read timeout: Has to be injected maually when transmitting to AT91.
// - DTR/RTS and RI/DSR/DCD/CTS pins unimplemented (as are
//   DTREN/DTRDIS/RTSEN/RTSDIS).
// - Simulate shift register not implemented, data is transferred immediately
//   rather than taking the appropriate time based on size and baud-rate.
// - US_NER update (error counting) not implemented.
// - SCK not supported as source for USART clock.
// - Start-/stop break sending (CR_STTBRK, CR_STPBRK) not supported.
// - Address sending (CR_SENDA) not implemented.
// - Mode register largely not implemented/unhandled.
// - Transmit timeguard (US_TTGR) not implemented.
// - US_IF, US_MAN not implemented.
//
// Note: Moste of these unimplemented features are not emulated as the data is
// transferred directly and the transfer channel is not emulated as close to
// hardware as required for these features to have an impact.


#include "at91-usart.h"
#include "exec/address-spaces.h"
#include "qemu/error-report.h"
#include "qapi/error.h"
#include "hw/irq.h"
#include "hw/qdev-properties.h"


#define IOX_CAT_DATA            0x01
#define IOX_CAT_FAULT           0x02

#define IOX_CID_DATA_IN         0x01
#define IOX_CID_DATA_OUT        0x02

#define IOX_CID_FAULT_OVRE      0x01
#define IOX_CID_FAULT_FRAME     0x02
#define IOX_CID_FAULT_PARE      0x03
#define IOX_CID_FAULT_TIMEOUT   0x04


#define MCKDIV      8           // TODO: product dependent divider, check value

#define US_CR       0x00
#define US_MR       0x04
#define US_IER      0x08
#define US_IDR      0x0C
#define US_IMR      0x10
#define US_CSR      0x14
#define US_RHR      0x18
#define US_THR      0x1C
#define US_BRGR     0x20
#define US_RTOR     0x24
#define US_TTGR     0x28
#define US_FIDI     0x40
#define US_NER      0x44
#define US_IF       0x4C
#define US_MAN      0x50

#define CR_RSTRX    BIT(2)
#define CR_RSTTX    BIT(3)
#define CR_RXEN     BIT(4)
#define CR_RXDIS    BIT(5)
#define CR_TXEN     BIT(6)
#define CR_TXDIS    BIT(7)
#define CR_RSTSTA   BIT(8)
#define CR_STTBRK   BIT(9)
#define CR_STPBRK   BIT(10)
#define CR_STTTO    BIT(11)
#define CR_SENDA    BIT(12)
#define CR_RSTIT    BIT(13)
#define CR_RSTNACK  BIT(14)
#define CR_RETTO    BIT(15)
#define CR_DTREN    BIT(16)
#define CR_DTRDIS   BIT(17)
#define CR_RTSEN    BIT(18)
#define CR_RTSDIS   BIT(19)

#define MR_USART_MODE(s)        (s->reg_mr & 0x0F)
#define MR_USCLKS(s)            ((s->reg_mr & 0x30) >> 4)
#define MR_CHRL(s)              (((s->reg_mr & 0xC0) >> 6) + 5)
#define MR_SYNC                 BIT(8)
#define MR_PAR(s)               ((s->reg_mr & 0x0E00) >> 9)
#define MR_NBSTOP(s)            ((s->reg_mr & 0x3000) >> 12)
#define MR_CHMODE(s)            ((s->reg_mr & 0xC000) >> 14)
#define MR_MSBF                 BIT(16)
#define MR_MODE9                BIT(17)
#define MR_CLKO                 BIT(18)
#define MR_OVER                 BIT(19)
#define MR_INACK                BIT(20)
#define MR_DSNACK               BIT(21)
#define MR_VAR_SYNC             BIT(22)
#define MR_MAX_ITERATION(s)     ((s->reg_mr & 0x07000000) >> 24)
#define MR_FILTER               BIT(28)
#define MR_MAN                  BIT(29)
#define MR_MODESYNC             BIT(30)
#define MR_ONEBIT               BIT(31)

enum usart_mode {
    USART_MODE_NORMAL    = 0x00,
    USART_MODE_RS485     = 0x01,
    USART_MODE_HWHS      = 0x02,
    USART_MODE_MODERN    = 0x03,
    USART_MODE_IS07816_0 = 0x04,
    USART_MODE_IS07816_1 = 0x06,
    USART_MODE_IRDA      = 0x08,
};

enum usclks {
    USCLKS_MCK    = 0x00,
    USCLKS_MCKDIV = 0x01,
    USCLKS_SCK    = 0x03,
};

#define PAR_EVEN(val)       (val == 0x00)
#define PAR_ODD(val)        (val == 0x01)
#define PAR_SPACE(val)      (val == 0x02)
#define PAR_MARK(val)       (val == 0x03)
#define PAR_NONE(val)       ((val & 0x06) == 0x04)
#define PAR_MULTIDROP(val)  ((val & 0x06) == 0x06)

enum nbstop {
    NBSTOP_1   = 0x00,
    NBSTOP_1p5 = 0x01,
    NBSTOP_2   = 0x02,
};

enum chmode {
    CHMODE_NORMAL          = 0x00,
    CHMODE_ECHO            = 0x01,
    CHMODE_LOOPBACK_LOCAL  = 0x02,
    CHMODE_LOOPBACK_REMOTE = 0x03,
};

#define CSR_RXRDY        BIT(0)
#define CSR_TXRDY        BIT(1)
#define CSR_RXBRK        BIT(2)
#define CSR_ENDRX        BIT(3)
#define CSR_ENDTX        BIT(4)
#define CSR_OVRE         BIT(5)
#define CSR_FRAME        BIT(6)
#define CSR_PARE         BIT(7)
#define CSR_TIMEOUT      BIT(8)
#define CSR_TXEMPTY      BIT(9)
#define CSR_ITER        BIT(10)
#define CSR_TXBUFE      BIT(11)
#define CSR_RXBUFF      BIT(12)
#define CSR_NACK        BIT(13)
#define CSR_RIIC        BIT(16)
#define CSR_DSRIC       BIT(17)
#define CSR_DCDIC       BIT(18)
#define CSR_CTSIC       BIT(19)
#define CSR_RI          BIT(20)
#define CSR_DSR         BIT(21)
#define CSR_DCD         BIT(22)
#define CSR_CTS         BIT(23)
#define CSR_MANERR      BIT(24)

#define RHR_RXCHR       0x1ff
#define RHR_RXSYNH      BIT(15)

#define THR_TXCHR       0x1ff
#define THR_TXSYNH      BIT(15)

#define BRGR_CD(s)      (s->reg_brgr & 0xFFFF)
#define BRGR_FP(s)      ((s->reg_brgr & 0xFF0000) >> 16)


static int iox_send_chars(UsartState *s, uint8_t* data, unsigned len);


static void update_irq(UsartState *s)
{
    uint32_t csr = (s->reg_csr & 0x0f3fff) | ((s->reg_csr & BIT(24)) >> 4);

    if (s->rx_enabled) {
        csr &= ~CSR_RXRDY;
    }

    qemu_set_irq(s->irq, !!(csr & s->reg_imr));
}

static void update_baud_rate(UsartState *s)
{
    unsigned baud = 0;

    if (BRGR_CD(s)) {
        enum usclks clks = MR_USCLKS(s);
        enum usart_mode mode = MR_USART_MODE(s);

        switch (clks) {
        case USCLKS_MCK:
            baud = s->mclk;
            break;

        case USCLKS_MCKDIV:
            baud = s->mclk / MCKDIV;
            break;

        case USCLKS_SCK:
        default:
            error_report("at91.usart: SCK clock not supported");
            abort();
        }

        if (s->reg_mr & MR_SYNC) {      // synchronous mode
            if (clks != USCLKS_SCK)
                baud /= BRGR_CD(s);

            // Note: SPEC: When either the external clock SCK or the internal
            // clock divided (MCK/DIV) is selected, the value programmed in CD
            // must be even if the user has to ensure a 50:50 mark/space ratio
            // on the SCK pin. If the internal clock MCK is selected, the Baud
            // Rate Generator ensures a 50:50 duty cycle on the SCK pin, even
            // if the value programmed in CD is odd.
        } else {                        // asynchronous mode
            if (BRGR_CD(s) > 1) {
                if (BRGR_FP(s))         // fractional
                    baud = (unsigned)(baud / (BRGR_CD(s) + ((double) BRGR_FP(s) / 8.0)));
                else
                    baud /= BRGR_CD(s);
            }

            if (s->reg_mr & MR_OVER)
                baud /= 8;
            else
                baud /= 16;
        }

        if (mode == USART_MODE_IS07816_0 || mode == USART_MODE_IS07816_1) {
            if (s->reg_fidi)
                baud = baud / s->reg_fidi;
            else
                baud = 0;
        }
    }

    s->baud = baud;
}

void at91_usart_set_master_clock(UsartState *s, unsigned mclk)
{
    s->mclk = mclk;
    update_baud_rate(s);
}


static void xfer_chr_receive(UsartState *s, uint16_t chr, bool rxsynh)
{
    if ((s->reg_csr & CSR_RXRDY) && s->rx_enabled) {
        s->reg_csr |= CSR_OVRE;
    }

    // The last character is transferred into US_RHR and overwrites the previous one.
    s->reg_rhr = (chr & RHR_RXCHR) | (rxsynh ? RHR_RXSYNH : 0);
    s->reg_csr |= CSR_RXRDY;

    update_irq(s);
}

static void xfer_receiver_next(UsartState *s)
{
    if (buffer_empty(&s->rcvbuf))
        return;

    if (s->reg_csr & CSR_RXRDY)
        return;

    uint8_t chr = s->rcvbuf.buffer[0];
    buffer_advance(&s->rcvbuf, 1);

    xfer_chr_receive(s, chr, false);
}

static void xfer_receiver_dma_updreg(UsartState *s)
{
    // if first DMA buffer is full, set its flag
    if (!s->pdc.reg_rcr)
        s->reg_csr |= CSR_ENDRX;

    // if there is no second buffer, indicate all buffers full
    if (!s->pdc.reg_rcr && !s->pdc.reg_rncr)
        s->reg_csr |= CSR_RXBUFF;

    // move to next buffer if we have RNCR and RCR is zero
    if (!s->pdc.reg_rcr && s->pdc.reg_rncr) {
        s->pdc.reg_rpr = s->pdc.reg_rnpr;
        s->pdc.reg_rnpr = 0;

        s->pdc.reg_rcr = s->pdc.reg_rncr;
        s->pdc.reg_rncr = 0;
    }
}

static void xfer_receiver_dma_rcr(UsartState *s)
{
    MemTxAttrs attrs = MEMTXATTRS_UNSPECIFIED;

    uint16_t len = s->rcvbuf.offset < s->pdc.reg_rcr ? s->rcvbuf.offset : s->pdc.reg_rcr;
    uint8_t *data = s->rcvbuf.buffer;

    MemTxResult result = address_space_rw(&address_space_memory, s->pdc.reg_rpr, attrs, data, len, true);
    if (result) {
        error_report("at91.usart: failed to write memory: %d", result);
        abort();
    }

    buffer_advance(&s->rcvbuf, len);
    s->pdc.reg_rpr += len;
    s->pdc.reg_rcr -= len;
}

static void xfer_receiver_dma_rhr(UsartState *s)
{
    MemTxAttrs attrs = MEMTXATTRS_UNSPECIFIED;
    uint8_t chr = s->reg_rhr & RHR_RXCHR;

    MemTxResult result = address_space_rw(&address_space_memory, s->pdc.reg_rpr, attrs, &chr, 1, true);
    if (result) {
        error_report("at91.usart: failed to write memory: %d", result);
        abort();
    }

    s->pdc.reg_rpr += 1;
    s->pdc.reg_rcr -= 1;
    s->reg_csr &= ~CSR_RXRDY;
}

static void __xfer_receiver_dma(UsartState *s)
{
    // read from RHR
    if (s->reg_csr & CSR_RXRDY) {
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
        s->reg_csr |= CSR_ENDRX;
        s->reg_csr |= CSR_RXBUFF;
    }
}

static void xfer_receiver_dma(UsartState *s)
{
    __xfer_receiver_dma(s);
    update_irq(s);

    // DMA needs to be re-enabled if buffer is full
    if (!s->pdc.reg_rcr)
        s->rx_dma_enabled = false;

    // if both DMA buffers are full and we still have data, read to RHR
    if (!s->pdc.reg_rcr && !s->pdc.reg_rncr)
        xfer_receiver_next(s);
}

static void xfer_chr_transmit(UsartState *s, uint16_t chr, bool txsynh)
{
    if (!(s->reg_csr & CSR_TXRDY)) {
        // SPEC Writing a character in US_THR while TXRDY is low has no effect
        // and the written character is lost.
        return;
    }

    // TODO: shift register, ...
    uint8_t bchr = chr;
    iox_send_chars(s, &bchr, 1);

    s->reg_csr |= CSR_TXRDY;
    s->reg_csr |= CSR_TXEMPTY;
}


static void xfer_dma_rx_start(void *opaque)
{
    UsartState *s = opaque;

    s->rx_dma_enabled = true;
    xfer_receiver_dma(s);
}

static void xfer_dma_rx_stop(void *opaque)
{
    UsartState *s = opaque;
    s->rx_dma_enabled = false;
}

static int xfer_dma_tx_do_tcr(UsartState *s)
{
    uint8_t *data = g_new0(uint8_t, s->pdc.reg_tcr);
    if (!data)
        return -ENOMEM;

    MemTxResult result = address_space_rw(&address_space_memory, s->pdc.reg_tpr,
                                          MEMTXATTRS_UNSPECIFIED, data, s->pdc.reg_tcr, false);
    if (result) {
        g_free(data);
        error_report("at91.usart: failed to read memory: %d", result);
        return -EIO;
    }

    int status = iox_send_chars(s, data, s->pdc.reg_tcr);
    g_free(data);

    s->pdc.reg_tpr += s->pdc.reg_tcr;
    s->pdc.reg_tcr = 0;

    return status;
}

static void xfer_dma_tx_start(void *opaque)
{
    UsartState *s = opaque;

    if (s->pdc.reg_tcr) {
        int status = xfer_dma_tx_do_tcr(s);
        if (status) {
            error_report("at91.usart: dma transfer failed");
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
            error_report("at91.usart: dma transfer failed");
            abort();
        }
    }

    s->reg_csr |= CSR_ENDTX | CSR_TXBUFE;
    update_irq(s);
}

static void xfer_dma_tx_stop(void *opaque)
{
    /* no-op */
}


static int iox_receive_data(UsartState *s, struct iox_data_frame *frame)
{
    bool in_progress = !buffer_empty(&s->rcvbuf);

    if (!s->rx_enabled)
        return iox_send_u32_resp(s->server, frame, ENXIO);

    buffer_reserve(&s->rcvbuf, frame->len);
    buffer_append(&s->rcvbuf, frame->payload, frame->len);
    int status = iox_send_u32_resp(s->server, frame, 0);
    if (status)
        return status;

    if (in_progress)
        return 0;

    if (s->rx_dma_enabled)
        xfer_receiver_dma(s);
    else
        xfer_receiver_next(s);

    return 0;
}

static void iox_receive(struct iox_data_frame *frame, void *opaque)
{
    UsartState *s = opaque;
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
            s->reg_csr |= CSR_OVRE;
            update_irq(s);
            break;

        case IOX_CID_FAULT_FRAME:
            s->reg_csr |= CSR_FRAME;
            update_irq(s);
            break;

        case IOX_CID_FAULT_PARE:
            s->reg_csr |= CSR_PARE;
            update_irq(s);
            break;

        case IOX_CID_FAULT_TIMEOUT:
            s->reg_csr |= CSR_TIMEOUT;
            update_irq(s);
            break;
        }
        break;
    }

    if (status) {
        error_report("error handling command frame: cat: %d, id: %d", frame->cat, frame->id);
        abort();
    }
}

static int iox_send_chars(UsartState *s, uint8_t* data, unsigned len)
{
    if (!s->server)
        return 0;

    return iox_send_data_multiframe_new(s->server, IOX_CAT_DATA, IOX_CID_DATA_OUT, len, data);
}


static uint64_t usart_mmio_read(void *opaque, hwaddr offset, unsigned size)
{
    UsartState *s = opaque;

    switch (offset) {
    case US_MR:
        return s->reg_mr;

    case US_IMR:
        return s->reg_imr;

    case US_CSR: {
        uint32_t tmp = s->reg_csr;

        // RXRDY is only active when the receiver is enabled, but data can also
        // be received when it is not active. RXRDY should then become active
        // when the receiver is enabled.
        if (!s->rx_enabled) {
            tmp &= ~CSR_RXRDY;
        }

        s->reg_csr &= ~(CSR_RIIC | CSR_DSRIC | CSR_DCDIC | CSR_CTSIC);
        return tmp;
    }

    case US_RHR: {
        s->reg_csr &= ~CSR_RXRDY;
        xfer_receiver_next(s);
        update_irq(s);
        return s->reg_rhr;
    }

    case US_BRGR:
        return s->reg_brgr;

    case US_RTOR:
        return s->reg_rtor;

    case US_TTGR:
        return s->reg_ttgr;

    case US_FIDI:
        return s->reg_fidi;

    case US_NER:
        return s->reg_ner;

    case US_IF:
        return s->reg_if;

    case US_MAN:
        return s->reg_man;

    case PDC_START...PDC_END:
        return at91_pdc_get_register(&s->pdc, offset);

    default:
        error_report("at91.usart: illegal read access at 0x%03lx", offset);
        abort();
    }
}

static void usart_mmio_write(void *opaque, hwaddr offset, uint64_t value, unsigned size)
{
    UsartState *s = opaque;

    switch (offset) {
    case US_CR:
        if (value & CR_RSTRX) {
            s->rx_enabled = false;
            s->reg_csr &= ~(CSR_PARE | CSR_FRAME | CSR_OVRE | CSR_MANERR);
            s->reg_csr &= ~(CSR_RXBRK | CSR_TIMEOUT | CSR_ENDRX | CSR_RXBUFF | CSR_NACK);

            // SPEC: The software resets clear the status flag and reset
            // internal state machines but the user interface configuration
            // registers hold the value configured prior to software reset.
            // Regardless of what the receiver or the trans- mitter is
            // performing, the communication is immediately stopped.

            // Note: Do not clear RXRDY, this is masked separately.
            update_irq(s);
        }
        if (value & CR_RSTTX) {
            s->tx_enabled = false;
            s->reg_csr &= ~(CSR_TXRDY | CSR_TXEMPTY | CSR_ENDTX | CSR_TXBUFE);

            // SPEC: The software resets clear the status flag and reset
            // internal state machines but the user interface configuration
            // registers hold the value configured prior to software reset.
            // Regardless of what the receiver or the trans- mitter is
            // performing, the communication is immediately stopped.
        }
        if (value & CR_RXEN) {
            s->rx_enabled = true;

            // SPEC: If characters were being received when the receiver was
            // disabled, RXRDY changes to 1 when the receiver is enabled.

            update_irq(s);
        }
        if (value & CR_RXDIS) {     // takes precedence over RXEN
            s->rx_enabled = false;

            // Note: Do not clear RXRDY, this is masked separately.
            update_irq(s);
        }
        if (value & CR_TXEN) {
            s->tx_enabled = true;
            s->reg_csr |= CSR_TXRDY | CSR_TXEMPTY;
        }
        if (value & CR_TXDIS) {     // takes precedence over TXEN
            s->tx_enabled = false;
            s->reg_csr &= ~(CSR_TXRDY | CSR_TXEMPTY);
        }
        if (value & CR_RSTSTA) {
            s->reg_csr &= ~(CSR_PARE | CSR_FRAME | CSR_OVRE | CSR_MANERR | CSR_RXBRK);
            update_irq(s);
        }
        if (value & CR_STTBRK) {
            // TODO: CR_STTBRK
            // SPEC: Starts transmission of a break after the characters
            // present in US_THR and the Transmit Shift Register have been
            // transmitted. No effect if a break is already being transmitted.
            warn_report("at91.usart US_CR.STTBRK: not supported yet");
        }
        if (value & CR_STPBRK) {
            // TODO: CR_STPBRK
            // SPEC: Stops transmission of the break after a minimum of one
            // character length and transmits a high level during 12-bit
            // periods. No effect if no break is being transmitted.
            warn_report("at91.usart US_CR.STPBRK: not supported yet");
        }
        if (value & CR_STTTO) {
            s->reg_csr &= ~CSR_TIMEOUT;

            // NOTE: Not implemented in emulation, use fault-injection for timeout.
            // Starts waiting for a character before clocking the time-out
            // counter. Resets the status bit TIMEOUT in US_CSR.
        }
        if (value & CR_SENDA) {
            // TODO: CR_SENDA
            // SPEC: In Multidrop Mode only, the next character written to the
            // US_THR is sent with the address bit set.
            warn_report("at91.usart US_CR.SENDA: not supported yet");
        }
        if (value & CR_RSTIT) {
            enum usart_mode mode = MR_USART_MODE(s);

            if (mode == USART_MODE_IS07816_0 || mode == USART_MODE_IS07816_1) {
                s->reg_csr &= ~CSR_ITER;
                update_irq(s);
            }
        }
        if (value & CR_RSTNACK) {
            s->reg_csr &= ~CSR_NACK;
            update_irq(s);
        }
        if (value & CR_RETTO) {
            // SPEC: Restart Time-out.
            // NOTE: Use fault injection for emulation.
        }
        if (value & CR_DTREN) {
            // TODO: CR_DTREN
            // SPEC: Drives the pin DTR at 0.
            warn_report("at91.usart US_CR.DTREN: not supported yet");
        }
        if (value & CR_DTRDIS) {
            // TODO: CR_DTRDIS
            // SPEC: Drives the pin DTR to 1.
            warn_report("at91.usart US_CR.DTRDIS: not supported yet");
        }
        if (value & CR_RTSEN) {
            // TODO: CR_RTSEN
            // SPEC: Drives the pin RTS to 0.
            warn_report("at91.usart US_CR.RTSEN: not supported yet");
        }
        if (value & CR_RTSDIS) {
            // TODO: CR_RTSDIS
            // SPEC: Drives the pin RTS to 1.
            warn_report("at91.usart US_CR.RTSDIS: not supported yet");
        }

        update_irq(s);
        break;

    case US_MR:
        s->reg_mr = value;
        update_baud_rate(s);
        break;

    case US_IER:
        s->reg_imr |= value;
        update_irq(s);
        break;

    case US_IDR:
        s->reg_imr &= ~value;
        update_irq(s);
        break;

    case US_THR:
        xfer_chr_transmit(s, value & THR_TXCHR, value & THR_TXSYNH);
        update_irq(s);
        break;

    case US_BRGR:
        s->reg_brgr = value;
        update_baud_rate(s);
        break;

    case US_RTOR:
        s->reg_rtor = value;

        // NOTE: Use fault injection for emulation.
        // The code below is mainly intended for documentation.

        if (s->reg_rtor) {
            // SPEC: enable/start timeout
        } else {
            s->reg_csr &= ~CSR_TIMEOUT;

            // SPEC: disable/stop timeout

            update_irq(s);
        }

        break;

    case US_TTGR:
        s->reg_ttgr = value;
        // NOTE: Not supported in emulation
        break;

    case US_FIDI:
        s->reg_fidi = value;
        update_baud_rate(s);
        break;

    case US_IF:
        s->reg_if = value;
        // TODO: US_IF
        warn_report("at91.usart US_IF: not supported yet [value: 0x%lx]", value);
        break;

    case US_MAN:
        s->reg_man = value;
        // TODO: US_MAN
        warn_report("at91.usart US_MAN: not supported yet [value: 0x%lx]", value);
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
                .flag_endrx   = CSR_ENDRX,
                .flag_endtx   = CSR_ENDTX,
                .flag_rxbuff  = CSR_RXBUFF,
                .flag_txbufe  = CSR_TXBUFE,
                .reg_sr       = &s->reg_csr,
            };

            at91_pdc_generic_set_register(&s->pdc, &ops, offset, value);
            update_irq(s);
        }
        break;

    default:
        error_report("at91.usart: illegal write access at "
                      "0x%03lx [value: 0x%08lx]", offset, value);
        abort();
    }
}

static const MemoryRegionOps usart_mmio_ops = {
    .read = usart_mmio_read,
    .write = usart_mmio_write,
    .impl.min_access_size = 4,
    .impl.max_access_size = 4,
    .valid.min_access_size = 4,
    .valid.max_access_size = 4,
    .endianness = DEVICE_NATIVE_ENDIAN,
};


static void usart_device_init(Object *obj)
{
    SysBusDevice *sbd = SYS_BUS_DEVICE(obj);
    UsartState *s = AT91_USART(obj);

    sysbus_init_irq(sbd, &s->irq);

    memory_region_init_io(&s->mmio, OBJECT(s), &usart_mmio_ops, s, "at91.usart", 0x4000);
    sysbus_init_mmio(sbd, &s->mmio);
}

static void usart_reset_registers(UsartState *s)
{
    s->rx_enabled = false;
    s->tx_enabled = false;

    s->reg_imr  = 0x00;
    s->reg_rhr  = 0x00;
    s->reg_brgr = 0x00;
    s->reg_rtor = 0x00;
    s->reg_ttgr = 0x00;
    s->reg_fidi = 0x174;
    s->reg_if   = 0x00;
    s->reg_man  = 0x30011004;

    at91_pdc_reset_registers(&s->pdc);
}

static void usart_device_realize(DeviceState *dev, Error **errp)
{
    UsartState *s = AT91_USART(dev);

    usart_reset_registers(s);

    buffer_init(&s->rcvbuf, "at91.usart.rcvbuf");
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
        info_report("at91.usart: listening on %s", s->socket);
    }
}

static void usart_device_unrealize(DeviceState *dev, Error **errp)
{
    UsartState *s = AT91_USART(dev);

    if (s->server) {
        iox_server_free(s->server);
        s->server = NULL;
    }

    buffer_free(&s->rcvbuf);
}

static void usart_device_reset(DeviceState *dev)
{
    UsartState *s = AT91_USART(dev);

    usart_reset_registers(s);
    buffer_reset(&s->rcvbuf);
}

static Property usart_device_properties[] = {
    DEFINE_PROP_STRING("socket", UsartState, socket),
    DEFINE_PROP_END_OF_LIST(),
};

static void usart_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    dc->realize = usart_device_realize;
    dc->unrealize = usart_device_unrealize;
    dc->reset = usart_device_reset;
    device_class_set_props(dc, usart_device_properties);
}

static const TypeInfo usart_device_info = {
    .name = TYPE_AT91_USART,
    .parent = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(UsartState),
    .instance_init = usart_device_init,
    .class_init = usart_class_init,
};

static void usart_register_types(void)
{
    type_register_static(&usart_device_info);
}

type_init(usart_register_types)
