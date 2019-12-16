#ifndef HW_ARM_ISIS_OBC_SPI_H
#define HW_ARM_ISIS_OBC_SPI_H

#include "qemu/osdep.h"
#include "hw/sysbus.h"
#include "hw/ptimer.h"


#define TYPE_AT91_SPI "at91-spi"
#define AT91_SPI(obj) OBJECT_CHECK(SpiState, (obj), TYPE_AT91_SPI)

#define AT91_SPI_NUM_CHANNELS     3


typedef struct {
    SysBusDevice parent_obj;

    MemoryRegion mmio;
    qemu_irq irq;

    // TODO: registers
} SpiState;

#endif /* HW_ARM_ISIS_OBC_SPI_H */
