/*
 * AT91 Multimedia Card Interface.
 *
 * SD and multimedia card support.
 * This is specifically implemented for the iOBC board. SD-Cards are
 * multiplexed outside of the actual MCI interface of the AT91 via the
 * "select" GPIO pin. Only slot A is used, thus slot B is not implemented.
 * Furthermore, only SD-cards are supported.
 *
 * See at91-mci.c for implementation status.
 *
 * Copyright (c) 2019-2020 KSat e.V. Stuttgart
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or, at your
 * option, any later version. See the COPYING file in the top-level directory.
 */

#ifndef HW_ARM_ISIS_OBC_MCI_H
#define HW_ARM_ISIS_OBC_MCI_H

#include "qemu/osdep.h"
#include "hw/sysbus.h"
#include "hw/sd/sd.h"
#include "at91-pdc.h"


#define TYPE_AT91_MCI "at91-mci"
#define AT91_MCI(obj) OBJECT_CHECK(MciState, (obj), TYPE_AT91_MCI)


typedef struct {
    SysBusDevice parent_obj;

    MemoryRegion mmio;
    qemu_irq irq;

    SDBus sdbus0;
    SDBus sdbus1;

    unsigned mclk;
    unsigned mcck;

    uint32_t reg_mr;
    uint32_t reg_dtor;
    uint32_t reg_sdcr;
    uint32_t reg_argr;
    uint32_t reg_blkr;
    uint32_t reg_sr;
    uint32_t reg_imr;
    uint32_t reg_rspr[4];
    uint8_t reg_rspr_index;
    uint8_t reg_rspr_len;

    bool mcien;
    bool pwsen;

    uint8_t selected_card;

    size_t rd_bytes_left;
    size_t wr_bytes_left;
    size_t wr_bytes_blk;

    At91Pdc pdc;
    bool rx_dma_enabled;
    bool tx_dma_enabled;
} MciState;


void at91_mci_set_master_clock(MciState *s, unsigned mclk);

#endif /* HW_ARM_ISIS_OBC_MCI_H */
