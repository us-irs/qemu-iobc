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

    // currently unimplemented things...
    create_unimplemented_device("iobc.internal.unimp", 0x00100000, 0x10000000 - 0x00100000);
    create_unimplemented_device("iobc.ebi.unimp",      0x30000000, 0x90000000 - 0x30000000);
    create_unimplemented_device("iobc.periph.unimp",   0xF0000000, 0x10000000);
    create_unimplemented_device("iobc.undefined",      0x90000000, 0xF0000000 - 0x90000000);

    arm_load_kernel(ARM_CPU(cpu_create(machine->cpu_type)), &iobc_board_binfo);
}

static void iobc_machine_init(MachineClass *mc)
{
    mc->desc = "ISIS-OBC for CubeSat";
    mc->init = iobc_init;
    mc->default_cpu_type = ARM_CPU_TYPE_NAME("arm926");
}

DEFINE_MACHINE("isis-obc", iobc_machine_init)
