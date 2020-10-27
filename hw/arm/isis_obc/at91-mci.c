/*
 * AT91 Multimedia Card Interface.
 *
 * See at91-mci.h for details.
 *
 * Copyright (c) 2019-2020 KSat e.V. Stuttgart
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or, at your
 * option, any later version. See the COPYING file in the top-level directory.
 */

// Notes:
// - Commands (CMDR register):
//   - MAXLAT field is ignored. in QEMU, commands are instantaneous, so
//     timeout latency impossible to emulate.
//   - OPDCMD field is ignored. Hardeare not emulated with this leve of
//     detail.
// - Write to TDR register only allowed when transaction is in progress:
//   This could cause problems if a program would want to "pre-load" the TDR
//   register before sending a write command... According to the write functional
//   flow diagram of the spec, writes to TDR should however only happen after
//   successfully sending a write command and checking its status. If something
//   like this happens the emulator will abort.
// - No failure injection possible due to QEMU SD-Card interface. This affects
//   the flags: RINDE, RDIRE, RCRCE, RENDE, RTOE, DCRCE, DTOE, OVRE, UNRE.
// - SDIO interrupts are not supported. This affects SDIOIRQA, SDIOIRQB.
// - MMC stream transfer not supported
// - special commands (SPCMD, IOSPCMD) might not be fully supported
// - RDPROOF/WRPROOF not supported due to HW not being emulated with that kind
//   of detail

#include "at91-mci.h"
#include "exec/address-spaces.h"
#include "qapi/error.h"
#include "qemu/error-report.h"
#include "sysemu/blockdev.h"
#include "hw/irq.h"
#include "hw/qdev-properties.h"

#define MCI_CR          0x00
#define MCI_MR          0x04
#define MCI_DTOR        0x08
#define MCI_SDCR        0x0C
#define MCI_ARGR        0x10
#define MCI_CMDR        0x14
#define MCI_BLKR        0x18
#define MCI_RSPR0       0x20
#define MCI_RSPR1       0x24
#define MCI_RSPR2       0x28
#define MCI_RSPR3       0x2C
#define MCI_RDR         0x30
#define MCI_TDR         0x34
#define MCI_SR          0x40
#define MCI_IER         0x44
#define MCI_IDR         0x48
#define MCI_IMR         0x4C

#define CR_MCIEN        BIT(0)
#define CR_MCIDIS       BIT(1)
#define CR_PWSEN        BIT(2)
#define CR_PWSDIS       BIT(3)
#define CR_SWRST        BIT(7)

#define MR_CLKDIV(s)    ((s)->reg_mr & 0xFF)
#define MR_PWSDIV(s)    (((s)->reg_mr >> 8) & 0x07)
#define MR_BLKLEN(s)    (((s)->reg_mr >> 16) & 0xFFFF)
#define MR_RDPROOF      BIT(11)
#define MR_WRPROOF      BIT(12)
#define MR_PDCFBYTE     BIT(13)
#define MR_PDCPADV      BIT(14)
#define MR_PDCMODE      BIT(15)

#define DTOR_DTOCYC(s)  ((s)->reg_dtor & 0x0F)
#define DTOR_DTOMUL(s)  (((s)->reg_dtor >> 4) & 0x07)

#define SDCR_SDCSEL(s)  ((s)->reg_sdcr & 0x03)
#define SDCR_SDCBUS     BIT(7)

#define CMDR_CMDNB(v)   ((v) & 0x3F)
#define CMDR_RSPTYP(v)  (((v) >> 6) & 0x03)
#define CMDR_SPCMD(v)   (((v) >> 9) & 0x07)
#define CMDR_TRCMD(v)   (((v) >> 16) & 0x03)
#define CMDR_TRTYP(v)   (((v) >> 19) & 0x07)
#define CMDR_IOSPCMD(v) (((v) >> 24) & 0x03)
#define CMDR_OPDCMD     BIT(11)
#define CMDR_MAXLAT     BIT(12)
#define CMDR_TRDIR      BIT(18)

enum cmdr_rsptyp {
    CMDR_RSPTYP_NORSP = 0,
    CMDR_RSPTYP_48bit = 1,
    CMDR_RSPTYP_136bit = 2,
};

