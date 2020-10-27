/*
 * AT91 Advanced Interrupt Controller stub.
 *
 * See at91-aic_stub.h for details.
 *
 * Copyright (c) 2019-2020 KSat e.V. Stuttgart
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or, at your
 * option, any later version. See the COPYING file in the top-level directory.
 */

#include "at91-aic_stub.h"
#include "qemu/error-report.h"
#include "hw/irq.h"


static void aicstub_irq_handle(void *opaque, int n, int level)
{
    AicStubState *s = opaque;

    s->line_state = (s->line_state & ~(1 << n)) | ((!!level) << n);
    qemu_set_irq(s->irq, !!s->line_state);
}

static void aicstub_device_init(Object *obj)
{
    SysBusDevice *sbd = SYS_BUS_DEVICE(obj);
    AicStubState *s = AT91_AIC_STUB(obj);

    sysbus_init_irq(sbd, &s->irq);
    qdev_init_gpio_in_named(DEVICE(s), aicstub_irq_handle, "irq-line", 32);
}

static void aicstub_device_realize(DeviceState *dev, Error **errp)
{
    AicStubState *s = AT91_AIC_STUB(dev);
    s->line_state = 0;
}

static void aicstub_device_reset(DeviceState *dev)
{
    AicStubState *s = AT91_AIC_STUB(dev);
    s->line_state = 0;
}

static void aicstub_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    dc->realize = aicstub_device_realize;
    dc->reset = aicstub_device_reset;
}

static const TypeInfo aicstub_device_info = {
    .name = TYPE_AT91_AIC_STUB,
    .parent = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(AicStubState),
    .instance_init = aicstub_device_init,
    .class_init = aicstub_class_init,
};

static void aicstub_register_types(void)
{
    type_register_static(&aicstub_device_info);
}

type_init(aicstub_register_types)
