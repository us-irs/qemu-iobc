/*
 * AT91 Peripheral I/O controller.
 *
 * The Peripheral I/O controller (PIO) controls GPIO pins and allows
 * interrupts to be triggered by setting the individual pin states.
 *
 * Communication with out-of-emulator processes controlling/accessing these
 * states is done via sockets and the custom I/O transfer server (IOX). See
 * ioxfer-server.h for details on the transfer protocoll, see at91-pio.c for
 * the respecitve category and frame IDs (IOX_CAT_* and IOX_CID_*). Currently
 * supported operations are:
 * - Querying pin-state (IOX_CID_PINSTATE_GET in/out frame). Note that only
 *   the reply carries a payload.
 * - Recieving pin-state updates on change (via IOX_CID_PINSTATE_OUT output
 *   frame).
 * - Setting pin-state (IOX_CID_PINSTATE_ENABLE/IOX_CID_PINSTATE_DISABLE).
 *
 * In all instances, the payload of the respecitve command is a 32 bit
 * little-endian integer representing the current/to-be-set state of the 32
 * pins (bit index equals pin number).
 *
 * See at91-pio.c for implementation status.
 *
 * Copyright (c) 2019-2020 KSat e.V. Stuttgart
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or, at your
 * option, any later version. See the COPYING file in the top-level directory.
 */

#ifndef HW_ARM_ISIS_OBC_PIO_H
#define HW_ARM_ISIS_OBC_PIO_H

#include "qemu/osdep.h"
#include "hw/sysbus.h"

#include "ioxfer-server.h"


#define AT91_PIO_NUM_PINS   32

#define TYPE_AT91_PIO "at91-pio"
#define AT91_PIO(obj) OBJECT_CHECK(PioState, (obj), TYPE_AT91_PIO)


typedef struct {
    SysBusDevice parent_obj;

    MemoryRegion mmio;
    qemu_irq irq;
    qemu_irq pin_out[AT91_PIO_NUM_PINS];

    char* socket;
    IoXferServer *server;

    // registers
    uint32_t reg_psr;
    uint32_t reg_osr;
    uint32_t reg_ifsr;
    uint32_t reg_odsr;
    uint32_t reg_pdsr;
    uint32_t reg_imr;
    uint32_t reg_isr;
    uint32_t reg_mdsr;
    uint32_t reg_pusr;
    uint32_t reg_absr;
    uint32_t reg_owsr;

    // raw input states
    uint32_t pin_state_in;
    uint32_t pin_state_periph_a;
    uint32_t pin_state_periph_b;
} PioState;

#endif /* HW_ARM_ISIS_OBC_PIO_H */
