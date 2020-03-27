/*
 * Basic reserved memory region.
 *
 * Implements a basic reserved memory region. Access to this region is
 * considered invalid and will output the location of the incident to the log
 * as well as abort the emulator.
 */

#ifndef HW_ARM_ISIS_OBC_RESERVED_MEM_H
#define HW_ARM_ISIS_OBC_RESERVED_MEM_H

#include "qemu/osdep.h"
#include "hw/qdev-properties.h"
#include "hw/sysbus.h"


#define TYPE_IOBC_RESERVED_MEMORY "iobc.memory.reserved"

#define IOBC_RESERVED_MEMORY(obj) \
    OBJECT_CHECK(ReservedMemoryDeviceState, (obj), TYPE_IOBC_RESERVED_MEMORY)

typedef struct {
    SysBusDevice parent_obj;
    MemoryRegion iomem;
    char *name;
    uint64_t size;
} ReservedMemoryDeviceState;

/*
 * Create a reserved memory region.
 *
 * Create a reserved memory region with the given name, base-address and size.
 * Access to this region will output the location of the incident to the log
 * and abort the emulator.
 */
inline static void create_reserved_memory_region(const char* name, hwaddr base, hwaddr size)
{
    DeviceState *dev = qdev_create(NULL, TYPE_IOBC_RESERVED_MEMORY);

    qdev_prop_set_string(dev, "name", name);
    qdev_prop_set_uint64(dev, "size", size);
    qdev_init_nofail(dev);

    sysbus_mmio_map_overlap(SYS_BUS_DEVICE(dev), 0, base, -1000);
}

#endif /* HW_ARMISIS_OBC_RESERVED_MEM_H */
