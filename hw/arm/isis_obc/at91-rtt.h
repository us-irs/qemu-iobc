/*
 * AT91 Real-Time Timer.
 *
 * Copyright (c) 2019-2020 KSat e.V. Stuttgart
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or, at your
 * option, any later version. See the COPYING file in the top-level directory.
 */

#ifndef HW_ARM_ISIS_OBC_RTT_H
#define HW_ARM_ISIS_OBC_RTT_H

#include "qemu/osdep.h"
#include "hw/sysbus.h"
#include "hw/ptimer.h"


#define TYPE_AT91_RTT "at91-rtt"
#define AT91_RTT(obj) OBJECT_CHECK(RttState, (obj), TYPE_AT91_RTT)


typedef struct {
    SysBusDevice parent_obj;

    MemoryRegion mmio;
    qemu_irq irq;
    ptimer_state *timer;

    uint32_t reg_mr;
    uint32_t reg_ar;
    uint32_t reg_vr;
    uint32_t reg_sr;
} RttState;

#endif /* HW_ARM_ISIS_OBC_RTT_H */
