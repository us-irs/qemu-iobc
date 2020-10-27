/*
 * AT91 Two-Wire Interface (I2C).
 *
 * Emulation of devices connected to TWI/I2C is done via outside processes
 * communicating via the IOX server (for details see ioxfer-server.h). The
 * socket address can be set via the "socket" property (as is done and defined
 * in iobc_board.c).
 * - Transfer data from AT19 to client process (category IOX_CAT_DATA, ID
 *   IOX_CID_DATA_OUT, Payload contains raw data).
 * - Transfer data from client process to AT91 (category IOX_CAT_DATA, ID
 *   IOX_CID_DATA_IN, payload contains raw data).
 * - Send start frame (AT91 to client, category IOX_CAT_DATA, ID
 *   IOX_CID_CTRL_START, payload defined in struct start_frame, see technical
 *   documentation of AT91 for details).
 * - Send stop frame (AT91 to client, category IOX_CAT_DATA, ID
 *   IOX_CID_CTRL_START, no payload)
 * Note that I2C/TWI data transfers from master to slave (currently only
 * DATA_OUT, meaning transfers from AT91 to client) are always encapsulated by
 * start and stop frames.
 *
 * In case of transmission from client to AT91, the IOX server sends a
 * response with a 32 bit little-endian status code. Currently this code can
 * be one of the following Unix/Linux error codes:
 * - 0: Success.
 *
 * As due to the different nature of the transport it is not possible to
 * emulate all failure modes and flags. Thus a mechanism for fault injection
 * is provided, allowing to set
 * - OVRE (category IOX_CAT_FAULT, ID IOX_CID_FAULT_OVRE)
 * - NACK (category IOX_CAT_FAULT, ID IOX_CID_FAULT_NACK)
 * - ARBLST (category IOX_CAT_FAULT, ID IOX_CID_FAULT_ARBLST)
 *
 * Additional notes:
 * - Master clock of AT91 must be set/updated via at91_twi_set_master_clock.
 *
 * See at91-twi.c for implementation status.
 *
 * Copyright (c) 2019-2020 KSat e.V. Stuttgart
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or, at your
 * option, any later version. See the COPYING file in the top-level directory.
 */

#ifndef HW_ARM_ISIS_OBC_TWI_H
#define HW_ARM_ISIS_OBC_TWI_H

#include "qemu/osdep.h"
#include "hw/sysbus.h"
#include "hw/ptimer.h"

#include "at91-pdc.h"
#include "ioxfer-server.h"


#define TYPE_AT91_TWI "at91-twi"
#define AT91_TWI(obj) OBJECT_CHECK(TwiState, (obj), TYPE_AT91_TWI)


typedef enum {
    AT91_TWI_MODE_OFFLINE,
    AT91_TWI_MODE_MASTER,
    AT91_TWI_MODE_SLAVE,
} TwiMode;

typedef struct {
    SysBusDevice parent_obj;

    MemoryRegion mmio;
    qemu_irq irq;

    char* socket;
    IoXferServer *server;
    Buffer rcvbuf;
    Buffer sendbuf;
    ptimer_state *chrtx_timer;

    TwiMode mode;
    unsigned mclk;
    unsigned clock;

    uint32_t reg_mmr;
    uint32_t reg_smr;
    uint32_t reg_iadr;
    uint32_t reg_cwgr;
    uint32_t reg_sr;
    uint32_t reg_imr;
    uint32_t reg_rhr;

    At91Pdc pdc;
    bool dma_rx_enabled;
} TwiState;


void at91_twi_set_master_clock(TwiState *s, unsigned mclk);

#endif /* HW_ARM_ISIS_OBC_TWI_H */
