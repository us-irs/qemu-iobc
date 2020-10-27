/*
 * Simple emulated LED.
 *
 * See gpio-led.h for details.
 *
 * Copyright (c) 2019-2020 KSat e.V. Stuttgart
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or, at your
 * option, any later version. See the COPYING file in the top-level directory.
 */

#include "gpio-led.h"
#include "qemu/error-report.h"
#include "hw/qdev-properties.h"


static void gpio_led_irq_handle(void *opaque, int n, int level)
{
    GpioLedState *s = opaque;

    level = !!level;
    if (s->state != level) {
        info_report("led[%s]: state changed to %d", s->name, level);
        s->state = level;
    }
}

static void gpio_led_device_init(Object *obj)
{
    GpioLedState *s = GPIO_LED(obj);

    qdev_init_gpio_in_named(DEVICE(s), gpio_led_irq_handle, "led", 1);
}

static void gpio_led_device_realize(DeviceState *dev, Error **errp)
{
    GpioLedState *s = GPIO_LED(dev);
    s->state = 0;
}

static void gpio_led_device_reset(DeviceState *dev)
{
    GpioLedState *s = GPIO_LED(dev);
    s->state = 0;
}

static Property gpio_led_properties[] = {
    DEFINE_PROP_STRING("name", GpioLedState, name),
    DEFINE_PROP_END_OF_LIST(),
};

static void gpio_led_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    dc->realize = gpio_led_device_realize;
    dc->reset = gpio_led_device_reset;
    device_class_set_props(dc, gpio_led_properties);
}

static const TypeInfo gpio_led_device_info = {
    .name = TYPE_GPIO_LED,
    .parent = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(GpioLedState),
    .instance_init = gpio_led_device_init,
    .class_init = gpio_led_class_init,
};

static void gpio_led_register_types(void)
{
    type_register_static(&gpio_led_device_info);
}

type_init(gpio_led_register_types)
