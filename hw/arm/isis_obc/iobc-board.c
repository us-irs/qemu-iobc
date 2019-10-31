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
#include "at91-aic.h"
#include "at91-aic_stub.h"
#include "at91-dbgu.h"


static struct arm_boot_info iobc_board_binfo = {
    .loader_start     = 0x00000000,
    .ram_size         = 0x10000000,
    .nb_cpus          = 1,
};


typedef enum {
    AT91_BOOTMEM_ROM,
    AT91_BOOTMEM_SRAM,
    AT91_BOOTMEM_SDRAM,
    __AT91_BOOTMEM_NUM_REGIONS,
} at91_bootmem_region;

typedef struct {
    ARMCPU *cpu;

    MemoryRegion mem_boot[__AT91_BOOTMEM_NUM_REGIONS];
    MemoryRegion mem_rom;
    MemoryRegion mem_sram0;
    MemoryRegion mem_sram1;
    MemoryRegion mem_pflash;
    MemoryRegion mem_sdram;

    DeviceState *dev_pmc;
    DeviceState *dev_aic;
    DeviceState *dev_aic_stub;
    DeviceState *dev_rstc;
    DeviceState *dev_pit;
    DeviceState *dev_dbgu;
    DeviceState *dev_matrix;

    qemu_irq irq_aic[32];
    qemu_irq irq_sysc[32];

    at91_bootmem_region mem_boot_target;
} IobcBoardState;


