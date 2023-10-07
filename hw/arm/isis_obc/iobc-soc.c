#include "qemu/osdep.h"
#include "qapi/error.h"
#include "qemu/module.h"
#include "hw/char/serial.h"
#include "hw/sysbus.h"
#include "hw/misc/unimp.h"
#include "sysemu/sysemu.h"
#include "hw/boards.h"
#include "hw/loader.h"

#include "iobc-soc.h"

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
/* 0xFFFA_C000  0x0000_4000  TWI                TODO: Slave Mode                           */
/* 0xFFFB_0000  0x0000_4000  USART0                                                        */
/* 0xFFFB_4000  0x0000_4000  USART1                                                        */
/* 0xFFFB_8000  0x0000_4000  USART2                                                        */
/* ...                                                                                     */
/* 0xFFFC_8000  0x0000_4000  SPI0               TODO: slave mode, tx/cs delays             */
/* 0xFFFC_C000  0x0000_4000  SPI1               TODO: slave mode, tx/cs delays             */
/* 0xFFFD_0000  0x0000_4000  USART3                                                        */
/* 0xFFFD_4000  0x0000_4000  USART4                                                        */
/* 0xFFFD_8000  0x0000_4000  USART5                                                        */
/* ...                                                                                     */
/*                                                                                         */
/* ...                                                                                     */
/* 0xFFFF_EE00  0x0000_0200  Matrix             TODO: Only minimal implementation for now  */
/* 0xFFFF_F000  0x0000_0200  AIC                Uses stub to OR system controller IRQs     */
/* 0xFFFF_F200  0x0000_0200  Debug Unit (DBGU)  TODO: PDC/DMA support not implemented yet  */
/* 0xFFFF_F400  0x0000_0200  PIO A              TODO: Peripherals not connected yet        */
/* 0xFFFF_F600  0x0000_0200  PIO B              TODO: Peripherals not connected yet        */
/* 0xFFFF_F800  0x0000_0200  PIO C              TODO: Peripherals not connected yet        */
/* ...                                                                                     */
/* 0xFFFF_FC00  0x0000_0100  PMC                                                           */
/* 0xFFFF_FD00  0x0000_0010  RSTC               TODO: Only minimal implementation for now  */
/* ...                                                                                     */
/* 0xFFFF_FD20  0x0000_0010  RTT                                                           */
/* 0xFFFF_FD30  0x0000_0010  PIT                                                           */
/* ...                                                                                     */

void iobc_soc_remap_bootmem(IobcSoc *s, at91_bootmem_region target)
{
    static const char *memnames[] = {
        [AT91_BOOTMEM_ROM]      = "ROM",
        [AT91_BOOTMEM_SRAM0]    = "SRAM0",
        [AT91_BOOTMEM_EBI_NCS0] = "EBI_NCS0",
    };

    info_report("at91: remapping bootmem to %s", memnames[target]);

    memory_region_transaction_begin();
    memory_region_set_enabled(&s->mem_boot[s->mem_boot_target], false);
    memory_region_set_enabled(&s->mem_boot[target], true);
    s->mem_boot_target = target;
    memory_region_transaction_commit();
}

void iobc_soc_set_master_clock(IobcSoc *s, unsigned clock)
{
    info_report("at91 master clock changed: %d", clock);
    at91_pit_set_master_clock(&s->dev_pit, clock);
    at91_twi_set_master_clock(&s->dev_twi, clock);
    at91_usart_set_master_clock(&s->dev_usart0, clock);
    at91_usart_set_master_clock(&s->dev_usart1, clock);
    at91_usart_set_master_clock(&s->dev_usart2, clock);
    at91_usart_set_master_clock(&s->dev_usart3, clock);
    at91_usart_set_master_clock(&s->dev_usart4, clock);
    at91_usart_set_master_clock(&s->dev_usart5, clock);
    at91_spi_set_master_clock(&s->dev_spi0, clock);
    at91_spi_set_master_clock(&s->dev_spi1, clock);
    at91_mci_set_master_clock(&s->dev_mci, clock);
    at91_tc_set_master_clock(&s->dev_tc012, clock);
    at91_tc_set_master_clock(&s->dev_tc345, clock);
}

