#ifndef HW_ARM_ISIS_OBC_PIO_H
#define HW_ARM_ISIS_OBC_PIO_H

#include "qemu/osdep.h"
#include "hw/sysbus.h"


#define AT91_PIO_NUM_PINS   32

#define TYPE_AT91_PIO "at91-pio"
#define AT91_PIO(obj) OBJECT_CHECK(PioState, (obj), TYPE_AT91_PIO)


typedef struct {
    SysBusDevice parent_obj;

    MemoryRegion mmio;
    qemu_irq irq;
    qemu_irq pin_out[AT91_PIO_NUM_PINS];

    // registers
    uint32_t reg_psr;
    uint32_t reg_osr;
    uint32_t reg_ifsr;
    uint32_t reg_odsr;
    uint32_t reg_pdsr;
    uint32_t reg_imr;
    uint32_t reg_isr;
    uint32_t reg_mdsr;
    uint32_t reg_pusr;
    uint32_t reg_absr;
    uint32_t reg_owsr;

    // raw input states
    uint32_t pin_state_in;
    uint32_t pin_state_periph_a;
    uint32_t pin_state_periph_b;
} PioState;

#endif /* HW_ARM_ISIS_OBC_PIO_H */