enum cmdr_trcmd {
    CMDR_TRCMD_NONE  = 0,
    CMDR_TRCMD_START = 1,
    CMDR_TRCMD_STOP  = 2,
};

enum cmdr_trtyp {
    CMDR_TRTYP_MMCSD_SINGLE_BLOCK   = 0,
    CMDR_TRTYP_MMCSD_MULTIPLE_BLOCK = 1,
    CMDR_TRTYP_MMC_STREAM           = 2,
    CMDR_TRTYP_SDIO_BYTE            = 4,
    CMDR_TRTYP_SDIO_BLOCK           = 5,
};

enum cmdr_spcmd {
    CMDR_SPCMD_NONE     = 0,
    CMDR_SPCMD_INIT     = 1,
    CMDR_SPCMD_SYNC     = 2,
    CMDR_SPCMD_INT_CMD  = 4,
    CMDR_SPCMD_INT_RESP = 5,
};

enum cmdr_iospcmd {
    CMDR_IOSPCMD_NONE    = 0,
    CMDR_IOSPCMD_SUSPEND = 1,
    CMDR_IOSPCMD_RESUME  = 2,
};

#define BLKR_BCNT(s)    ((s)->reg_blkr & 0xFFFF)
#define BLKR_BLKLEN(s)  (((s)->reg_blkr >> 16) & 0xFFFF)

#define SR_CMDRDY       BIT(0)
#define SR_RXRDY        BIT(1)
#define SR_TXRDY        BIT(2)
#define SR_BLKE         BIT(3)
#define SR_DTIP         BIT(4)
#define SR_NOTBUSY      BIT(5)
#define SR_ENDRX        BIT(6)
#define SR_ENDTX        BIT(7)
#define SR_SDIOIRQA     BIT(8)
#define SR_SDIOIRQB     BIT(9)
#define SR_RXBUFF       BIT(14)
#define SR_TXBUFE       BIT(15)
#define SR_RINDE        BIT(16)
#define SR_RDIRE        BIT(17)
#define SR_RCRCE        BIT(18)
#define SR_RENDE        BIT(19)
#define SR_RTOE         BIT(20)
#define SR_DCRCE        BIT(21)
#define SR_DTOE         BIT(22)
#define SR_OVRE         BIT(30)
#define SR_UNRE         BIT(31)

#define BLKLEN_MULTIBLOCK_UNLIMITED   ((size_t)-1)


static void mci_reset_registers(MciState *s);

static void mci_irq_update(MciState *s)
{
    qemu_set_irq(s->irq, !!(s->reg_sr & s->reg_imr));
}

static void mci_update_mcck(MciState *s)
{
    s->mcck = s->mclk / (2 * (MR_CLKDIV(s) + 1));
}

void at91_mci_set_master_clock(MciState *s, unsigned mclk)
{
    s->mclk = mclk;
    mci_update_mcck(s);
}

static inline SDBus *mci_get_selected_sdcard(MciState *s)
{
    return s->selected_card == 0 ? &s->sdbus0 : &s->sdbus1;
}


static void mci_pdc_do_read_rcr(MciState *s)
{
    SDBus *sd = mci_get_selected_sdcard(s);

    size_t len = s->pdc.reg_rcr;
    if (!(s->reg_mr & MR_PDCFBYTE))
        len *= 4;

    if (len > s->rd_bytes_left)
        len = s->rd_bytes_left;

    uint8_t *data = g_new0(uint8_t, len);

    // read from SD card to buffer
    if (!sdbus_data_ready(sd)) {
        error_report("at91.mci: sd card has no data available for read");
        abort();
    }

    for (size_t i = 0; i < len; i++) {
        data[i] = sdbus_read_data(sd);
    }

    // copy buffer to DMA memory
    MemTxResult result = address_space_rw(&address_space_memory, s->pdc.reg_rpr,
                                          MEMTXATTRS_UNSPECIFIED, data, len, true);
    if (result) {
        error_report("at91.mci: failed to write memory: %d", result);
        abort();
    }

    g_free(data);

    s->pdc.reg_rpr += len;
    s->pdc.reg_rcr -= (s->reg_mr & MR_PDCFBYTE) ? len : len / 4;

    if (s->rd_bytes_left != BLKLEN_MULTIBLOCK_UNLIMITED)
        s->rd_bytes_left -= len;
}

