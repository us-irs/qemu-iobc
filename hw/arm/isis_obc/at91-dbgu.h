#ifndef HW_ARM_ISIS_OBC_DBGU_H
#define HW_ARM_ISIS_OBC_DBGU_H

#include "qemu/osdep.h"
#include "hw/sysbus.h"
#include "chardev/char-fe.h"


#define TYPE_AT91_DBGU "at91-dbgu"
#define AT91_DBGU(obj) OBJECT_CHECK(DbguState, (obj), TYPE_AT91_DBGU)


typedef struct {
    SysBusDevice parent_obj;

    qemu_irq irq;
    MemoryRegion mmio;
    CharBackend chr;

    bool rx_enabled;
    bool tx_enabled;

    // registers
    uint32_t reg_mr;
    uint32_t reg_imr;
    uint32_t reg_sr;
    uint32_t reg_rhr;
    uint32_t reg_thr;
    uint32_t reg_brgr;
    uint32_t reg_cidr;
    uint32_t reg_exid;
    uint32_t reg_fnr;
} DbguState;

#endif /* HW_ARM_ISIS_OBC_DBGU_H */
