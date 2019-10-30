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
#include "at91-pmc.h"
#include "at91-dbgu.h"


// TODO:
// - implement at91-dbgu
// - implement at91-aic


static struct arm_boot_info iobc_board_binfo = {
    .loader_start     = 0x00000000,
    .ram_size         = 0x10000000,
    .nb_cpus          = 1,      // TODO
};

static void iobc_init(MachineState *machine)
{
    MemoryRegion *address_space_mem = get_system_memory();
    MemoryRegion *mem_boot   = g_new(MemoryRegion, 1);
    MemoryRegion *mem_rom    = g_new(MemoryRegion, 1);
    MemoryRegion *mem_sram0  = g_new(MemoryRegion, 1);
    MemoryRegion *mem_sram1  = g_new(MemoryRegion, 1);
    MemoryRegion *mem_pflash = g_new(MemoryRegion, 1);
    MemoryRegion *mem_sdram  = g_new(MemoryRegion, 1);
    char *firmware_path;

    qemu_irq aic_irq[32];
    int i;

    ARMCPU *cpu = ARM_CPU(cpu_create(machine->cpu_type));

    DeviceState *tmp;

    /* Memory Map for AT91SAM9G20 (current implementation status)                              */
    /*                                                                                         */
    /* start        length       description        notes                                      */
    /* --------------------------------------------------------------------------------------- */
    /* 0x0000_0000  0x0010_0000  Boot Memory        Aliases SDRAMC at boot (set by hardware)   */
    /* 0x0010_0000  0x0000_8000  Internal ROM                                                  */
    /* 0x0020_0000  0x0000_4000  Internal SRAM0                                                */
    /* 0x0030_0000  0x0000_4000  Internal SRAM1                                                */
    /* ...                                                                                     */
    /*                                                                                         */
    /* 0x1000_0000  0x1000_0000  NOR Program Flash  Gets loaded with program code              */
    /* 0x2000_0000  0x1000_0000  SDRAM              Copied from NOR Flash at boot via hardware */
    /* ...                                                                                     */
    /*                                                                                         */
    /* 0xFFFF_F200  0x0000_0200  Debug Unit (DBGU)                                             */
    /* ...                                                                                     */
    /* 0xFFFF_FC00  0x0000_0100  PMC                                                           */
    /* ...                                                                                     */

    // rom, ram, and flash
    memory_region_init_rom(mem_rom,   NULL, "iobc.internal.rom",   0x8000, &error_fatal);
    memory_region_init_ram(mem_sram0, NULL, "iobc.internal.sram0", 0x4000, &error_fatal);
    memory_region_init_ram(mem_sram1, NULL, "iobc.internal.sram1", 0x4000, &error_fatal);

    memory_region_init_ram(mem_pflash, NULL, "iobc.pflash", 0x10000000, &error_fatal);
    memory_region_init_ram(mem_sdram,  NULL, "iobc.sdram",  0x10000000, &error_fatal);

    // boot memory aliases nor pflash (FIXME: this alias can be changed at runtime)
    memory_region_init_alias(mem_boot, NULL, "iobc.internal.boot", mem_pflash, 0x00000000, 0x00100000);

    // put it all together
    memory_region_add_subregion(address_space_mem, 0x00000000, mem_boot);
    memory_region_add_subregion(address_space_mem, 0x00100000, mem_rom);
    memory_region_add_subregion(address_space_mem, 0x00200000, mem_sram0);
    memory_region_add_subregion(address_space_mem, 0x00300000, mem_sram1);
    memory_region_add_subregion(address_space_mem, 0x10000000, mem_pflash);
    memory_region_add_subregion(address_space_mem, 0x20000000, mem_sdram);

    // reserved memory, accessing this will abort
    create_reserved_memory_region("iobc.undefined", 0x90000000, 0xF0000000 - 0x90000000);
    create_reserved_memory_region("iobc.periph.reserved0", 0xF0000000, 0xFFFA0000 - 0xF0000000);
    create_reserved_memory_region("iobc.periph.reserved1", 0xFFFE4000, 0xFFFFC000 - 0xFFFE4000);
    create_reserved_memory_region("iobc.periph.reserved2", 0xFFFEC000, 0xFFFFE800 - 0xFFFEC000);
    create_reserved_memory_region("iobc.periph.reserved3", 0xFFFFFA00, 0xFFFFFC00 - 0xFFFFFA00);
    create_reserved_memory_region("iobc.periph.reserved4", 0xFFFFFD60, 0x2A0);
    create_reserved_memory_region("iobc.internal.reserved0", 0x108000, 0x200000 - 0x108000);
    create_reserved_memory_region("iobc.internal.reserved1", 0x204000, 0x300000 - 0x204000);
    create_reserved_memory_region("iobc.internal.reserved2", 0x304000, 0x400000 - 0x304000);
    create_reserved_memory_region("iobc.internal.reserved3", 0x504000, 0x0FFFFFFF - 0x504000);

    // peripherals
    // FIXME: clean this up (aparently sysbus_create_simple is legacy/deprecated?)
    tmp = qdev_create(NULL, TYPE_AT91_AIC);
    qdev_init_nofail(tmp);
    sysbus_mmio_map(SYS_BUS_DEVICE(tmp), 0, 0xFFFFF000);
    for (i = 0; i < 32; i++) {
        aic_irq[i] = qdev_get_gpio_in_named(tmp, "irq-line", i);
    }
    sysbus_connect_irq(SYS_BUS_DEVICE(tmp), 0, qdev_get_gpio_in(DEVICE(cpu), ARM_CPU_IRQ));
    sysbus_connect_irq(SYS_BUS_DEVICE(tmp), 1, qdev_get_gpio_in(DEVICE(cpu), ARM_CPU_FIQ));

    // TODO: AIC SYSC stub for IRQs

    tmp = qdev_create(NULL, TYPE_AT91_DBGU);
    qdev_prop_set_chr(tmp, "chardev", serial_hd(0));
    qdev_init_nofail(tmp);
    sysbus_mmio_map(SYS_BUS_DEVICE(tmp), 0, 0xFFFFF200);
    sysbus_connect_irq(SYS_BUS_DEVICE(tmp), 0, aic_irq[1]);

    sysbus_create_simple(TYPE_AT91_PMC,  0xFFFFFC00, NULL);

    // currently unimplemented things...
    create_unimplemented_device("iobc.internal.uhp",   0x00500000, 0x4000);

    create_unimplemented_device("iobc.ebi.cs2",        0x30000000, 0x10000000);
    create_unimplemented_device("iobc.ebi.cs3",        0x40000000, 0x10000000);
    create_unimplemented_device("iobc.ebi.cs4",        0x50000000, 0x10000000);
    create_unimplemented_device("iobc.ebi.cs5",        0x60000000, 0x10000000);
    create_unimplemented_device("iobc.ebi.cs6",        0x70000000, 0x10000000);
    create_unimplemented_device("iobc.ebi.cs7",        0x80000000, 0x10000000);

    create_unimplemented_device("iobc.periph.tc012",   0xFFFA0000, 0x4000);
    create_unimplemented_device("iobc.periph.udp",     0xFFFA4000, 0x4000);
    create_unimplemented_device("iobc.periph.mci",     0xFFFA8000, 0x4000);
    create_unimplemented_device("iobc.periph.twi",     0xFFFAC000, 0x4000);
    create_unimplemented_device("iobc.periph.usart0",  0xFFFB0000, 0x4000);
    create_unimplemented_device("iobc.periph.usart1",  0xFFFB4000, 0x4000);
    create_unimplemented_device("iobc.periph.usart2",  0xFFFB8000, 0x4000);
    create_unimplemented_device("iobc.periph.ssc",     0xFFFBC000, 0x4000);
    create_unimplemented_device("iobc.periph.isi",     0xFFFC0000, 0x4000);
    create_unimplemented_device("iobc.periph.emac",    0xFFFC4000, 0x4000);
    create_unimplemented_device("iobc.periph.spi0",    0xFFFC8000, 0x4000);
    create_unimplemented_device("iobc.periph.spi1",    0xFFFCC000, 0x4000);
    create_unimplemented_device("iobc.periph.usart3",  0xFFFD0000, 0x4000);
    create_unimplemented_device("iobc.periph.usart4",  0xFFFD4000, 0x4000);
    create_unimplemented_device("iobc.periph.usart5",  0xFFFD8000, 0x4000);
    create_unimplemented_device("iobc.periph.tc345",   0xFFFDC000, 0x4000);
    create_unimplemented_device("iobc.periph.adc",     0xFFFE0000, 0x4000);

    create_unimplemented_device("iobc.periph.ecc",     0xFFFFE800, 0x200);
    create_unimplemented_device("iobc.periph.sdramc",  0xFFFFEA00, 0x200);
    create_unimplemented_device("iobc.periph.smc",     0xFFFFEC00, 0x200);
    create_unimplemented_device("iobc.periph.matrix",  0xFFFFEE00, 0x200);
    create_unimplemented_device("iobc.periph.pioa",    0xFFFFF400, 0x200);
    create_unimplemented_device("iobc.periph.piob",    0xFFFFF600, 0x200);
    create_unimplemented_device("iobc.periph.pioc",    0xFFFFF800, 0x200);

    create_unimplemented_device("iobc.periph.rstc",    0xFFFFFD00, 0x10);
    create_unimplemented_device("iobc.periph.shdwc",   0xFFFFFD10, 0x10);
    create_unimplemented_device("iobc.periph.rtt",     0xFFFFFD20, 0x10);
    create_unimplemented_device("iobc.periph.pit",     0xFFFFFD30, 0x10);
    create_unimplemented_device("iobc.periph.wdt",     0xFFFFFD40, 0x10);
    create_unimplemented_device("iobc.periph.gpbr",    0xFFFFFD50, 0x10);

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
    } else {
        warn_report("No firmware specified: Use -bios <file> to load firmware");
    }

    arm_load_kernel(cpu, &iobc_board_binfo);
}

static void iobc_machine_init(MachineClass *mc)
{
    mc->desc = "ISIS-OBC for CubeSat";
    mc->init = iobc_init;
    mc->default_cpu_type = ARM_CPU_TYPE_NAME("arm926");
}

DEFINE_MACHINE("isis-obc", iobc_machine_init)