static void mci_pdc_do_read(MciState *s)
{
    if (s->pdc.reg_rcr)
        mci_pdc_do_read_rcr(s);

    if (s->pdc.reg_rcr == 0)
        s->reg_sr |= SR_ENDRX;

    if (s->pdc.reg_rcr == 0 && s->pdc.reg_rncr != 0) {
        s->pdc.reg_rcr = s->pdc.reg_rncr;
        s->pdc.reg_rncr = 0;

        s->pdc.reg_rpr = s->pdc.reg_rnpr;
        s->pdc.reg_rnpr = 0;

        if (s->rd_bytes_left)
            mci_pdc_do_read_rcr(s);
    }

    if (s->rd_bytes_left == 0) {
        s->reg_sr &= ~(SR_DTIP | SR_RXRDY);
    }

    if (s->pdc.reg_rcr == 0 && s->pdc.reg_rncr == 0) {
        s->reg_sr |= SR_RXBUFF;
        s->rx_dma_enabled = false;

        if (s->rd_bytes_left)
            s->reg_sr |= SR_RXRDY;
    }
}

static void mci_pdc_do_write_tcr(MciState *s)
{
    SDBus *sd = mci_get_selected_sdcard(s);

    size_t len = s->pdc.reg_tcr;
    if (!(s->reg_mr & MR_PDCFBYTE))
        len *= 4;

    if (len > s->wr_bytes_left)
        len = s->wr_bytes_left;

    uint8_t *data = g_new0(uint8_t, len);

    // read DMA memory into buffer
    MemTxResult result = address_space_rw(&address_space_memory, s->pdc.reg_tpr,
                                          MEMTXATTRS_UNSPECIFIED, data, len, false);
    if (result) {
        error_report("at91.mci: failed to read memory: %d", result);
        abort();
    }

    // write buffer to SD card
    for (size_t i = 0; i < len; i++) {
        sdbus_write_data(sd, data[i]);
    }

    g_free(data);

    s->pdc.reg_tpr += len;
    s->pdc.reg_tcr -= (s->reg_mr & MR_PDCFBYTE) ? len : len / 4;

    if (s->wr_bytes_left != BLKLEN_MULTIBLOCK_UNLIMITED)
        s->wr_bytes_left -= len;

    s->wr_bytes_blk = (s->wr_bytes_blk + len) % BLKR_BLKLEN(s);
}

static void mci_pdc_do_write(MciState *s)
{
    if (s->pdc.reg_tcr)
        mci_pdc_do_write_tcr(s);

    if (s->pdc.reg_tcr == 0)
        s->reg_sr |= SR_ENDTX;

    if (s->pdc.reg_tcr == 0 && s->pdc.reg_tncr != 0) {
        s->pdc.reg_tcr = s->pdc.reg_tncr;
        s->pdc.reg_tncr = 0;

        s->pdc.reg_tpr = s->pdc.reg_tnpr;
        s->pdc.reg_tnpr = 0;

        if (s->wr_bytes_left)
            mci_pdc_do_write_tcr(s);
    }

    if (s->wr_bytes_left == 0) {
        // Note: In PDC mode, BLKE is set for the last block transferred.
        s->reg_sr |= SR_NOTBUSY | SR_BLKE;
        s->reg_sr &= ~(SR_DTIP | SR_TXRDY);
    }

    if (s->pdc.reg_tcr == 0 && s->pdc.reg_tncr == 0) {
        s->reg_sr |= SR_TXBUFE;
        s->tx_dma_enabled = false;

        // for unlimited block transfer: make sure that the last block sent is marked as such
        if (s->wr_bytes_left == BLKLEN_MULTIBLOCK_UNLIMITED && s->wr_bytes_blk == 0)
            s->reg_sr |= SR_BLKE;

        if (s->wr_bytes_left)
            s->reg_sr |= SR_TXRDY;
    }
}


