#include "qemu/osdep.h"
#include "qemu-common.h"
#include "qemu/log.h"
#include "qemu/error-report.h"
#include "qapi/error.h"
#include "hw/hw.h"
#include "hw/loader.h"
#include "hw/boards.h"
#include "hw/arm/boot.h"
#include "hw/misc/unimp.h"
#include "exec/address-spaces.h"
#include "exec/memory.h"
#include "cpu.h"


#define TYPE_RESERVED_MEMORY_DEVICE "reserved-memory-device"

#define RESERVED_MEMORY_DEVICE(obj) \
    OBJECT_CHECK(ReservedMemoryDeviceState, (obj), TYPE_RESERVED_MEMORY_DEVICE)

typedef struct {
    SysBusDevice parent_obj;
    MemoryRegion iomem;
    char *name;
    uint64_t size;
} ReservedMemoryDeviceState;

static void create_reserved_memory_region(const char* name, hwaddr base, hwaddr size)
{
    DeviceState *dev = qdev_create(NULL, TYPE_RESERVED_MEMORY_DEVICE);

    qdev_prop_set_string(dev, "name", name);
    qdev_prop_set_uint64(dev, "size", size);
    qdev_init_nofail(dev);

    sysbus_mmio_map_overlap(SYS_BUS_DEVICE(dev), 0, base, -1000);
}

static uint64_t reserved_memory_read(void *opaque, hwaddr offset, unsigned size)
{
    ReservedMemoryDeviceState *s = RESERVED_MEMORY_DEVICE(opaque);
    MemoryRegion *mem = &s->iomem;

    error_report("invalid memory access to '%s' [0x%08lx + 0x%08lx, r]", mem->name, mem->addr, offset);
    abort();
}

static void reserved_memory_write(void *opaque, hwaddr offset, uint64_t value, unsigned size)
{
    ReservedMemoryDeviceState *s = RESERVED_MEMORY_DEVICE(opaque);
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
    ReservedMemoryDeviceState *s = RESERVED_MEMORY_DEVICE(dev);

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
    dc->props = reserved_memory_device_props;
}

static const TypeInfo reserved_memory_device_info = {
    .name = TYPE_RESERVED_MEMORY_DEVICE,
    .parent = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(ReservedMemoryDeviceState),
    .class_init = reserved_memory_device_class_init,
};

static void reserved_memory_register_types(void)
{
    type_register_static(&reserved_memory_device_info);
}

type_init(reserved_memory_register_types)


static struct arm_boot_info iobc_board_binfo = {
    .loader_start     = 0x00000000,
    .ram_size         = 0x10000000,
    .nb_cpus          = 1,      // TODO
};

static void iobc_init(MachineState *machine)
{
    MemoryRegion *address_space_mem = get_system_memory();
    MemoryRegion *mem_pflash = g_new(MemoryRegion, 1);
    MemoryRegion *mem_sdram = g_new(MemoryRegion, 1);
    MemoryRegion *mem_internal_boot = g_new(MemoryRegion, 1);
    char *firmware_path;

    if (!bios_name) {
        warn_report("No firmware specified: Use -bios <file> to load firmware");
    }

    /* Memory Map for AT91SAM9G20                                                              */
    /*                                                                                         */
    /* start        length       description        notes                                      */
    /* --------------------------------------------------------------------------------------- */
    /* 0x0000_0000  0x0010_0000  Boot Memory        Aliases SDRAMC at boot (set by hardware)   */
    /* ...                                                                                     */
    /*                                                                                         */
    /* 0x1000_0000  0x1000_0000  NOR Program Flash  Gets loaded with program code              */
    /* 0x2000_0000  0x1000_0000  SDRAM              Copied from NOR Flash at boot via hardware */
    /* ...                                                                                     */

    // ram and flash
    memory_region_init_ram(mem_pflash, NULL, "iobc.pflash", 0x10000000, &error_fatal);
    memory_region_init_ram(mem_sdram,  NULL, "iobc.sdram",  0x10000000, &error_fatal);

    // boot memory aliases nor pflash (FIXME: this alias can be changed at runtime)
    memory_region_init_alias(mem_internal_boot, NULL, "iobc.internal.boot", mem_pflash, 0x00000000, 0x00100000);

    // put it all together
    memory_region_add_subregion(address_space_mem, 0x20000000, mem_sdram);
    memory_region_add_subregion(address_space_mem, 0x10000000, mem_pflash);
    memory_region_add_subregion(address_space_mem, 0x00000000, mem_internal_boot);

    // reserved memory, accessing this will abort
    create_reserved_memory_region("iobc.undefined", 0x90000000, 0xF0000000 - 0x90000000);
    create_reserved_memory_region("iobc.periph.reserved2", 0xFFFFFD60, 0x2A0);

    // currently unimplemented things...
    create_unimplemented_device("iobc.internal.unimp", 0x00100000, 0x10000000 - 0x00100000);
    create_unimplemented_device("iobc.ebi.unimp",      0x30000000, 0x90000000 - 0x30000000);
    create_unimplemented_device("iobc.periph.unimp1",  0xF0000000, 0xFFFFFC00 - 0xF0000000);
    create_unimplemented_device("iobc.periph.pmc",     0xFFFFFC00, 0xFFFFFD00 - 0xFFFFFC00);
    create_unimplemented_device("iobc.periph.unimp2",  0xFFFFFD00, 0xFFFFFD60 - 0xFFFFFD00);

    // load firmware
    if (bios_name) {
        firmware_path = qemu_find_file(QEMU_FILE_TYPE_BIOS, bios_name);

        if (firmware_path) {
            // load into nor flash (default program store)
            if (load_image_mr(firmware_path, mem_pflash) < 0) {
                error_report("Unable to load %s into pflash", bios_name);
                exit(1);
            }

            // nor flash gets copied to sdram at boot, thus we load it directly
            if (load_image_mr(firmware_path, mem_sdram) < 0) {
                error_report("Unable to load %s into sdram", bios_name);
                exit(1);
            }

            g_free(firmware_path);
        } else {
            error_report("Unable to find %s", bios_name);
            exit(1);
        }
    }

    arm_load_kernel(ARM_CPU(cpu_create(machine->cpu_type)), &iobc_board_binfo);
}

static void iobc_machine_init(MachineClass *mc)
{
    mc->desc = "ISIS-OBC for CubeSat";
    mc->init = iobc_init;
    mc->default_cpu_type = ARM_CPU_TYPE_NAME("arm926");
}

DEFINE_MACHINE("isis-obc", iobc_machine_init)
