/*
 * AT91 Bus Matrix.
 *
 * Responsibilities include switching of boot memory.
 *
 * See at91-dbgu.c for implementation status.
 */

#ifndef HW_ARM_ISIS_OBC_MATRIX_H
#define HW_ARM_ISIS_OBC_MATRIX_H

#include "qemu/osdep.h"
#include "hw/sysbus.h"
#include "hw/ptimer.h"


#define TYPE_AT91_MATRIX "at91-matrix"
#define AT91_MATRIX(obj) OBJECT_CHECK(MatrixState, (obj), TYPE_AT91_MATRIX)

typedef enum {
    AT91_BOOTMEM_ROM,
    AT91_BOOTMEM_SRAM,
    AT91_BOOTMEM_SDRAM,
    __AT91_BOOTMEM_NUM_REGIONS,
} at91_bootmem_region;

typedef void(at91_bootmem_remap_cb)(void* opaque, at91_bootmem_region target);


typedef struct {
    SysBusDevice parent_obj;

    MemoryRegion mmio;

    uint32_t reg_mrcr;

    at91_bootmem_remap_cb *bootmem_cb;
    void *bootmem_opaque;
} MatrixState;


inline static void
at91_matrix_set_bootmem_remap_callback(MatrixState *s, void *opaque, at91_bootmem_remap_cb *cbfn)
{
    s->bootmem_cb = cbfn;
    s->bootmem_opaque = opaque;
}

#endif /* HW_ARM_ISIS_OBC_MATRIX_H */