static size_t mci_tr_length(MciState *s, uint32_t cmdr)
{
    switch (CMDR_TRTYP(cmdr)) {
    case CMDR_TRTYP_MMCSD_SINGLE_BLOCK:
        return BLKR_BLKLEN(s);

    case CMDR_TRTYP_MMCSD_MULTIPLE_BLOCK:
        if (BLKR_BCNT(s) == 0)          // infinite block transfer
            return BLKLEN_MULTIBLOCK_UNLIMITED;
        else                            // finite block transfer
            return ((size_t)BLKR_BLKLEN(s)) * ((size_t)BLKR_BCNT(s));

    case CMDR_TRTYP_SDIO_BYTE:
        return BLKR_BCNT(s);

    case CMDR_TRTYP_SDIO_BLOCK:
        return ((size_t)BLKR_BLKLEN(s)) * ((size_t)BLKR_BCNT(s));

    case CMDR_TRTYP_MMC_STREAM:
        error_report("at91.mci: MMC stream data transfer not supported");
        abort();

    default:
        error_report("at91.mci: invalid transfer type: %d", CMDR_TRTYP(cmdr));
        abort();
    }
}

static void mci_tr_start_read(MciState *s, uint32_t cmdr)
{
    s->rd_bytes_left = mci_tr_length(s, cmdr);

    if ((s->reg_mr & MR_PDCMODE) && s->rx_dma_enabled)
        mci_pdc_do_read(s);
    else if (!(s->reg_mr & MR_PDCMODE))
        s->reg_sr |= SR_RXRDY;
}

static void mci_tr_start_write(MciState *s, uint32_t cmdr)
{
    s->wr_bytes_left = mci_tr_length(s, cmdr);
    s->wr_bytes_blk = 0;
    s->reg_sr &= ~SR_NOTBUSY;

    if ((s->reg_mr & MR_PDCMODE) && s->tx_dma_enabled)
        mci_pdc_do_write(s);
    else if (!(s->reg_mr & MR_PDCMODE))
        s->reg_sr |= SR_TXRDY;
}

static void mci_tr_start(MciState *s, uint32_t cmdr)
{
    if (CMDR_TRDIR & cmdr)
        mci_tr_start_read(s, cmdr);
    else
        mci_tr_start_write(s, cmdr);
}

static void mci_tr_stop(MciState *s, uint32_t cmdr)
{
    // Note: Stop transmission command does not have a direction.
    //   Due to this, we also set NOTBUSY. According to spec, this flags must be
    //   used only for write transactions, so we assume that setting them for
    //   read transactions doesn't break anything as depending on them in any way
    //   is considered illegal behavior.

    s->rd_bytes_left = 0;
    s->wr_bytes_left = 0;
    s->wr_bytes_blk = 0;
    s->reg_sr &= ~(SR_DTIP | SR_RXRDY | SR_TXRDY);
    s->reg_sr |= SR_NOTBUSY;
}


