/*
 * Simple emulated GPIO pushbuttons.
 *
 * See gpio-pushbutton.h for details.
 *
 * Copyright (c) 2019-2020 KSat e.V. Stuttgart
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or, at your
 * option, any later version. See the COPYING file in the top-level directory.
 */

#include "gpio-pushbutton.h"
#include "qemu/error-report.h"
#include "hw/irq.h"

#include "io/channel.h"
#include "io/channel-socket.h"


__attribute__ ((packed))
struct gpio_pb_cmd {
    uint8_t number;
    uint8_t value;
};


#define GPIO_PB_PORT    "6000"

static gboolean ioc_handle_event(QIOChannel *ioc, GIOCondition condition, gpointer data)
{
    GpioPushbuttonState *s = data;
    ssize_t ret;
    struct gpio_pb_cmd cmd;

    ret = qio_channel_read(QIO_CHANNEL(s->ioc), (char *)&cmd, sizeof(struct gpio_pb_cmd), NULL);
    if (ret < 0) {
        error_report("gpio-pushbuttons: error receiving command data: %ld", ret);
        return G_SOURCE_REMOVE;
    }

    if (ret < sizeof(struct gpio_pb_cmd)) {
        error_report("gpio-pushbuttons: incomplete command data");
        return G_SOURCE_CONTINUE;
    }

    if (cmd.number >= 32) {
        error_report("gpio-pushbuttons: invalid command data");
        return G_SOURCE_CONTINUE;
    }

    info_report("gpio-pushbuttons: set gpio %d to %d", cmd.number, cmd.value);
    qemu_set_irq(s->buttons[cmd.number], !!cmd.value);

    return G_SOURCE_CONTINUE;
}

static void gpio_pushbutton_server_setup(GpioPushbuttonState *s, Error **errp)
{
    QIOChannelSocket *ioc;
    SocketAddress local;
    SocketAddress remote;
    int status;

    local.type = SOCKET_ADDRESS_TYPE_INET;
    local.u.inet.host     = (char *)"";
    local.u.inet.port     = (char *)GPIO_PB_PORT;
    local.u.inet.has_ipv4 = false;      // force IPv6
    local.u.inet.ipv4     = false;
    local.u.inet.has_ipv6 = true;
    local.u.inet.ipv6     = true;

    remote.type = SOCKET_ADDRESS_TYPE_INET;
    remote.u.inet.host     = (char *)"";
    remote.u.inet.port     = (char *)"0";
    remote.u.inet.has_ipv4 = false;     // force IPv6
    remote.u.inet.ipv4     = false;
    remote.u.inet.has_ipv6 = true;
    remote.u.inet.ipv6     = true;

    ioc = qio_channel_socket_new();

    status = qio_channel_socket_dgram_sync(ioc, &local, &remote, NULL);
    if (status) {
        error_report("failed to create socket: %d", status);
        abort();
    }

    qio_channel_add_watch(QIO_CHANNEL(ioc), G_IO_IN, ioc_handle_event, s, NULL);

    s->ioc = ioc;
    info_report("gpio-pushbuttons: listening on port %s", GPIO_PB_PORT);
}


static void gpio_pushbutton_device_init(Object *obj)
{
    GpioPushbuttonState *s = GPIO_PUSHBUTTON(obj);

    qdev_init_gpio_out_named(DEVICE(s), s->buttons, "pushbutton", 32);
}

static void gpio_pushbutton_device_realize(DeviceState *dev, Error **errp)
{
    gpio_pushbutton_server_setup(GPIO_PUSHBUTTON(dev), errp);
}

static void gpio_pushbutton_device_reset(DeviceState *dev)
{
    GpioPushbuttonState *s = GPIO_PUSHBUTTON(dev);

    for (int i = 0; i < 32; i++) {
        qemu_set_irq(s->buttons[0], 0);
    }
}

static void gpio_pushbutton_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    dc->realize = gpio_pushbutton_device_realize;
    dc->reset = gpio_pushbutton_device_reset;
}

static const TypeInfo gpio_pushbutton_device_info = {
    .name = TYPE_GPIO_PUSHBUTTON,
    .parent = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(GpioPushbuttonState),
    .instance_init = gpio_pushbutton_device_init,
    .class_init = gpio_pushbutton_class_init,
};

static void gpio_pushbutton_register_types(void)
{
    type_register_static(&gpio_pushbutton_device_info);
}

type_init(gpio_pushbutton_register_types)
