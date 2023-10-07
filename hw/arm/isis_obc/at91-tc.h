/*
 * AT91 Timer/Counter.
 *
 * See at91-tc.c for implementation status.
 *
 * Copyright (c) 2019-2020 KSat e.V. Stuttgart
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or, at your
 * option, any later version. See the COPYING file in the top-level directory.
 */

#ifndef HW_ARM_ISIS_OBC_TC_H
#define HW_ARM_ISIS_OBC_TC_H

#include "qemu/osdep.h"
#include "hw/ptimer.h"
#include "hw/sysbus.h"


#define TYPE_AT91_TC "at91-tc"
#define AT91_TC(obj) OBJECT_CHECK(At91Tc, (obj), TYPE_AT91_TC)

#define AT91_TC_NUM_CHANNELS     3


struct At91Tc;
typedef struct At91Tc At91Tc;


typedef struct {
    At91Tc *parent;

    unsigned clk;
    ptimer_state *timer;
    qemu_irq irq;

    int cstep;
    uint32_t reg_cmr;
    uint32_t reg_cv;
    uint32_t reg_ra;
    uint32_t reg_rb;
    uint32_t reg_rc;
    uint32_t reg_sr;
    uint32_t reg_imr;
} TcChanState;


struct At91Tc {
    SysBusDevice parent_obj;

    MemoryRegion mmio;
    TcChanState chan[AT91_TC_NUM_CHANNELS];

    unsigned mclk;
    uint32_t reg_bmr;
};

void at91_tc_set_master_clock(At91Tc *s, unsigned mclk);

#endif /* HW_ARM_ISIS_OBC_TC_H */
