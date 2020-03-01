/*
 * AT91 Advanced Interrupt Controller stub.
 *
 * Collects interrupts from system controller (SYSC) devices and forwards them
 * to the Advanced Interrupt Controller (AIC). SYSC devices share a single
 * interrupt line of the AIC, this stub collects them and ORs them together to
 * a single qemu_irq to be forwarded to the AIC.
 */

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
