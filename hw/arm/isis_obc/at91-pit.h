/*
 * AT91 Programmable Interrupt Timer
 *
 * Interrupt timer implementation based on the emulated QEMU system timer and
 * the AT91 master clock.
 *
 * Notes: Master clock of AT91 must be set/updated via
 * at91_pit_set_master_clock.
 *
 * Copyright (c) 2019-2020 KSat e.V. Stuttgart
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or, at your
 * option, any later version. See the COPYING file in the top-level directory.
 */

#ifndef HW_ARM_ISIS_OBC_PIT_H
#define HW_ARM_ISIS_OBC_PIT_H

#include "qemu/osdep.h"
#include "hw/sysbus.h"
#include "hw/ptimer.h"


#define TYPE_AT91_PIT "at91-pit"
#define AT91_PIT(obj) OBJECT_CHECK(PitState, (obj), TYPE_AT91_PIT)


typedef struct {
    SysBusDevice parent_obj;

    MemoryRegion mmio;
    qemu_irq irq;
    ptimer_state *timer;
    unsigned mclk;

    uint32_t reg_mr;
    uint32_t reg_sr;

    uint32_t picnt;
} PitState;


/*
 * Set/update master-clock reference value on PIT.
 */
void at91_pit_set_master_clock(PitState *s, unsigned mclk);

#endif /* HW_ARM_ISIS_OBC_PIT_H */
