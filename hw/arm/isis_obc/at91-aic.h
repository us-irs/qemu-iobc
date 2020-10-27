/*
 * AT91 Advanced Interrupt Controller.
 *
 * Note: All System Controller (SYSC) interrupts should be connected to the
 * AIC Stub and not directly to the AIC. On the AT91, all SYSC interrupts are
 * handled by a single interrupt line to the AIC. The AIC stub collects the
 * SYSC IRQs to create this single IRQ line. This means that all SYSC
 * interrupts should be connected to the stub, which in turn is then connected
 * to the AIC itself on line 1. All other interrupts should be connected to
 * their corresponding AIC IRQ line (see AT91 technical documentation for
 * details).
 *
 * Copyright (c) 2019-2020 KSat e.V. Stuttgart
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or, at your
 * option, any later version. See the COPYING file in the top-level directory.
 */

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
