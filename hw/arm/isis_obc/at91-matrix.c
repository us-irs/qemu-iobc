/*
 * AT91 Bus Matrix.
 *
 * See at91-matrix.h for details.
 */

// Overview of TODOs:
// - only switching between SRAM and SDRAM for boot memory supported (nothing
//   else)

#include "at91-matrix.h"
#include "qemu/error-report.h"


#define MATRIX_MRCR 0x100

#define MRCR_RCB0   1
#define MRCR_RCB1   2


inline static void matrix_bootmem_remap(MatrixState *s, at91_bootmem_region target)
{
    if (s->bootmem_cb)
        s->bootmem_cb(s->bootmem_opaque, target);
}


static uint64_t matrix_mmio_read(void *opaque, hwaddr offset, unsigned size)
{
    MatrixState *s = opaque;

    info_report("at91.matrix: read access at 0x%02lx with size: 0x%02x", offset, size);

    switch (offset) {
    case MATRIX_MRCR:
        return s->reg_mrcr;

    // TODO

    default:
        error_report("at91.matrix: illegal/unimplemented read access at 0x%02lx", offset);
        abort();
    }
}

static void matrix_mmio_write(void *opaque, hwaddr offset, uint64_t value, unsigned size)
{
    MatrixState *s = opaque;

    info_report("at91.matrix: write access at 0x%02lx with size: 0x%02x [value: 0x%08lx]",
                offset, size, value);

    switch (offset) {
    case MATRIX_MRCR:
        s->reg_mrcr = value;

        // RCB0: Remap Command Bit for AHB Master 0 (ARM926 Instruction)
        // RCB1: Remap Command Bit for AHB Master 1 (ARM926 Data)

        if ((value & MRCR_RCB0) && (value & MRCR_RCB1)) {
            matrix_bootmem_remap(s, AT91_BOOTMEM_SRAM0);

        } else if (!(value & MRCR_RCB0) && !(value & MRCR_RCB1)) {
            // TODO: switch between rom and EBI_NCS0 (SDRAM) based on BMS
            matrix_bootmem_remap(s, AT91_BOOTMEM_EBI_NCS0);

        } else {
            /*
             * QEMU doesn't allow us to remap data indpeendently from
             * instructions. For QEMU, both are the same. So we can only make
             * this a hard error to catch it in case this happens...
             */
            error_report("at91.matrix: cannot set REMAP independently for Data and Instruction");
            abort();
        }
        break;

    // TODO

    default:
        error_report("at91.matrix: illegal/unimplemented write access at 0x%02lx [value; 0x%08lx]",
                     offset, value);
        abort();
    }
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

static void matrix_device_realize(DeviceState *dev, Error **errp)
{
    MatrixState *s = AT91_MATRIX(dev);
    s->bms = AT91_BMS_INIT;
    s->reg_mrcr = 0;
}

static void matrix_device_reset(DeviceState *dev)
{
    MatrixState *s = AT91_MATRIX(dev);
    s->reg_mrcr = 0;
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