static void map_reserved_memory_region(ReservedMemory *mem, const char* name, hwaddr base, hwaddr size)
{
    qdev_prop_set_string(DEVICE(mem), "name", name);
    qdev_prop_set_uint64(DEVICE(mem), "size", size);

    sysbus_realize(SYS_BUS_DEVICE(mem), &error_abort);

    sysbus_mmio_map_overlap(SYS_BUS_DEVICE(mem), 0, base, -1000);
}

static void map_unimplemented_device(UnimplementedDeviceState *dev, const char *name, hwaddr base, hwaddr size)
{
    qdev_prop_set_string(DEVICE(dev), "name", name);
    qdev_prop_set_uint64(DEVICE(dev), "size", size);

    sysbus_realize(SYS_BUS_DEVICE(dev), &error_abort);

    sysbus_mmio_map_overlap(SYS_BUS_DEVICE(dev), 0, base, -1000);
}

static void iobc_soc_init(Object *obj)
{
    IobcSoc *s = IOBC_SOC(obj);

    // CPU
    object_initialize_child(obj, "cpu", &s->cpu, ARM_CPU_TYPE_NAME("arm926"));

    // reserved memory
    object_initialize_child(obj, "undefined",          &s->mem_undefined,          TYPE_IOBC_RESERVED_MEMORY);
    object_initialize_child(obj, "periph_reserved0",   &s->mem_periph_reserved0,   TYPE_IOBC_RESERVED_MEMORY);
    object_initialize_child(obj, "periph_reserved1",   &s->mem_periph_reserved1,   TYPE_IOBC_RESERVED_MEMORY);
    object_initialize_child(obj, "periph_reserved2",   &s->mem_periph_reserved2,   TYPE_IOBC_RESERVED_MEMORY);
    object_initialize_child(obj, "periph_reserved3",   &s->mem_periph_reserved3,   TYPE_IOBC_RESERVED_MEMORY);
    object_initialize_child(obj, "periph_reserved4",   &s->mem_periph_reserved4,   TYPE_IOBC_RESERVED_MEMORY);
    object_initialize_child(obj, "internal_reserved0", &s->mem_internal_reserved0, TYPE_IOBC_RESERVED_MEMORY);
    object_initialize_child(obj, "internal_reserved1", &s->mem_internal_reserved1, TYPE_IOBC_RESERVED_MEMORY);
    object_initialize_child(obj, "internal_reserved2", &s->mem_internal_reserved2, TYPE_IOBC_RESERVED_MEMORY);
    object_initialize_child(obj, "internal_reserved3", &s->mem_internal_reserved3, TYPE_IOBC_RESERVED_MEMORY);

    // Advanced Interrupt Controller
    object_initialize_child(obj, "aic", &s->dev_aic, TYPE_AT91_AIC);

    // Advanced Interrupt Controller: Stub for or-ing SYSC interrupts
    object_initialize_child(obj, "aic_stub", &s->dev_aic_stub, TYPE_AT91_AIC_STUB);

    // Power Managemant Controller
    object_initialize_child(obj, "pmc", &s->dev_pmc, TYPE_AT91_PMC);

    // Bus Matrix
    object_initialize_child(obj, "matrix", &s->dev_matrix, TYPE_AT91_MATRIX);

    // Debug Unit
    object_initialize_child(obj, "dbgu", &s->dev_dbgu, TYPE_AT91_DBGU);

    // Parallel Input Ouput Controllers
    object_initialize_child(obj, "pio_a", &s->dev_pio_a, TYPE_AT91_PIO);
    object_initialize_child(obj, "pio_b", &s->dev_pio_b, TYPE_AT91_PIO);
    object_initialize_child(obj, "pio_c", &s->dev_pio_c, TYPE_AT91_PIO);

    // TWI
    object_initialize_child(obj, "twi", &s->dev_twi, TYPE_AT91_TWI);

    // USARTs
    object_initialize_child(obj, "usart0", &s->dev_usart0, TYPE_AT91_USART);
    object_initialize_child(obj, "usart1", &s->dev_usart1, TYPE_AT91_USART);
    object_initialize_child(obj, "usart2", &s->dev_usart2, TYPE_AT91_USART);
    object_initialize_child(obj, "usart3", &s->dev_usart3, TYPE_AT91_USART);
    object_initialize_child(obj, "usart4", &s->dev_usart4, TYPE_AT91_USART);
    object_initialize_child(obj, "usart5", &s->dev_usart5, TYPE_AT91_USART);

    // SPIs
    object_initialize_child(obj, "spi0", &s->dev_spi0, TYPE_AT91_SPI);
    object_initialize_child(obj, "spi1", &s->dev_spi1, TYPE_AT91_SPI);

    // SDRAMC
    object_initialize_child(obj, "sdramc", &s->dev_sdramc, TYPE_AT91_SDRAMC);

    // MCI
    object_initialize_child(obj, "mci", &s->dev_mci, TYPE_AT91_MCI);

    // TCs
    object_initialize_child(obj, "tc012", &s->dev_tc012, TYPE_AT91_TC);
    object_initialize_child(obj, "tc345", &s->dev_tc345, TYPE_AT91_TC);

    // RSTC
    object_initialize_child(obj, "rstc", &s->dev_rstc, TYPE_AT91_RSTC);

    // RTT
    object_initialize_child(obj, "rtt", &s->dev_rtt, TYPE_AT91_RTT);

    // PIT
    object_initialize_child(obj, "pit", &s->dev_pit, TYPE_AT91_PIT);

    // currently unimplemented things...
    object_initialize_child(obj, "uhp",     &s->dev_uhp,     TYPE_UNIMPLEMENTED_DEVICE);
    object_initialize_child(obj, "ebi_cs2", &s->dev_ebi_cs2, TYPE_UNIMPLEMENTED_DEVICE);
    object_initialize_child(obj, "ebi_cs3", &s->dev_ebi_cs3, TYPE_UNIMPLEMENTED_DEVICE);
    object_initialize_child(obj, "ebi_cs4", &s->dev_ebi_cs4, TYPE_UNIMPLEMENTED_DEVICE);
    object_initialize_child(obj, "ebi_cs5", &s->dev_ebi_cs5, TYPE_UNIMPLEMENTED_DEVICE);
    object_initialize_child(obj, "ebi_cs6", &s->dev_ebi_cs6, TYPE_UNIMPLEMENTED_DEVICE);
    object_initialize_child(obj, "ebi_cs7", &s->dev_ebi_cs7, TYPE_UNIMPLEMENTED_DEVICE);
    object_initialize_child(obj, "udp",     &s->dev_udp,     TYPE_UNIMPLEMENTED_DEVICE);
    object_initialize_child(obj, "ssc",     &s->dev_ssc,     TYPE_UNIMPLEMENTED_DEVICE);
    object_initialize_child(obj, "isi",     &s->dev_isi,     TYPE_UNIMPLEMENTED_DEVICE);
    object_initialize_child(obj, "emac",    &s->dev_emac,    TYPE_UNIMPLEMENTED_DEVICE);
    object_initialize_child(obj, "adc",     &s->dev_adc,     TYPE_UNIMPLEMENTED_DEVICE);
    object_initialize_child(obj, "ecc",     &s->dev_ecc,     TYPE_UNIMPLEMENTED_DEVICE);
    object_initialize_child(obj, "smc",     &s->dev_smc,     TYPE_UNIMPLEMENTED_DEVICE);
    object_initialize_child(obj, "shdwc",   &s->dev_shdwc,   TYPE_UNIMPLEMENTED_DEVICE);
    object_initialize_child(obj, "wdt",     &s->dev_wdt,     TYPE_UNIMPLEMENTED_DEVICE);
    object_initialize_child(obj, "gpbr",    &s->dev_gpbr,    TYPE_UNIMPLEMENTED_DEVICE);
}

