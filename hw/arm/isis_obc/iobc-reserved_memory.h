/*
 * Basic reserved memory region.
 *
 * Implements a basic reserved memory region. Access to this region is
 * considered invalid and will output the location of the incident to the log
 * as well as abort the emulator.
 *
 * Copyright (c) 2019-2020 KSat e.V. Stuttgart
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or, at your
 * option, any later version. See the COPYING file in the top-level directory.
 */

#ifndef HW_ARM_ISIS_OBC_RESERVED_MEM_H
#define HW_ARM_ISIS_OBC_RESERVED_MEM_H

#include "qemu/osdep.h"
#include "hw/qdev-properties.h"
#include "hw/sysbus.h"


#define TYPE_IOBC_RESERVED_MEMORY "iobc.memory.reserved"

#define IOBC_RESERVED_MEMORY(obj) \
    OBJECT_CHECK(ReservedMemory, (obj), TYPE_IOBC_RESERVED_MEMORY)

typedef struct {
    SysBusDevice parent_obj;
    MemoryRegion iomem;
    char *name;
    uint64_t size;
} ReservedMemory;

#endif /* HW_ARMISIS_OBC_RESERVED_MEM_H */
