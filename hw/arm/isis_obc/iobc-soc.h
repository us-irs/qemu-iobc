/*
 * IOBC AT91 SoC.
 *
 * Copyright (c) 2023 KSat e.V. Stuttgart
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or, at your
 * option, any later version. See the COPYING file in the top-level directory.
 */

#ifndef HW_ARM_ISIS_OBC_SOC_H
#define HW_ARM_ISIS_OBC_SOC_H

#include "qemu/osdep.h"
#include "qemu/error-report.h"
#include "qapi/error.h"
#include "hw/hw.h"
#include "hw/loader.h"
#include "hw/boards.h"
#include "hw/arm/boot.h"
#include "hw/misc/unimp.h"
#include "sysemu/sysemu.h"
#include "cpu.h"

#include "iobc-reserved_memory.h"
#include "at91-pmc.h"
#include "at91-aic.h"
#include "at91-aic_stub.h"
#include "at91-dbgu.h"
#include "at91-rtt.h"
#include "at91-pit.h"
#include "at91-matrix.h"
#include "at91-rstc.h"
#include "at91-pio.h"
#include "at91-usart.h"
#include "at91-twi.h"
#include "at91-spi.h"
#include "at91-sdramc.h"
#include "at91-mci.h"
#include "at91-tc.h"

#define TYPE_IOBC_SOC "iobc-soc"
OBJECT_DECLARE_SIMPLE_TYPE(IobcSoc, IOBC_SOC)

struct IobcSoc {
    DeviceState parent_obj;

    ARMCPU cpu;

    MemoryRegion mem_boot[__AT91_BOOTMEM_NUM_REGIONS];
    MemoryRegion mem_rom;
    MemoryRegion mem_sram0;
    MemoryRegion mem_sram1;
    MemoryRegion mem_pflash;
    MemoryRegion mem_sdram;

    ReservedMemory mem_undefined;
    ReservedMemory mem_periph_reserved0;
    ReservedMemory mem_periph_reserved1;
    ReservedMemory mem_periph_reserved2;
    ReservedMemory mem_periph_reserved3;
    ReservedMemory mem_periph_reserved4;
    ReservedMemory mem_internal_reserved0;
    ReservedMemory mem_internal_reserved1;
    ReservedMemory mem_internal_reserved2;
    ReservedMemory mem_internal_reserved3;

    At91Pmc dev_pmc;
    At91Aic dev_aic;
    At91AicStub dev_aic_stub;
    At91Rstc dev_rstc;
    At91Rtt dev_rtt;
    At91Pit dev_pit;
    At91Dbgu dev_dbgu;
    At91Matrix dev_matrix;
    At91Pio dev_pio_a;
    At91Pio dev_pio_b;
    At91Pio dev_pio_c;
    At91Usart dev_usart0;
    At91Usart dev_usart1;
    At91Usart dev_usart2;
    At91Usart dev_usart3;
    At91Usart dev_usart4;
    At91Usart dev_usart5;
    At91Spi dev_spi0;
    At91Spi dev_spi1;
    At91Twi dev_twi;
    At91Sdramc dev_sdramc;
    At91Mci dev_mci;
    At91Tc dev_tc012;
    At91Tc dev_tc345;

    UnimplementedDeviceState dev_uhp;
    UnimplementedDeviceState dev_ebi_cs2;
    UnimplementedDeviceState dev_ebi_cs3;
    UnimplementedDeviceState dev_ebi_cs4;
    UnimplementedDeviceState dev_ebi_cs5;
    UnimplementedDeviceState dev_ebi_cs6;
    UnimplementedDeviceState dev_ebi_cs7;
    UnimplementedDeviceState dev_udp;
    UnimplementedDeviceState dev_ssc;
    UnimplementedDeviceState dev_isi;
    UnimplementedDeviceState dev_emac;
    UnimplementedDeviceState dev_adc;
    UnimplementedDeviceState dev_ecc;
    UnimplementedDeviceState dev_smc;
    UnimplementedDeviceState dev_shdwc;
    UnimplementedDeviceState dev_wdt;
    UnimplementedDeviceState dev_gpbr;

    qemu_irq irq_aic[32];
    qemu_irq irq_sysc[32];

    at91_bootmem_region mem_boot_target;
};

void iobc_soc_remap_bootmem(IobcSoc *s, at91_bootmem_region target);
void iobc_soc_set_master_clock(IobcSoc *s, unsigned clock);

#endif /* HW_ARM_ISIS_OBC_SOC_H */