static void iobc_soc_realize(DeviceState *dev, Error **errp)
{
    IobcSoc *s = IOBC_SOC(dev);
    Object *obj = OBJECT(dev);
    int i;

    MemoryRegion *sys_mem = get_system_memory();

    // CPU
    if (!qdev_realize(DEVICE(&s->cpu), NULL, errp)) {
        return;
    }

    // rom, ram, and flash
    memory_region_init_rom(&s->mem_rom,   obj, "iobc.internal.rom",   0x8000, &error_fatal);
    memory_region_init_ram(&s->mem_sram0, obj, "iobc.internal.sram0", 0x4000, &error_fatal);
    memory_region_init_ram(&s->mem_sram1, obj, "iobc.internal.sram1", 0x4000, &error_fatal);

    memory_region_init_ram(&s->mem_pflash, obj, "iobc.pflash", 0x10000000, &error_fatal);
    memory_region_init_ram(&s->mem_sdram,  obj, "iobc.sdram",  0x10000000, &error_fatal);

    // bootmem aliases
    memory_region_init_alias(&s->mem_boot[AT91_BOOTMEM_ROM], obj, "iobc.internal.bootmem", &s->mem_rom, 0, 0x100000);
    memory_region_init_alias(&s->mem_boot[AT91_BOOTMEM_SRAM0], obj, "iobc.internal.bootmem", &s->mem_sram0, 0, 0x100000);
    memory_region_init_alias(&s->mem_boot[AT91_BOOTMEM_EBI_NCS0], obj, "iobc.internal.bootmem", &s->mem_pflash, 0, 0x100000);
    
    // put it all together
    memory_region_add_subregion(sys_mem, 0x00100000, &s->mem_rom);
    memory_region_add_subregion(sys_mem, 0x00200000, &s->mem_sram0);
    memory_region_add_subregion(sys_mem, 0x00300000, &s->mem_sram1);
    memory_region_add_subregion(sys_mem, 0x10000000, &s->mem_pflash);
    memory_region_add_subregion(sys_mem, 0x20000000, &s->mem_sdram);

    memory_region_transaction_begin();
    for (i = 0; i < __AT91_BOOTMEM_NUM_REGIONS; i++) {
        memory_region_set_enabled(&s->mem_boot[i], false);
        memory_region_add_subregion_overlap(sys_mem, 0, &s->mem_boot[i], 1);
    }
    memory_region_transaction_commit();

    // by default REMAP = 0, so initial bootmem mapping depends on BMS only
    s->mem_boot_target = AT91_BMS_INIT ? AT91_BOOTMEM_ROM : AT91_BOOTMEM_EBI_NCS0;
    memory_region_set_enabled(&s->mem_boot[s->mem_boot_target], true);

    // reserved memory
    map_reserved_memory_region(&s->mem_undefined, "iobc.undefined", 0x90000000, 0xF0000000 - 0x90000000);
    map_reserved_memory_region(&s->mem_periph_reserved0, "iobc.periph.reserved0", 0xF0000000, 0xFFFA0000 - 0xF0000000);
    map_reserved_memory_region(&s->mem_periph_reserved1, "iobc.periph.reserved1", 0xFFFE4000, 0xFFFFC000 - 0xFFFE4000);
    map_reserved_memory_region(&s->mem_periph_reserved2, "iobc.periph.reserved2", 0xFFFEC000, 0xFFFFE800 - 0xFFFEC000);
    map_reserved_memory_region(&s->mem_periph_reserved3, "iobc.periph.reserved3", 0xFFFFFA00, 0xFFFFFC00 - 0xFFFFFA00);
    map_reserved_memory_region(&s->mem_periph_reserved4, "iobc.periph.reserved4", 0xFFFFFD60, 0x2A0);
    map_reserved_memory_region(&s->mem_internal_reserved0, "iobc.internal.reserved0", 0x108000, 0x200000 - 0x108000);
    map_reserved_memory_region(&s->mem_internal_reserved1, "iobc.internal.reserved1", 0x204000, 0x300000 - 0x204000);
    map_reserved_memory_region(&s->mem_internal_reserved2, "iobc.internal.reserved2", 0x304000, 0x400000 - 0x304000);
    map_reserved_memory_region(&s->mem_internal_reserved3, "iobc.internal.reserved3", 0x504000, 0x0FFFFFFF - 0x504000);

    // Advanced Interrupt Controller
    if (!sysbus_realize(SYS_BUS_DEVICE(&s->dev_aic), errp)) {
        return;
    }
    sysbus_mmio_map(SYS_BUS_DEVICE(&s->dev_aic), 0, 0xFFFFF000);
    sysbus_connect_irq(SYS_BUS_DEVICE(&s->dev_aic), 0, qdev_get_gpio_in(DEVICE(&s->cpu), ARM_CPU_IRQ));
    sysbus_connect_irq(SYS_BUS_DEVICE(&s->dev_aic), 1, qdev_get_gpio_in(DEVICE(&s->cpu), ARM_CPU_FIQ));
    for (i = 0; i < 32; i++) {
        s->irq_aic[i] = qdev_get_gpio_in_named(DEVICE(&s->dev_aic), "irq-line", i);
    }

    // Advanced Interrupt Controller: Stub for or-ing SYSC interrupts
    if (!sysbus_realize(SYS_BUS_DEVICE(&s->dev_aic_stub), errp)) {
        return;
    }
    sysbus_connect_irq(SYS_BUS_DEVICE(&s->dev_aic_stub), 0, s->irq_aic[1]);
    for (i = 0; i < 32; i++) {
        s->irq_sysc[i] = qdev_get_gpio_in_named(DEVICE(&s->dev_aic_stub), "irq-line", i);
    }

    // Power Managemant Controller
    if (!sysbus_realize(SYS_BUS_DEVICE(&s->dev_pmc), errp)) {
        return;
    }
    sysbus_mmio_map(SYS_BUS_DEVICE(&s->dev_pmc), 0, 0xFFFFFC00);
    sysbus_connect_irq(SYS_BUS_DEVICE(&s->dev_pmc), 0, s->irq_sysc[0]);

    // Bus Matrix
    if (!sysbus_realize(SYS_BUS_DEVICE(&s->dev_matrix), errp)) {
        return;
    }
    sysbus_mmio_map(SYS_BUS_DEVICE(&s->dev_matrix), 0, 0xFFFFEE00);

    // Debug Unit
    if (!sysbus_realize(SYS_BUS_DEVICE(&s->dev_dbgu), errp)) {
        return;
    }
    sysbus_mmio_map(SYS_BUS_DEVICE(&s->dev_dbgu), 0, 0xFFFFF200);
    sysbus_connect_irq(SYS_BUS_DEVICE(&s->dev_dbgu), 0, s->irq_sysc[1]);

    // Parallel Input Ouput Controller
    if (!sysbus_realize(SYS_BUS_DEVICE(&s->dev_pio_a), errp)) {
        return;
    }
    sysbus_mmio_map(SYS_BUS_DEVICE(&s->dev_pio_a), 0, 0xFFFFF400);
    sysbus_connect_irq(SYS_BUS_DEVICE(&s->dev_pio_a), 0, s->irq_aic[2]);

    if (!sysbus_realize(SYS_BUS_DEVICE(&s->dev_pio_b), errp)) {
        return;
    }
    sysbus_mmio_map(SYS_BUS_DEVICE(&s->dev_pio_b), 0, 0xFFFFF600);
    sysbus_connect_irq(SYS_BUS_DEVICE(&s->dev_pio_b), 0, s->irq_aic[3]);

    if (!sysbus_realize(SYS_BUS_DEVICE(&s->dev_pio_c), errp)) {
        return;
    }
    sysbus_mmio_map(SYS_BUS_DEVICE(&s->dev_pio_c), 0, 0xFFFFF800);
    sysbus_connect_irq(SYS_BUS_DEVICE(&s->dev_pio_c), 0, s->irq_aic[4]);

    // TODO: connect PIO(A,B,C) peripheral pins

    // TWI
    if (!sysbus_realize(SYS_BUS_DEVICE(&s->dev_twi), errp)) {
        return;
    }
    sysbus_mmio_map(SYS_BUS_DEVICE(&s->dev_twi), 0, 0xFFFAC000);
    sysbus_connect_irq(SYS_BUS_DEVICE(&s->dev_twi), 0, s->irq_aic[11]);

    // USARTs
    if (!sysbus_realize(SYS_BUS_DEVICE(&s->dev_usart0), errp)) {
        return;
    }
    sysbus_mmio_map(SYS_BUS_DEVICE(&s->dev_usart0), 0, 0xFFFB0000);
    sysbus_connect_irq(SYS_BUS_DEVICE(&s->dev_usart0), 0, s->irq_aic[6]);

    if (!sysbus_realize(SYS_BUS_DEVICE(&s->dev_usart1), errp)) {
        return;
    }
    sysbus_mmio_map(SYS_BUS_DEVICE(&s->dev_usart1), 0, 0xFFFB4000);
    sysbus_connect_irq(SYS_BUS_DEVICE(&s->dev_usart1), 0, s->irq_aic[7]);

    if (!sysbus_realize(SYS_BUS_DEVICE(&s->dev_usart2), errp)) {
        return;
    }
    sysbus_mmio_map(SYS_BUS_DEVICE(&s->dev_usart2), 0, 0xFFFB8000);
    sysbus_connect_irq(SYS_BUS_DEVICE(&s->dev_usart2), 0, s->irq_aic[8]);

    if (!sysbus_realize(SYS_BUS_DEVICE(&s->dev_usart3), errp)) {
        return;
    }
    sysbus_mmio_map(SYS_BUS_DEVICE(&s->dev_usart3), 0, 0xFFFD0000);
    sysbus_connect_irq(SYS_BUS_DEVICE(&s->dev_usart3), 0, s->irq_aic[23]);

    if (!sysbus_realize(SYS_BUS_DEVICE(&s->dev_usart4), errp)) {
        return;
    }
    sysbus_mmio_map(SYS_BUS_DEVICE(&s->dev_usart4), 0, 0xFFFD4000);
    sysbus_connect_irq(SYS_BUS_DEVICE(&s->dev_usart4), 0, s->irq_aic[24]);

    if (!sysbus_realize(SYS_BUS_DEVICE(&s->dev_usart5), errp)) {
        return;
    }
    sysbus_mmio_map(SYS_BUS_DEVICE(&s->dev_usart5), 0, 0xFFFD8000);
    sysbus_connect_irq(SYS_BUS_DEVICE(&s->dev_usart5), 0, s->irq_aic[25]);

    // SPIs
    if (!sysbus_realize(SYS_BUS_DEVICE(&s->dev_spi0), errp)) {
        return;
    }
    sysbus_mmio_map(SYS_BUS_DEVICE(&s->dev_spi0), 0, 0xFFFC8000);
    sysbus_connect_irq(SYS_BUS_DEVICE(&s->dev_spi0), 0, s->irq_aic[12]);

    if (!sysbus_realize(SYS_BUS_DEVICE(&s->dev_spi1), errp)) {
        return;
    }
    sysbus_mmio_map(SYS_BUS_DEVICE(&s->dev_spi1), 0, 0xFFFCC000);
    sysbus_connect_irq(SYS_BUS_DEVICE(&s->dev_spi1), 0, s->irq_aic[13]);

    // SDRAMC
    if (!sysbus_realize(SYS_BUS_DEVICE(&s->dev_sdramc), errp)) {
        return;
    }
    sysbus_mmio_map(SYS_BUS_DEVICE(&s->dev_sdramc), 0, 0xFFFFEA00);
    sysbus_connect_irq(SYS_BUS_DEVICE(&s->dev_sdramc), 0, s->irq_sysc[2]);

    // MCI
    if (!sysbus_realize(SYS_BUS_DEVICE(&s->dev_mci), errp)) {
        return;
    }
    sysbus_mmio_map(SYS_BUS_DEVICE(&s->dev_mci), 0, 0xFFFA8000);
    sysbus_connect_irq(SYS_BUS_DEVICE(&s->dev_mci), 0, s->irq_aic[9]);
    qdev_connect_gpio_out_named(DEVICE(&s->dev_pio_b), "pin.out", 7, qdev_get_gpio_in_named(DEVICE(&s->dev_mci), "select", 0));

    // TC0, TC1, TC2
    if (!sysbus_realize(SYS_BUS_DEVICE(&s->dev_tc012), errp)) {
        return;
    }
    sysbus_mmio_map(SYS_BUS_DEVICE(&s->dev_tc012), 0, 0xFFFA0000);
    sysbus_connect_irq(SYS_BUS_DEVICE(&s->dev_tc012), 0, s->irq_aic[17]);
    sysbus_connect_irq(SYS_BUS_DEVICE(&s->dev_tc012), 1, s->irq_aic[18]);
    sysbus_connect_irq(SYS_BUS_DEVICE(&s->dev_tc012), 2, s->irq_aic[19]);

    // TC3, TC4, TC5
    if (!sysbus_realize(SYS_BUS_DEVICE(&s->dev_tc345), errp)) {
        return;
    }
    sysbus_mmio_map(SYS_BUS_DEVICE(&s->dev_tc345), 0, 0xFFFDC000);
    sysbus_connect_irq(SYS_BUS_DEVICE(&s->dev_tc345), 0, s->irq_aic[26]);
    sysbus_connect_irq(SYS_BUS_DEVICE(&s->dev_tc345), 1, s->irq_aic[27]);
    sysbus_connect_irq(SYS_BUS_DEVICE(&s->dev_tc345), 2, s->irq_aic[28]);

    // RSTC
    if (!sysbus_realize(SYS_BUS_DEVICE(&s->dev_rstc), errp)) {
        return;
    }
    sysbus_mmio_map(SYS_BUS_DEVICE(&s->dev_rstc), 0, 0xFFFFFD00);
    sysbus_connect_irq(SYS_BUS_DEVICE(&s->dev_rstc), 0, s->irq_sysc[3]);

    // RTT
    if (!sysbus_realize(SYS_BUS_DEVICE(&s->dev_rtt), errp)) {
        return;
    }
    sysbus_mmio_map(SYS_BUS_DEVICE(&s->dev_rtt), 0, 0xFFFFFD20);
    sysbus_connect_irq(SYS_BUS_DEVICE(&s->dev_rtt), 0, s->irq_sysc[4]);

    // PIT
    if (!sysbus_realize(SYS_BUS_DEVICE(&s->dev_pit), errp)) {
        return;
    }
    sysbus_mmio_map(SYS_BUS_DEVICE(&s->dev_pit), 0, 0xFFFFFD30);
    sysbus_connect_irq(SYS_BUS_DEVICE(&s->dev_pit), 0, s->irq_sysc[5]);

    // currently unimplemented things...
    map_unimplemented_device(&s->dev_uhp,     "iobc.internal.uhp", 0x00500000, 0x4000);
    map_unimplemented_device(&s->dev_ebi_cs2, "iobc.ebi.cs2",      0x30000000, 0x10000000);
    map_unimplemented_device(&s->dev_ebi_cs3, "iobc.ebi.cs3",      0x40000000, 0x10000000);
    map_unimplemented_device(&s->dev_ebi_cs4, "iobc.ebi.cs4",      0x50000000, 0x10000000);
    map_unimplemented_device(&s->dev_ebi_cs5, "iobc.ebi.cs5",      0x60000000, 0x10000000);
    map_unimplemented_device(&s->dev_ebi_cs6, "iobc.ebi.cs6",      0x70000000, 0x10000000);
    map_unimplemented_device(&s->dev_ebi_cs7, "iobc.ebi.cs7",      0x80000000, 0x10000000);
    map_unimplemented_device(&s->dev_udp,     "iobc.periph.udp",   0xFFFA4000, 0x4000);
    map_unimplemented_device(&s->dev_ssc,     "iobc.periph.ssc",   0xFFFBC000, 0x4000);
    map_unimplemented_device(&s->dev_isi,     "iobc.periph.isi",   0xFFFC0000, 0x4000);
    map_unimplemented_device(&s->dev_emac,    "iobc.periph.emac",  0xFFFC4000, 0x4000);
    map_unimplemented_device(&s->dev_adc,     "iobc.periph.adc",   0xFFFE0000, 0x4000);
    map_unimplemented_device(&s->dev_ecc,     "iobc.periph.ecc",   0xFFFFE800, 0x200);
    map_unimplemented_device(&s->dev_smc,     "iobc.periph.smc",   0xFFFFEC00, 0x200);
    map_unimplemented_device(&s->dev_shdwc,   "iobc.periph.shdwc", 0xFFFFFD10, 0x10);
    map_unimplemented_device(&s->dev_wdt,     "iobc.periph.wdt",   0xFFFFFD40, 0x10);
    map_unimplemented_device(&s->dev_gpbr,    "iobc.periph.gpbr",  0xFFFFFD50, 0x10);
}

static void iobc_soc_class_init(ObjectClass *oc, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(oc);
    dc->realize = iobc_soc_realize;
}

static const TypeInfo iobc_soc_device_info = {
    .name = TYPE_IOBC_SOC,
    .parent = TYPE_DEVICE,
    .instance_size = sizeof(IobcSoc),
    .instance_init = iobc_soc_init,
    .class_init = iobc_soc_class_init,
};

static void iobc_soc_register_types(void)
{
    type_register_static(&iobc_soc_device_info);
}

type_init(iobc_soc_register_types)
