/*
 * Basic reserved memory region.
 *
 * See iobc-reserved_memory.h for details.
 *
 * Copyright (c) 2019-2020 KSat e.V. Stuttgart
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or, at your
 * option, any later version. See the COPYING file in the top-level directory.
 */

#include "qemu/osdep.h"
#include "qemu/error-report.h"
#include "qapi/error.h"
#include "hw/qdev-properties.h"
#include "hw/sysbus.h"

#include "iobc-reserved_memory.h"


static uint64_t reserved_memory_read(void *opaque, hwaddr offset, unsigned size)
{
    ReservedMemoryDeviceState *s = IOBC_RESERVED_MEMORY(opaque);
    MemoryRegion *mem = &s->iomem;

    error_report("invalid memory access to '%s' [0x%08lx + 0x%08lx, r]", mem->name, mem->addr, offset);
    abort();
}

static void reserved_memory_write(void *opaque, hwaddr offset, uint64_t value, unsigned size)
{
    ReservedMemoryDeviceState *s = IOBC_RESERVED_MEMORY(opaque);
    MemoryRegion *mem = &s->iomem;

    error_report("invalid memory access to '%s' [0x%08lx + 0x%08lx, r]", mem->name, mem->addr, offset);
    abort();
}

static const MemoryRegionOps reserved_memory_ops = {
    .read = reserved_memory_read,
    .write = reserved_memory_write,
    .impl.min_access_size = 1,
    .impl.max_access_size = 8,
    .valid.min_access_size = 1,
    .valid.max_access_size = 8,
    .endianness = DEVICE_NATIVE_ENDIAN,
};

static void reserved_memory_device_realize(DeviceState *dev, Error **errp)
{
    ReservedMemoryDeviceState *s = IOBC_RESERVED_MEMORY(dev);

    if (s->size == 0) {
        error_setg(errp, "property 'size' not specified or zero");
        return;
    }

    if (s->name == NULL) {
        error_setg(errp, "property 'name' not specified");
        return;
    }

    memory_region_init_io(&s->iomem, OBJECT(s), &reserved_memory_ops, s, s->name, s->size);
    sysbus_init_mmio(SYS_BUS_DEVICE(s), &s->iomem);
}

static Property reserved_memory_device_props[] = {
    DEFINE_PROP_UINT64("size", ReservedMemoryDeviceState, size, 0),
    DEFINE_PROP_STRING("name", ReservedMemoryDeviceState, name),
    DEFINE_PROP_END_OF_LIST(),
};

static void reserved_memory_device_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    dc->realize = reserved_memory_device_realize;
    device_class_set_props(dc, reserved_memory_device_props);
}

static const TypeInfo reserved_memory_device_info = {
    .name = TYPE_IOBC_RESERVED_MEMORY,
    .parent = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(ReservedMemoryDeviceState),
    .class_init = reserved_memory_device_class_init,
};

static void reserved_memory_register_types(void)
{
    type_register_static(&reserved_memory_device_info);
}

type_init(reserved_memory_register_types)
