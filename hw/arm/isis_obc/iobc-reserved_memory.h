#ifndef HW_ARM_ISIS_OBC_RESERVED_MEM_H
#define HW_ARM_ISIS_OBC_RESERVED_MEM_H

#include "qemu/osdep.h"
#include "hw/sysbus.h"


#define TYPE_IOBC_RESERVED_MEMORY_DEVICE "iobc-reserved_memory-device"

#define IOBC_RESERVED_MEMORY_DEVICE(obj) \
    OBJECT_CHECK(ReservedMemoryDeviceState, (obj), TYPE_IOBC_RESERVED_MEMORY_DEVICE)

typedef struct {
    SysBusDevice parent_obj;
    MemoryRegion iomem;
    char *name;
    uint64_t size;
} ReservedMemoryDeviceState;

inline static void create_reserved_memory_region(const char* name, hwaddr base, hwaddr size)
{
    DeviceState *dev = qdev_create(NULL, TYPE_IOBC_RESERVED_MEMORY_DEVICE);

    qdev_prop_set_string(dev, "name", name);
    qdev_prop_set_uint64(dev, "size", size);
    qdev_init_nofail(dev);

    sysbus_mmio_map_overlap(SYS_BUS_DEVICE(dev), 0, base, -1000);
}

#endif /* HW_ARMISIS_OBC_RESERVED_MEM_H */