static void mci_do_command(MciState *s, uint32_t cmdr)
{
    SDBus *bus = mci_get_selected_sdcard(s);
    SDRequest request;
    uint8_t response[16];
    int rlen_expected;
    int rlen;

    // clear flag for documentation, even though commands are instant in emulation
    s->reg_sr &= ~SR_CMDRDY;

    if (CMDR_RSPTYP(cmdr) == CMDR_RSPTYP_NORSP) {
        rlen_expected = 0;
    } else if (CMDR_RSPTYP(cmdr) == CMDR_RSPTYP_48bit) {
        rlen_expected = 4;
    } else if (CMDR_RSPTYP(cmdr) == CMDR_RSPTYP_136bit) {
        rlen_expected = 16;
    } else {
        error_report("at91.mci: invalid command RSPTYP: 0x%x", CMDR_RSPTYP(cmdr));
        abort();
    }

    request.cmd = CMDR_CMDNB(cmdr);
    request.arg = s->reg_argr;
    request.crc = 0;    // FIXME: not implemented in QEMU core, ignored for now, fix in future

    rlen = sdbus_do_command(bus, &request, response);

    if (rlen < 0) {
        warn_report("at91.mci: sdbus_do_command failed with error: %d", rlen);
        s->reg_sr |= SR_CMDRDY | SR_RTOE;

        mci_irq_update(s);
        return;
    }

    // rlen == 0 might mean illegal command (card disconnected, ...)
    if (rlen && rlen != rlen_expected) {
        error_report("at91.mci: command response length does not match expected length");
        error_report("          cmdr: 0x%x, got: %d, expected: %d", cmdr, rlen, rlen_expected);
        abort();
    }
    if (!rlen && rlen != rlen_expected) {
        s->reg_sr |= SR_RTOE;
    }

    s->reg_rspr_index = 0;
    if (rlen == 0) {
        s->reg_rspr[0] = 0;
        s->reg_rspr[1] = 0;
        s->reg_rspr[2] = 0;
        s->reg_rspr[3] = 0;
        s->reg_rspr_len = 0;
    } else if (rlen == 4) {
        s->reg_rspr[0] = ldl_be_p(&response[0]);
        s->reg_rspr[1] = 0;
        s->reg_rspr[2] = 0;
        s->reg_rspr[3] = 0;
        s->reg_rspr_len = 1;
    } else if (rlen == 16) {
        s->reg_rspr[0] = ldl_be_p(&response[12]);
        s->reg_rspr[1] = ldl_be_p(&response[8]);
        s->reg_rspr[2] = ldl_be_p(&response[4]);
        s->reg_rspr[3] = ldl_be_p(&response[0]);
        s->reg_rspr_len = 4;
    }

    if (CMDR_TRCMD(cmdr) != CMDR_TRCMD_NONE) {
        s->reg_sr &= ~(SR_OVRE | SR_UNRE);
        s->reg_sr |= SR_DTIP;

        if ((s->reg_mr & MR_PDCMODE) && !(s->reg_mr & MR_PDCFBYTE) && (BLKR_BLKLEN(s) & 0x03) != 0) {
            error_report("at91.mci: block length must be multiple of 4 bytes unless PDCFBYTE is set");
            abort();
        }

        if (CMDR_TRCMD(cmdr) == CMDR_TRCMD_START) {
            mci_tr_start(s, cmdr);
        } else if (CMDR_TRCMD(cmdr) == CMDR_TRCMD_STOP) {
            mci_tr_stop(s, cmdr);
        } else {
            error_report("at91.mci: invalid value for TRCMD field");
            abort();
        }

        mci_irq_update(s);
    }

    if (CMDR_SPCMD(cmdr) != CMDR_SPCMD_NONE) {
        // TODO: handle special commands
        warn_report("special commands not implemented yet (cmdr: 0x%x)", cmdr);
    }

    if (CMDR_IOSPCMD(cmdr) != CMDR_IOSPCMD_NONE) {
        // TODO: handle SDIO special commands
        warn_report("SDIO special commands not implemented yet (cmdr: 0x%x)", cmdr);
    }

    s->reg_sr |= SR_CMDRDY;
    mci_irq_update(s);
}


static uint32_t mci_rdr(MciState *s)
{
    SDBus *sd = mci_get_selected_sdcard(s);

    if (s->rd_bytes_left == 0) {
        error_report("at91.mci: access to RDR register without active read transmission");
        abort();
    }

    if (s->reg_mr & MR_PDCMODE) {
        error_report("at91.mci: access to RDR register while PDCMODE is set");
        abort();
    }

    if (!sdbus_data_ready(sd)) {
        error_report("at91.mci: sd card has no data available for read");
        abort();
    }

    if (!(s->reg_sr & SR_RXRDY)) {
        error_report("at91.mci: access to RDR while TXRDY not set");
        abort();
    }

    s->reg_sr &= ~SR_RXRDY;

    size_t len = s->rd_bytes_left <= 4 ? s->rd_bytes_left : 4;
    uint32_t buf = 0;

    // Note: The spec does not clarify endianess/order, only that "words" are
    // read, so assume consecutive bytes.
    for (int i = 0; i < len; i++) {
        ((uint8_t *)&buf)[i] = sdbus_read_data(sd);
    }
    s->rd_bytes_left -= len;

    if (s->rd_bytes_left == 0) {
        s->reg_sr &= ~SR_DTIP;
    } else {
        s->reg_sr |= SR_RXRDY;      // instantly ready to read next datum, if available
    }

    // Note:
    //   According to spec, BLKE must be only used on writes, thus we don't set
    //   it here.

    mci_irq_update(s);
    return buf;
}

