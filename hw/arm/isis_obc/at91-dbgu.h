/*
 * AT91 Debug Unit.
 *
 * The Debug Unit (DBGU) provides the serial output of the AT91. Internally,
 * the DBGU provides a UART device. This implementation maps this UART to a
 * generic QEMU serial device. This serial device is set to serial descriptor
 * 0 (stdout/stdin) in iobc-board.c to directly forward standard output/input
 * of the AT91 to the emulator output/input. The main emulator window should
 * thus behave like a normal serial (debugging) console to the AT91.
 *
 * See at91-dbgu.c for implementation status.
 *
 * Copyright (c) 2019-2020 KSat e.V. Stuttgart
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or, at your
 * option, any later version. See the COPYING file in the top-level directory.
 */

#ifndef HW_ARM_ISIS_OBC_DBGU_H
#define HW_ARM_ISIS_OBC_DBGU_H

#include "qemu/osdep.h"
#include "hw/sysbus.h"
#include "chardev/char-fe.h"


#define TYPE_AT91_DBGU "at91-dbgu"
#define AT91_DBGU(obj) OBJECT_CHECK(DbguState, (obj), TYPE_AT91_DBGU)


typedef struct {
    SysBusDevice parent_obj;

    qemu_irq irq;
    MemoryRegion mmio;
    CharBackend chr;

    bool rx_enabled;
    bool tx_enabled;

    // registers
    uint32_t reg_mr;
    uint32_t reg_imr;
    uint32_t reg_sr;
    uint32_t reg_rhr;
    uint32_t reg_thr;
    uint32_t reg_brgr;
    uint32_t reg_cidr;
    uint32_t reg_exid;
    uint32_t reg_fnr;
} DbguState;

#endif /* HW_ARM_ISIS_OBC_DBGU_H */
