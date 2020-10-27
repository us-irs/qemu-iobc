/*
 * AT91 Reset Controller.
 *
 * Processor and peripheral resets initiated by code.
 *
 * See at91-rstc.c for implementation status.
 *
 * Copyright (c) 2019-2020 KSat e.V. Stuttgart
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or, at your
 * option, any later version. See the COPYING file in the top-level directory.
 */

#ifndef HW_ARM_ISIS_OBC_RSTC_H
#define HW_ARM_ISIS_OBC_RSTC_H

#include "qemu/osdep.h"
#include "hw/sysbus.h"


#define TYPE_AT91_RSTC "at91-rstc"
#define AT91_RSTC(obj) OBJECT_CHECK(RstcState, (obj), TYPE_AT91_RSTC)


typedef struct {
    SysBusDevice parent_obj;

    MemoryRegion mmio;
    qemu_irq irq;

    uint32_t reg_sr;
    uint32_t reg_mr;
} RstcState;

#endif /* HW_ARM_ISIS_OBC_RSTC_H */