static void mci_tdr(MciState *s, uint32_t data)
{
    SDBus *sd = mci_get_selected_sdcard(s);

    if (s->wr_bytes_left == 0) {
        error_report("at91.mci: access to TDR register without active write transmission");
        // See top comments and comments on TXRDY below if this abort ever
        // becomes an issue...
        abort();
    }

    if (s->reg_mr & MR_PDCMODE) {
        error_report("at91.mci: access to TDR register while PDCMODE is set");
        abort();
    }

    if (!(s->reg_sr & SR_TXRDY)) {
        error_report("at91.mci: access to TDR while TXRDY not set");
        abort();
    }

    s->reg_sr &= ~SR_TXRDY;

    size_t len = s->wr_bytes_left <= 4 ? s->wr_bytes_left : 4;

    // Note: The spec does not clarify endianess/order, only that "words" are
    // written, so assume consecutive bytes.
    for (int i = 0; i < len; i++) {
        sdbus_write_data(sd, ((uint8_t *)&data)[i]);
    }
    s->wr_bytes_left -= len;
    s->wr_bytes_blk += len;

    // On writes, check for full block transfers and set BLKE accordingly.
    if (s->wr_bytes_blk >= BLKR_BLKLEN(s)) {
        s->wr_bytes_blk -= BLKR_BLKLEN(s);
        s->reg_sr |= SR_BLKE;
    }

    if (s->wr_bytes_left == 0) {
        s->reg_sr |= SR_NOTBUSY | SR_BLKE;
        s->reg_sr &= ~SR_DTIP;
        s->wr_bytes_blk = 0;
    }

    // Note: We deliberately set TXRDY even if no more bytes are left to write.
    //   It seems that the behavior of the AT91 would be to put the data to be
    //   written on hold until writes can occur. For now, this functionality is
    //   not implemented and data is written directy. Thus set TXRDY instantly
    //   and abort on access outside transaction.

    s->reg_sr |= SR_TXRDY;      // instantly ready to write next datum
    mci_irq_update(s);
}


static void mci_dma_rx_start(void *opaque)
{
    MciState *s = opaque;
    s->rx_dma_enabled = true;

    if (s->rd_bytes_left) {
        if (!(s->reg_mr & MR_PDCMODE)) {
            error_report("at91.mci: attempting PDC read transfer without PDCMODE set");
            abort();
        }

        mci_pdc_do_read(s);
    }
}

static void mci_dma_rx_stop(void *opaque)
{
    MciState *s = opaque;
    s->rx_dma_enabled = false;
}

static void mci_dma_tx_start(void *opaque)
{
    MciState *s = opaque;
    s->tx_dma_enabled = true;

    if (s->wr_bytes_left) {
        if (!(s->reg_mr & MR_PDCMODE)) {
            error_report("at91.mci: attempting PDC write transfer without PDCMODE set");
            abort();
        }

        mci_pdc_do_write(s);
    }
}

static void mci_dma_tx_stop(void *opaque)
{
    MciState *s = opaque;
    s->tx_dma_enabled = false;
}


static void card_select_irq_handle(void *opaque, int n, int level)
{
    MciState *s = opaque;
    uint8_t card = !level;

    s->selected_card = card;
}


static uint64_t mci_mmio_read(void *opaque, hwaddr offset, unsigned size)
{
    MciState *s = opaque;

    switch (offset)  {
    case MCI_MR:
        return s->reg_mr;

    case MCI_DTOR:
        return s->reg_dtor;

    case MCI_SDCR:
        return s->reg_sdcr;

    case MCI_ARGR:
        return s->reg_argr;

    case MCI_BLKR:
        return s->reg_blkr;

    // Note: According to spec, access to response registers can be done either
    // by consecutively accessing the registers (0 to 3) or accessing the same
    // register (up to) 4 times. As we can't detect which access pattern is
    // being used, let's use an index incremented on each access. This is
    // confirmed to work on the SD test task.
    case MCI_RSPR0:
    case MCI_RSPR1:
    case MCI_RSPR2:
    case MCI_RSPR3:
        if (s->reg_rspr_index < s->reg_rspr_len) {
            return s->reg_rspr[s->reg_rspr_index++];
        } else {
            error_report("at91.mci: invalid access to RSPR[0-3]");
            error_report("          response of length %d but accessed %d times",
                         s->reg_rspr_len, s->reg_rspr_index);
            abort();
        }

    case MCI_RDR:
        return mci_rdr(s);

    case MCI_SR:
        {
            uint32_t sr = s->reg_sr;
            s->reg_sr &= ~(SR_BLKE | SR_DCRCE | SR_DTOE | SR_SDIOIRQA | SR_SDIOIRQB);
            mci_irq_update(s);
            return sr;
        }

    case MCI_IMR:
        return s->reg_imr;

    case PDC_START...PDC_END:
        return at91_pdc_get_register(&s->pdc, offset);

    default:
        error_report("at91.mci illegal read access at 0x%03lx", offset);
        abort();
    }
}

