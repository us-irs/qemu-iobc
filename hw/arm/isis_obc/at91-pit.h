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

    uint32_t reg_mr;
    uint32_t reg_sr;

    uint32_t picnt;
} PitState;

#endif /* HW_ARM_ISIS_OBC_PIT_H */
