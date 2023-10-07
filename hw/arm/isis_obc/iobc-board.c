/*
 * ISIS iOBC.
 *
 * Main board file for the ISIS iOBC board with AT91-SAM chip.
 * See iobc_init function for connected devices and device setup.
 *
 * Copyright (c) 2019-2020 KSat e.V. Stuttgart
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or, at your
 * option, any later version. See the COPYING file in the top-level directory.
 */

#include "qemu/osdep.h"
#include "qemu/datadir.h"
#include "qemu/error-report.h"
#include "qapi/error.h"
#include "hw/hw.h"
#include "hw/loader.h"
#include "hw/boards.h"
#include "hw/arm/boot.h"
#include "hw/misc/unimp.h"
#include "sysemu/sysemu.h"
#include "cpu.h"

#include "iobc-soc.h"


#define SOCKET_TWI      "/tmp/qemu_at91_twi"
#define SOCKET_USART0   "/tmp/qemu_at91_usart0"
#define SOCKET_USART1   "/tmp/qemu_at91_usart1"
#define SOCKET_USART2   "/tmp/qemu_at91_usart2"
#define SOCKET_USART3   "/tmp/qemu_at91_usart3"
#define SOCKET_USART4   "/tmp/qemu_at91_usart4"
#define SOCKET_USART5   "/tmp/qemu_at91_usart5"
#define SOCKET_SPI0     "/tmp/qemu_at91_spi0"
#define SOCKET_SPI1     "/tmp/qemu_at91_spi1"
#define SOCKET_PIOA     "/tmp/qemu_at91_pioa"
#define SOCKET_PIOB     "/tmp/qemu_at91_piob"
#define SOCKET_PIOC     "/tmp/qemu_at91_pioc"
#define SOCKET_SDRAMC   "/tmp/qemu_at91_sdramc"

#define ADDR_BOOTMEM    0x00000000
#define ADDR_SDRAMC     0x20000000


#define IOBC_LOADER_NONE    0
#define IOBC_LOADER_DBG     1

#define IOBC_LOADER         IOBC_LOADER_NONE


#if IOBC_LOADER == IOBC_LOADER_DBG

#define IOBC_START_ADDRESS  ADDR_SDRAMC

static const At91PmcInitState pmc_init_state_sdram = {
    .reg_ckgr_mor     = 0x00004001,
    .reg_ckgr_plla    = 0x202a3f01,
    .reg_ckgr_pllb    = 0x10193f05,
    .reg_pmc_mckr     = 0x00001302,
};

#else /* IOBC_LOADER */

#define IOBC_START_ADDRESS  ADDR_BOOTMEM

#endif /* IOBC_LOADER */

static struct arm_boot_info iobc_board_binfo = {
    .loader_start     = IOBC_START_ADDRESS,
    .ram_size         = 0x10000000,
};

static void iobc_cb_bootmem_remap(void *opaque, at91_bootmem_region target)
{
    IobcSoc *s = opaque;
    iobc_soc_remap_bootmem(s, target);
}

static void iobc_cb_mclk_changed(void *opaque, unsigned clock)
{
    IobcSoc *s = opaque;
    iobc_soc_set_master_clock(s, clock);
}

static void iobc_init(MachineState *machine)
{
    IobcSoc *soc;
    Error *err = NULL;

    // only allow ARM926 for this board
    if (strcmp(machine->cpu_type, ARM_CPU_TYPE_NAME("arm926")) != 0) {
        error_report("This board can only be used with arm926 CPU");
        exit(1);
    }

    // initialize SoC device
    soc = IOBC_SOC(object_new(TYPE_IOBC_SOC));
    object_property_add_child(OBJECT(machine), "soc", OBJECT(soc));
    object_unref(OBJECT(soc));

    // set device properties
    qdev_prop_set_chr(DEVICE(&soc->dev_dbgu), "chardev", serial_hd(0));
    qdev_prop_set_string(DEVICE(&soc->dev_pio_a), "socket", SOCKET_PIOA);
    qdev_prop_set_string(DEVICE(&soc->dev_pio_b), "socket", SOCKET_PIOB);
    qdev_prop_set_string(DEVICE(&soc->dev_pio_c), "socket", SOCKET_PIOC);
    qdev_prop_set_string(DEVICE(&soc->dev_twi), "socket", SOCKET_TWI);
    qdev_prop_set_string(DEVICE(&soc->dev_usart0), "socket", SOCKET_USART0);
    qdev_prop_set_string(DEVICE(&soc->dev_usart1), "socket", SOCKET_USART1);
    qdev_prop_set_string(DEVICE(&soc->dev_usart2), "socket", SOCKET_USART2);
    qdev_prop_set_string(DEVICE(&soc->dev_usart3), "socket", SOCKET_USART3);
    qdev_prop_set_string(DEVICE(&soc->dev_usart4), "socket", SOCKET_USART4);
    qdev_prop_set_string(DEVICE(&soc->dev_usart5), "socket", SOCKET_USART5);
    qdev_prop_set_string(DEVICE(&soc->dev_spi0), "socket", SOCKET_SPI0);
    qdev_prop_set_string(DEVICE(&soc->dev_spi1), "socket", SOCKET_SPI1);
    qdev_prop_set_string(DEVICE(&soc->dev_sdramc), "socket", SOCKET_SDRAMC);

    // set callbacks
    at91_pmc_set_mclk_change_callback(&soc->dev_pmc, soc, iobc_cb_mclk_changed);
    at91_matrix_set_bootmem_remap_callback(&soc->dev_matrix, soc, iobc_cb_bootmem_remap);

    // realize SoC device
    if (!qdev_realize(DEVICE(soc), NULL, &err)) {
        error_reportf_err(err, "Couldn't realize IOBC SoC: ");
        exit(1);
    }

#if IOBC_LOADER == IOBC_LOADER_DBG
    char *firmware_path;

    /*
     * Load firmware directly to SDRAMC.
     *
     * Note: This is the "debug" way, i.e. load to SDRAMC and jump to SDRAMC start address.
     *       This bypasses the bootloader and configures the clock for OBSW, which in debug
     *       loading on real hardware is done via jlink.
     */
    if (machine->firmware) {
        firmware_path = qemu_find_file(QEMU_FILE_TYPE_BIOS, machine->firmware);

        if (firmware_path) {
            if (load_image_mr(firmware_path, &soc->mem_sdram) < 0) {
                error_report("Unable to load %s into sdram", machine->firmware);
                exit(1);
            }

            at91_pmc_set_init_state(AT91_PMC(&soc->dev_pmc), &pmc_init_state_sdram);

            g_free(firmware_path);
        } else {
            error_report("Unable to find %s", machine->firmware);
            exit(1);
        }
    } else {
        warn_report("No firmware specified: Use -bios <file> to load firmware");
    }
#endif

    arm_load_kernel(&soc->cpu, machine, &iobc_board_binfo);
}

static void iobc_machine_init(MachineClass *mc)
{
    mc->desc = "ISIS-OBC for CubeSat";
    mc->init = iobc_init;
    mc->default_cpu_type = ARM_CPU_TYPE_NAME("arm926");
}

DEFINE_MACHINE("isis-obc", iobc_machine_init)
