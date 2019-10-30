#ifndef HW_ARM_ISIS_OBC_AIC_STUB_H
#define HW_ARM_ISIS_OBC_AIC_STUB_H

#include "qemu/osdep.h"
#include "hw/sysbus.h"


#define TYPE_AT91_AIC_STUB "at91-aicstub"
#define AT91_AIC_STUB(obj) OBJECT_CHECK(AicStubState, (obj), TYPE_AT91_AIC_STUB)

typedef struct {
    SysBusDevice parent_obj;

    qemu_irq irq;
    uint32_t line_state;
} AicStubState;

#endif /* HW_ARM_ISIS_OBC_AIC_STUB_H */
