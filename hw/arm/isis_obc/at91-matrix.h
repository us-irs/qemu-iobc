/*
 * AT91 Bus Matrix.
 *
 * Responsibilities include switching of boot memory.
 *
 * See at91-dbgu.c for implementation status.
 *
 * Copyright (c) 2019-2020 KSat e.V. Stuttgart
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or, at your
 * option, any later version. See the COPYING file in the top-level directory.
 */

#ifndef HW_ARM_ISIS_OBC_MATRIX_H
#define HW_ARM_ISIS_OBC_MATRIX_H

#include "qemu/osdep.h"
#include "hw/sysbus.h"

#define AT91_BMS_INIT   0           // 0/false = EBI_NCS=, 1/true = ROM

#define TYPE_AT91_MATRIX "at91-matrix"
#define AT91_MATRIX(obj) OBJECT_CHECK(MatrixState, (obj), TYPE_AT91_MATRIX)

typedef enum {
    AT91_BOOTMEM_ROM,               // 0x0010 0000
    AT91_BOOTMEM_SRAM0,             // 0x0020 0000
    AT91_BOOTMEM_EBI_NCS0,          // 0x1000 0000
    __AT91_BOOTMEM_NUM_REGIONS,
} at91_bootmem_region;

typedef void(at91_bootmem_remap_cb)(void* opaque, at91_bootmem_region target);


typedef struct {
    SysBusDevice parent_obj;

    MemoryRegion mmio;

    uint32_t reg_mcfg[6];
    uint32_t reg_scfg[5];
    uint32_t reg_pras[5];
    uint32_t reg_mrcr;
    uint32_t reg_ebi_csa;
    bool bms;

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
