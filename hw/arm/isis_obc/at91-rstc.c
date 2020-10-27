/*
 * AT91 Reset Controller.
 *
 * See at91-rstc.h for details.
 *
 * Copyright (c) 2019-2020 KSat e.V. Stuttgart
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or, at your
 * option, any later version. See the COPYING file in the top-level directory.
 */

// Overview of TODOs:
// - processor reset not implemented (calling it currently does nothing)
// - peripheral reset not implemented (calling it currently does nothing)
// - external reset not implemented (calling it currently does nothing)

#include "at91-rstc.h"
#include "qemu/error-report.h"
#include "hw/irq.h"

#define RSTC_KEY_PASSWORD   0xa5

#define RSTC_CR     0x00
#define RSTC_SR     0x04
#define RSTC_MR     0x08

#define CR_PROCRST  1
#define CR_PERRST   (1 << 2)
#define CR_EXTRST   (1 << 3)

#define SR_URSTS    1
#define SR_NRSTL    (1 << 16)
#define SR_SRCMP    (1 << 17)

#define MR_URSTIEN  (1 << 4)


static void rstc_update_irq(RstcState *s)
{
    qemu_set_irq(s->irq, (s->reg_mr & MR_URSTIEN) && (s->reg_sr & SR_URSTS));
}


static uint64_t rstc_mmio_read(void *opaque, hwaddr offset, unsigned size)
{
    RstcState *s = opaque;
    uint32_t sr;

    switch (offset) {
    case RSTC_SR:
        sr = s->reg_sr;
        s->reg_sr &= ~SR_URSTS;
        rstc_update_irq(s);
        return sr;

    case RSTC_MR:
        return s->reg_mr;

    default:
        error_report("at91.rstc: illegal read access at 0x%02lx", offset);
        abort();
    }
}

static void rstc_mmio_write(void *opaque, hwaddr offset, uint64_t value, unsigned size)
{
    RstcState *s = opaque;

    // check for the correct access key
    if (((value >> 24) & 0xFF) != RSTC_KEY_PASSWORD) {
        warn_report("at91.rstc: write access without proper key");
        return;
    }

    switch (offset) {
    case RSTC_CR:
        if (value & CR_PROCRST) {
            // TODO: reset processor
            warn_report("at91.rstc: processor reset not implemented yet");
        }
        if (value & CR_PERRST) {
            // TODO: reset peripherals
            warn_report("at91.rstc: preipheral reset not implemented yet");
        }
        if (value & CR_EXTRST) {
            // TODO: external reset
            warn_report("at91.rstc: external reset not implemented yet");
        }
        break;

    case RSTC_MR:
        s->reg_mr = value;
        break;

    default:
        error_report("at91.rstc: illegal read access at 0x%02lx", offset);
        abort();
    }

    rstc_update_irq(s);
}

static const MemoryRegionOps rstc_mmio_ops = {
    .read = rstc_mmio_read,
    .write = rstc_mmio_write,
    .impl.min_access_size = 4,
    .impl.max_access_size = 4,
    .valid.min_access_size = 4,
    .valid.max_access_size = 4,
    .endianness = DEVICE_NATIVE_ENDIAN,
};


static void rstc_device_init(Object *obj)
{
    SysBusDevice *sbd = SYS_BUS_DEVICE(obj);
    RstcState *s = AT91_RSTC(obj);

    sysbus_init_irq(sbd, &s->irq);

    memory_region_init_io(&s->mmio, OBJECT(s), &rstc_mmio_ops, s, "at91.rstc", 0x10);
    sysbus_init_mmio(SYS_BUS_DEVICE(s), &s->mmio);
}

static void rstc_device_realize(DeviceState *dev, Error **errp)
{
    RstcState *s = AT91_RSTC(dev);
    s->reg_sr = SR_URSTS | SR_NRSTL;    // TODO: actually implement NRST line?
    s->reg_mr = 0;
}

static void rstc_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    dc->realize = rstc_device_realize;
}

static const TypeInfo rstc_device_info = {
    .name = TYPE_AT91_RSTC,
    .parent = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(RstcState),
    .instance_init = rstc_device_init,
    .class_init = rstc_class_init,
};

static void rstc_register_types(void)
{
    type_register_static(&rstc_device_info);
}

type_init(rstc_register_types)