static void mci_mmio_write(void *opaque, hwaddr offset, uint64_t value, unsigned size)
{
    MciState *s = opaque;

    switch (offset)  {
    case MCI_CR:
        if ((value & CR_MCIEN) && !(value & CR_MCIDIS)) {
            s->mcien = true;
        }
        if (value & CR_MCIDIS) {
            s->mcien = false;
        }

        if ((value & CR_PWSEN) && !(value & CR_PWSDIS)) {
            if (MR_PWSDIV(s) == 0) {
                error_report("at91.mci: cannot enable power save mode with PWSDIV set to zero");
                abort();
            }

            s->pwsen = true;
        }
        if (value & CR_PWSDIS) {
            s->pwsen = false;
        }

        if (value & CR_SWRST) {
            mci_reset_registers(s);
            qbus_reset_all(BUS(&s->sdbus0));
            qbus_reset_all(BUS(&s->sdbus1));
        }

        break;

    case MCI_MR:
        s->reg_mr = value;
        s->reg_blkr = (s->reg_blkr & 0x0000FFFF) | (value & 0xFFFF0000);
        mci_update_mcck(s);
        break;

    case MCI_DTOR:
        s->reg_dtor = value;
        break;

    case MCI_SDCR:
        s->reg_sdcr = value;

        if (SDCR_SDCSEL(s) == 0) {          // selected slot A
            // nothing to do: slot A is default and only slot available on iOBC
        } else if (SDCR_SDCSEL(s) == 1) {   // selected slot B
            error_report("at91.mci: cannot select slot B: all cards are multiplexed on slot A");
            abort();
        } else {
            error_report("at91.mci: invalid slot selection: %d", SDCR_SDCSEL(s));
            abort();
        }

        break;

    case MCI_ARGR:
        s->reg_argr = value;
        break;

    case MCI_CMDR:
        if (!s->mcien) {
            error_report("at91.mci: cannot send command while disabled");
            abort();
        }

        if (!(s->reg_sr & SR_CMDRDY)) {
            error_report("at91.mci: register CMDR is write protected while not CMDRDY");
            abort();
        }

        // TODO: If an Interrupt command is sent, this register is only
        // writeable by an interrupt response (field SPCMD). This means that
        // the current command execution cannot be interrupted or modified.

        // clear flags before sending new command
        s->reg_sr &= ~(SR_RDIRE | SR_CMDRDY | SR_RINDE | SR_RCRCE | SR_RENDE | SR_RTOE | SR_RDIRE);

        mci_do_command(s, value);
        break;

    case MCI_BLKR:
        s->reg_blkr = value;
        s->reg_mr = (s->reg_mr & 0x0000FFFF) | (value & 0xFFFF0000);
        break;

    case MCI_TDR:
        mci_tdr(s, value);
        break;

    case MCI_IER:
        s->reg_imr |= value;
        mci_irq_update(s);
        break;

    case MCI_IDR:
        s->reg_imr &= ~value;
        mci_irq_update(s);
        break;

    case PDC_START...PDC_END:
        {
            At91PdcOps ops = {
                .opaque       = s,
                .dma_rx_start = mci_dma_rx_start,
                .dma_rx_stop  = mci_dma_rx_stop,
                .dma_tx_start = mci_dma_tx_start,
                .dma_tx_stop  = mci_dma_tx_stop,
                .update_irq   = (void (*)(void *))mci_irq_update,
                .flag_endrx   = SR_ENDRX,
                .flag_endtx   = SR_ENDTX,
                .flag_rxbuff  = SR_RXBUFF,
                .flag_txbufe  = SR_TXBUFE,
                .reg_sr       = &s->reg_sr,
            };

            at91_pdc_generic_set_register(&s->pdc, &ops, offset, value);
            mci_irq_update(s);
        }
        break;

    default:
        error_report("at91.mci illegal write access at "
                      "0x%03lx [value: 0x%08lx]", offset, value);
        abort();
    }
}

