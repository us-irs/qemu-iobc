/*
 * AT91 Timer/Counter.
 *
 * See at91-tc.c for implementation status.
 */

#ifndef HW_ARM_ISIS_OBC_TC_H
#define HW_ARM_ISIS_OBC_TC_H

#include "qemu/osdep.h"
#include "hw/ptimer.h"
#include "hw/sysbus.h"


#define TYPE_AT91_TC "at91-tc"
#define AT91_TC(obj) OBJECT_CHECK(TcState, (obj), TYPE_AT91_TC)

#define AT91_TC_NUM_CHANNELS     3


struct TcState;
typedef struct TcState TcState;


typedef struct {
    TcState *parent;

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


struct TcState {
    SysBusDevice parent_obj;

    MemoryRegion mmio;
    TcChanState chan[AT91_TC_NUM_CHANNELS];

    unsigned mclk;
    uint32_t reg_bmr;
};

void at91_tc_set_master_clock(TcState *s, unsigned mclk);

#endif /* HW_ARM_ISIS_OBC_TC_H */
