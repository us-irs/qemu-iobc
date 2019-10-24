#include "qemu/osdep.h"
#include "qemu-common.h"
#include "qemu/error-report.h"
#include "qapi/error.h"
#include "hw/hw.h"
#include "hw/loader.h"
#include "hw/boards.h"
#include "hw/arm/boot.h"
#include "hw/misc/unimp.h"
#include "cpu.h"

#include "iobc-reserved_memory.h"
#include "iobc-pmc.h"


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
    /*                                                                                         */
    /* 0xFFFF_FC00  0x0000_0100  PMC                                                           */
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

    // peripherals
    sysbus_create_simple(TYPE_IOBC_PMC, 0xFFFFFC00, NULL);

    // currently unimplemented things...
    create_unimplemented_device("iobc.internal.unimp", 0x00100000, 0x10000000 - 0x00100000);
    create_unimplemented_device("iobc.ebi.unimp",      0x30000000, 0x90000000 - 0x30000000);
    create_unimplemented_device("iobc.periph.unimp1",  0xF0000000, 0xFFFFFC00 - 0xF0000000);
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
