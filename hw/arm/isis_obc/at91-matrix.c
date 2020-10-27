/*
 * AT91 Bus Matrix.
 *
 * See at91-matrix.h for details.
 *
 * Copyright (c) 2019-2020 KSat e.V. Stuttgart
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or, at your
 * option, any later version. See the COPYING file in the top-level directory.
 */

// Overview of TODOs:
// - only switching between SRAM and SDRAM for boot memory supported (nothing
//   else)

#include "at91-matrix.h"
#include "qemu/error-report.h"

#define MATRIX_MCFG0        0x000
#define MATRIX_MCFG4        0x010
#define MATRIX_MCFG5        0x014
#define MATRIX_MCFG_STRIDE  4
#define MATRIX_SCFG0        0x040
#define MATRIX_SCFG4        0x050
#define MATRIX_SCFG_STRIDE  4
#define MATRIX_PRAS0        0x080
#define MATRIX_PRAS4        0x0A0
#define MATRIX_PRAS_STRIDE  8
#define MATRIX_MRCR         0x100
#define EBI_CSA             0x11C

#define MRCR_RCB0           BIT(0)
#define MRCR_RCB1           BIT(1)


inline static void matrix_bootmem_remap(MatrixState *s, at91_bootmem_region target)
{
    if (s->bootmem_cb)
        s->bootmem_cb(s->bootmem_opaque, target);
}

inline static void matrix_bootmem_update(MatrixState *s)
{
    // RCB0: Remap Command Bit for AHB Master 0 (ARM926 Instruction)
    // RCB1: Remap Command Bit for AHB Master 1 (ARM926 Data)

    if ((s->reg_mrcr & MRCR_RCB0) && (s->reg_mrcr & MRCR_RCB1)) {           // REMAP = 1
        matrix_bootmem_remap(s, AT91_BOOTMEM_SRAM0);

    } else if (!(s->reg_mrcr & MRCR_RCB0) && !(s->reg_mrcr & MRCR_RCB1)) {  // REMAP = 0
        if (s->bms) {                                                       // BMS = 1
            matrix_bootmem_remap(s, AT91_BOOTMEM_ROM);
        } else {                                                            // BMS = 0
            matrix_bootmem_remap(s, AT91_BOOTMEM_EBI_NCS0);
        }

    } else {
        /*
         * QEMU doesn't allow us to remap data indpeendently from
         * instructions. For QEMU, both are the same. So we can only make
         * this a hard error to catch it in case this happens...
         */
        error_report("at91.matrix: cannot set REMAP independently for Data and Instruction");
        abort();
    }
}


static uint64_t matrix_mmio_read(void *opaque, hwaddr offset, unsigned size)
{
    MatrixState *s = opaque;

    info_report("at91.matrix: read access at 0x%02lx with size: 0x%02x", offset, size);

    switch (offset) {
    case MATRIX_MCFG0 ... MATRIX_MCFG4:
        if ((offset - MATRIX_MCFG0) % MATRIX_MCFG_STRIDE != 0)
            break;

        return s->reg_mcfg[(offset - MATRIX_MCFG0) / MATRIX_MCFG_STRIDE];

    case MATRIX_SCFG0 ... MATRIX_SCFG4:
        if ((offset - MATRIX_SCFG0) % MATRIX_SCFG_STRIDE != 0)
            break;

        return s->reg_scfg[(offset - MATRIX_SCFG0) / MATRIX_SCFG_STRIDE];

    case MATRIX_PRAS0 ... MATRIX_PRAS4:
        if ((offset - MATRIX_PRAS0) % MATRIX_PRAS_STRIDE != 0)
            break;

        return s->reg_pras[(offset - MATRIX_PRAS0) / MATRIX_PRAS_STRIDE];

    case MATRIX_MRCR:
        return s->reg_mrcr;

    case EBI_CSA:
        return s->reg_ebi_csa;
    }

    error_report("at91.matrix: illegal/unimplemented read access at 0x%02lx", offset);
    abort();
}

