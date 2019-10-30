#ifndef HW_ARM_ISIS_OBC_AIC_H
#define HW_ARM_ISIS_OBC_AIC_H

#include "qemu/osdep.h"
#include "hw/sysbus.h"


#define TYPE_AT91_AIC "at91-aic"
#define AT91_AIC(obj) OBJECT_CHECK(AicState, (obj), TYPE_AT91_AIC)


typedef struct {
    uint8_t pri;
    uint8_t irq;
} AicIrqStackElem;


typedef struct {
    SysBusDevice parent_obj;

    MemoryRegion mmio;
    qemu_irq irq;
    qemu_irq fiq;

    // registers
    uint32_t reg_smr[32];
    uint32_t reg_svr[32];
    uint32_t reg_ipr;
    uint32_t reg_imr;
    uint32_t reg_cisr;
    uint32_t reg_spu;
    uint32_t reg_dcr;
    uint32_t reg_ffsr;

    AicIrqStackElem irq_stack[9];   // 8 + spurious
    int irq_stack_pos;

    uint32_t line_state;
} AicState;

#endif /* HW_ARM_ISIS_OBC_AIC_H */