static const MemoryRegionOps mci_mmio_ops = {
    .read = mci_mmio_read,
    .write = mci_mmio_write,
    .impl.min_access_size = 4,
    .impl.max_access_size = 4,
    .valid.min_access_size = 4,
    .valid.max_access_size = 4,
    .endianness = DEVICE_NATIVE_ENDIAN,
};


static void mci_device_init(Object *obj)
{
    SysBusDevice *sbd = SYS_BUS_DEVICE(obj);
    MciState *s = AT91_MCI(obj);

    qbus_create_inplace(&s->sdbus0, sizeof(s->sdbus0), TYPE_SD_BUS, DEVICE(s), "sd-bus0");
    qbus_create_inplace(&s->sdbus1, sizeof(s->sdbus1), TYPE_SD_BUS, DEVICE(s), "sd-bus1");
    qdev_init_gpio_in_named(DEVICE(s), card_select_irq_handle, "select", 1);

    sysbus_init_irq(sbd, &s->irq);

    memory_region_init_io(&s->mmio, OBJECT(s), &mci_mmio_ops, s, "at91.mci", 0x4000);
    sysbus_init_mmio(SYS_BUS_DEVICE(s), &s->mmio);
}

static void mci_reset_registers(MciState *s)
{
    s->reg_mr   = 0x00;
    s->reg_dtor = 0x00;
    s->reg_sdcr = 0x00;
    s->reg_argr = 0x00;
    s->reg_blkr = 0x00;
    s->reg_sr   = SR_CMDRDY | SR_TXRDY | SR_NOTBUSY | SR_ENDRX | SR_ENDTX | SR_RXBUFF | SR_TXBUFE;
    s->reg_imr  = 0x00;

    s->reg_rspr[0] = 0;
    s->reg_rspr[1] = 0;
    s->reg_rspr[2] = 0;
    s->reg_rspr[3] = 0;
    s->reg_rspr_index = 0;
    s->reg_rspr_len = 0;

    s->mcien = false;
    s->pwsen = false;

    s->rd_bytes_left = 0;
    s->wr_bytes_left = 0;

    // Note:
    //   s->selected_card deliberately not set as this is not part of the AT91
    //   MCI in the IOBC configuration, thus in-flight reset of _only_ the MCI
    //   should probably not reset this. External GPIO resets should propagate
    //   changes via the IRQ handler, thus update s->selected_card accordingly.
}

static void mci_device_realize(DeviceState *dev, Error **errp)
{
    MciState *s = AT91_MCI(dev);
    BlockBackend *blk0, *blk1;
    DriveInfo *di0, *di1;
    DeviceState *sd0, *sd1;

    // SD-Card 1
    di0 = drive_get(IF_SD, 0, 0);
    blk0 = di0 ? blk_by_legacy_dinfo(di0) : NULL;
    sd0 = qdev_create(qdev_get_child_bus(dev, "sd-bus0"), TYPE_SD_CARD);
    qdev_prop_set_drive(sd0, "drive", blk0, &error_abort);
    qdev_init_nofail(sd0);

    // SD-Card 2
    di1 = drive_get(IF_SD, 0, 1);
    blk1 = di1 ? blk_by_legacy_dinfo(di1) : NULL;
    sd1 = qdev_create(qdev_get_child_bus(dev, "sd-bus1"), TYPE_SD_CARD);
    qdev_prop_set_drive(sd1, "drive", blk1, &error_abort);
    qdev_init_nofail(sd1);

    mci_reset_registers(s);
    s->selected_card = 0;
    s->rx_dma_enabled = false;
    s->tx_dma_enabled = false;
}

static void mci_device_reset(DeviceState *dev)
{
    MciState *s = AT91_MCI(dev);
    mci_reset_registers(s);
}

static void mci_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    dc->realize = mci_device_realize;
    dc->reset = mci_device_reset;
}

static const TypeInfo mci_device_info = {
    .name = TYPE_AT91_MCI,
    .parent = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(MciState),
    .instance_init = mci_device_init,
    .class_init = mci_class_init,
};

static void mci_register_types(void)
{
    type_register_static(&mci_device_info);
}

type_init(mci_register_types)