static void matrix_mmio_write(void *opaque, hwaddr offset, uint64_t value, unsigned size)
{
    MatrixState *s = opaque;

    info_report("at91.matrix: write access at 0x%02lx with size: 0x%02x [value: 0x%08lx]",
                offset, size, value);

    switch (offset) {
    case MATRIX_MCFG0 ... MATRIX_MCFG5:
        if ((offset - MATRIX_MCFG0) % MATRIX_MCFG_STRIDE != 0)
            break;

        s->reg_mcfg[(offset - MATRIX_MCFG0) / MATRIX_MCFG_STRIDE] = value;
        // TODO
        return;

    case MATRIX_SCFG0 ... MATRIX_SCFG4:
        if ((offset - MATRIX_SCFG0) % MATRIX_SCFG_STRIDE != 0)
            break;

        s->reg_scfg[(offset - MATRIX_SCFG0) / MATRIX_SCFG_STRIDE] = value;
        // TODO
        return;

    case MATRIX_PRAS0 ... MATRIX_PRAS4:
        if ((offset - MATRIX_PRAS0) % MATRIX_PRAS_STRIDE != 0)
            break;

        s->reg_pras[(offset - MATRIX_PRAS0) / MATRIX_PRAS_STRIDE] = value;
        // TODO
        return;

    case MATRIX_MRCR:
        s->reg_mrcr = value;
        matrix_bootmem_update(s);
        return;

    case EBI_CSA:
        s->reg_ebi_csa = value;
        // TODO
        return;
    }

    error_report("at91.matrix: illegal/unimplemented write access at 0x%02lx [value; 0x%08lx]",
                     offset, value);
    abort();
}

static const MemoryRegionOps matrix_mmio_ops = {
    .read = matrix_mmio_read,
    .write = matrix_mmio_write,
    .impl.min_access_size = 4,
    .impl.max_access_size = 4,
    .valid.min_access_size = 4,
    .valid.max_access_size = 4,
    .endianness = DEVICE_NATIVE_ENDIAN,
};


static void matrix_device_init(Object *obj)
{
    MatrixState *s = AT91_MATRIX(obj);

    memory_region_init_io(&s->mmio, OBJECT(s), &matrix_mmio_ops, s, "at91.matrix", 0x200);
    sysbus_init_mmio(SYS_BUS_DEVICE(s), &s->mmio);
}

static void matrix_reset_registers(MatrixState *s)
{
    s->reg_mcfg[0] = 0x00;
    s->reg_mcfg[1] = 0x02;
    s->reg_mcfg[2] = 0x02;
    s->reg_mcfg[3] = 0x02;
    s->reg_mcfg[4] = 0x02;
    s->reg_mcfg[5] = 0x02;

    s->reg_scfg[0] = 0x10;
    s->reg_scfg[1] = 0x10;
    s->reg_scfg[2] = 0x10;
    s->reg_scfg[3] = 0x10;
    s->reg_scfg[4] = 0x10;

    s->reg_pras[0] = 0x00;
    s->reg_pras[1] = 0x00;
    s->reg_pras[2] = 0x00;
    s->reg_pras[3] = 0x00;
    s->reg_pras[4] = 0x00;

    s->reg_mrcr = 0;
    s->reg_ebi_csa = 0x00010000;
}

static void matrix_device_realize(DeviceState *dev, Error **errp)
{
    MatrixState *s = AT91_MATRIX(dev);

    matrix_reset_registers(s);
    s->bms = AT91_BMS_INIT;
}

static void matrix_device_reset(DeviceState *dev)
{
    MatrixState *s = AT91_MATRIX(dev);

    matrix_reset_registers(s);
    matrix_bootmem_update(s);
}

static void matrix_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    dc->realize = matrix_device_realize;
    dc->reset = matrix_device_reset;
}

static const TypeInfo matrix_device_info = {
    .name = TYPE_AT91_MATRIX,
    .parent = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(MatrixState),
    .instance_init = matrix_device_init,
    .class_init = matrix_class_init,
};

static void matrix_register_types(void)
{
    type_register_static(&matrix_device_info);
}

type_init(matrix_register_types)