static void iobc_init(MachineState *machine)
{
    MemoryRegion *address_space_mem = get_system_memory();
    IobcBoardState *s = g_new(IobcBoardState, 1);
    char *firmware_path;
    int i;

    s->cpu = ARM_CPU(cpu_create(machine->cpu_type));

    /* Memory Map for AT91SAM9G20 (current implementation status)                              */
    /*                                                                                         */
    /* start        length       description        notes                                      */
    /* --------------------------------------------------------------------------------------- */
    /* 0x0000_0000  0x0010_0000  Boot Memory        Aliases SDRAM at boot (set by hardware)    */
    /* 0x0010_0000  0x0000_8000  Internal ROM                                                  */
    /* 0x0020_0000  0x0000_4000  Internal SRAM0                                                */
    /* 0x0030_0000  0x0000_4000  Internal SRAM1                                                */
    /* ...                                                                                     */
    /*                                                                                         */
    /* 0x1000_0000  0x1000_0000  NOR Program Flash  Gets loaded with program code              */
    /* 0x2000_0000  0x1000_0000  SDRAM              Copied from NOR Flash at boot via hardware */
    /* ...                                                                                     */
    /*                                                                                         */
    /* ...                                                                                     */
    /* 0xFFFF_EE00  0x0000_0200  Matrix             TODO: Only minimal implementation for now  */
    /* 0xFFFF_F000  0x0000_0200  AIC                Uses stub to OR system controller IRQs     */
    /* 0xFFFF_F200  0x0000_0200  Debug Unit (DBGU)                                             */
    /* ...                                                                                     */
    /* 0xFFFF_FC00  0x0000_0100  PMC                                                           */
    /* 0xFFFF_FD00  0x0000_0010  RSTC               TODO: Only minimal implementation for now  */
    /* ...                                                                                     */
    /* 0xFFFF_FD30  0x0000_0010  PIT                TODO: Uses dummy clock frequency           */
    /* ...                                                                                     */

    // rom, ram, and flash
    memory_region_init_rom(&s->mem_rom,   NULL, "iobc.internal.rom",   0x8000, &error_fatal);
    memory_region_init_ram(&s->mem_sram0, NULL, "iobc.internal.sram0", 0x4000, &error_fatal);
    memory_region_init_ram(&s->mem_sram1, NULL, "iobc.internal.sram1", 0x4000, &error_fatal);

    memory_region_init_ram(&s->mem_pflash, NULL, "iobc.pflash", 0x10000000, &error_fatal);
    memory_region_init_ram(&s->mem_sdram,  NULL, "iobc.sdram",  0x10000000, &error_fatal);

    // bootmem aliases
    memory_region_init_alias(&s->mem_boot[AT91_BOOTMEM_ROM],   NULL, "iobc.internal.bootmem", &s->mem_rom,   0, 0x100000);
    memory_region_init_alias(&s->mem_boot[AT91_BOOTMEM_SRAM],  NULL, "iobc.internal.bootmem", &s->mem_sram0, 0, 0x100000);
    memory_region_init_alias(&s->mem_boot[AT91_BOOTMEM_SDRAM], NULL, "iobc.internal.bootmem", &s->mem_sdram, 0, 0x100000);

    // put it all together
    memory_region_add_subregion(address_space_mem, 0x00100000, &s->mem_rom);
    memory_region_add_subregion(address_space_mem, 0x00200000, &s->mem_sram0);
    memory_region_add_subregion(address_space_mem, 0x00300000, &s->mem_sram1);
    memory_region_add_subregion(address_space_mem, 0x10000000, &s->mem_pflash);
    memory_region_add_subregion(address_space_mem, 0x20000000, &s->mem_sdram);

    memory_region_transaction_begin();
    for (i = 0; i < __AT91_BOOTMEM_NUM_REGIONS; i++) {
        memory_region_set_enabled(&s->mem_boot[i], false);
        memory_region_add_subregion_overlap(address_space_mem, 0, &s->mem_boot[i], 1);
    }
    memory_region_transaction_commit();

    // map SDRAM to boot by default
    memory_region_set_enabled(&s->mem_boot[AT91_BOOTMEM_SDRAM], true);
    s->mem_boot_target = AT91_BOOTMEM_SDRAM;

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

    // Advanced Interrupt Controller
    s->dev_aic = qdev_create(NULL, TYPE_AT91_AIC);
    qdev_init_nofail(s->dev_aic);
    sysbus_mmio_map(SYS_BUS_DEVICE(s->dev_aic), 0, 0xFFFFF000);
    sysbus_connect_irq(SYS_BUS_DEVICE(s->dev_aic), 0, qdev_get_gpio_in(DEVICE(s->cpu), ARM_CPU_IRQ));
    sysbus_connect_irq(SYS_BUS_DEVICE(s->dev_aic), 1, qdev_get_gpio_in(DEVICE(s->cpu), ARM_CPU_FIQ));
    for (i = 0; i < 32; i++) {
        s->irq_aic[i] = qdev_get_gpio_in_named(s->dev_aic, "irq-line", i);
    }

    // Advanced Interrupt Controller: Stub for or-ing SYSC interrupts
    s->dev_aic_stub = qdev_create(NULL, TYPE_AT91_AIC_STUB);
    qdev_init_nofail(s->dev_aic_stub);
    sysbus_connect_irq(SYS_BUS_DEVICE(s->dev_aic_stub), 0, s->irq_aic[1]);
    for (i = 0; i < 32; i++) {
        s->irq_sysc[i] = qdev_get_gpio_in_named(s->dev_aic_stub, "irq-line", i);
    }

    // Debug Unit
    s->dev_dbgu = qdev_create(NULL, TYPE_AT91_DBGU);
    qdev_prop_set_chr(s->dev_dbgu, "chardev", serial_hd(0));
    qdev_init_nofail(s->dev_dbgu);
    sysbus_mmio_map(SYS_BUS_DEVICE(s->dev_dbgu), 0, 0xFFFFF200);
    sysbus_connect_irq(SYS_BUS_DEVICE(s->dev_dbgu), 0, s->irq_sysc[0]);

    // other peripherals
    sysbus_create_simple(TYPE_AT91_PMC,  0xFFFFFC00, s->irq_sysc[1]);

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
            if (load_image_mr(firmware_path, &s->mem_pflash) < 0) {
                error_report("Unable to load %s into pflash", bios_name);
                exit(1);
            }

            // nor flash gets copied to sdram at boot, thus we load it directly
            if (load_image_mr(firmware_path, &s->mem_sdram) < 0) {
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

    arm_load_kernel(s->cpu, &iobc_board_binfo);
}

static void iobc_machine_init(MachineClass *mc)
{
    mc->desc = "ISIS-OBC for CubeSat";
    mc->init = iobc_init;
    mc->default_cpu_type = ARM_CPU_TYPE_NAME("arm926");
}

DEFINE_MACHINE("isis-obc", iobc_machine_init)
