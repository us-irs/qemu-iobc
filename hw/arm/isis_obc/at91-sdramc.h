#ifndef HW_ARM_ISIS_OBC_SDRAMC_H
#define HW_ARM_ISIS_OBC_SDRAMC_H

#include "qemu/osdep.h"
#include "hw/sysbus.h"
#include "hw/ptimer.h"

#include "ioxfer-server.h"


#define TYPE_AT91_SDRAMC "at91-sdramc"
#define AT91_SDRAMC(obj) OBJECT_CHECK(SdramcState, (obj), TYPE_AT91_SDRAMC)

typedef struct {
    SysBusDevice parent_obj;

    MemoryRegion mmio;
    qemu_irq irq;

    char* socket;
    IoXferServer *server;

    uint32_t reg_mr;
    uint32_t reg_tr;
    uint32_t reg_cr;
    uint32_t reg_lpr;
    uint32_t reg_imr;
    uint32_t reg_isr;
    uint32_t reg_mdr;
} SdramcState;

#endif /* HW_ARM_ISIS_OBC_SDRAMC_H */
