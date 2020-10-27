/*
 * AT91 Peripheral I/O controller.
 *
 * See at91-pio.h for details.
 *
 * Copyright (c) 2019-2020 KSat e.V. Stuttgart
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or, at your
 * option, any later version. See the COPYING file in the top-level directory.
 */

// Overview of TODOs:
// - QEMU IRQ lines are not connected to the respective peripheral lines
//   (secondary functionality of PIO). This is missing as the line-/pin-states of
//   the connected devices are currently not emulated.
// - Board implementation dependent PSR reset values are assumed to be zero.

#include "at91-pio.h"
#include "qapi/error.h"
#include "qemu/error-report.h"
#include "hw/irq.h"
#include "hw/qdev-properties.h"

#define IOX_CAT_PINSTATE            0x01
#define IOX_CID_PINSTATE_ENABLE     0x01
#define IOX_CID_PINSTATE_DISABLE    0x02
#define IOX_CID_PINSTATE_OUT        0x03
#define IOX_CID_PINSTATE_GET        0x04

#define PIO_PER     0x00
#define PIO_PDR     0x04
#define PIO_PSR     0x08
#define PIO_OER     0x10
#define PIO_ODR     0x14
#define PIO_OSR     0x18
#define PIO_IFER    0x20
#define PIO_IFDR    0x24
#define PIO_IFSR    0x28
#define PIO_SODR    0x30
#define PIO_CODR    0x34
#define PIO_ODSR    0x38
#define PIO_PDSR    0x3C
#define PIO_IER     0x40
#define PIO_IDR     0x44
#define PIO_IMR     0x48
#define PIO_ISR     0x4C
#define PIO_MDER    0x50
#define PIO_MDDR    0x54
#define PIO_MDSR    0x58
#define PIO_PUDR    0x60
#define PIO_PUER    0x64
#define PIO_PUSR    0x68
#define PIO_ASR     0x70
#define PIO_BSR     0x74
#define PIO_ABSR    0x78
#define PIO_OWER    0xA0
#define PIO_OWDR    0xA4
#define PIO_OWSR    0xA8


static void pio_handle_gpio_pin(void *opaque, int n, int level);

static void iox_pinstate_set(PioState *s, struct iox_data_frame *frame)
{
    if (frame->len != sizeof(uint32_t)) {
        warn_report("at91.pio: invalid pin-enable/-disable command payload");
        return;
    }

    uint32_t state = *((uint32_t *)&frame->payload[0]);
    bool level = frame->id == IOX_CID_PINSTATE_ENABLE;

    for (uint32_t i = 0; i < 32; i++)
        if (state & (1 << i))
            pio_handle_gpio_pin(s, i, level);
}

static void iox_pinstate_get(PioState *s, struct iox_data_frame *frame)
{
    int status = iox_send_u32_resp(s->server, frame, s->reg_pdsr);
    if (status) {
        error_report("at91.pio: failed to send pin-state");
        abort();
    }
}

static void iox_receive(struct iox_data_frame *frame, void *opaque)
{
    PioState *s = opaque;

    switch (frame->cat) {
    case IOX_CAT_PINSTATE:
        switch (frame->id) {
        case IOX_CID_PINSTATE_ENABLE:
        case IOX_CID_PINSTATE_DISABLE:
            iox_pinstate_set(s, frame);
            break;

        case IOX_CID_PINSTATE_GET:
            iox_pinstate_get(s, frame);
            break;
        }
    }

}

static void iox_send_pin_state(PioState *s)
{
    int status = iox_send_u32_new(s->server, IOX_CAT_PINSTATE, IOX_CID_PINSTATE_OUT, s->reg_pdsr);
    if (status) {
        error_report("at91.pio: failed to send pin-state");
        abort();
    }
}


inline static void pio_update_irq(PioState *s)
{
    qemu_set_irq(s->irq, !!(s->reg_isr & s->reg_imr));
}

static void pio_update_pins(PioState *s)
{
    uint32_t pdsr = s->reg_pdsr;
    uint32_t mask;
    int pin;

    for (pin = 0; pin < AT91_PIO_NUM_PINS; pin++) {
        mask = 1 << pin;

        if (s->reg_psr & mask) {            // PIO controlls this pin
            if (s->reg_osr & mask) {        // configured as output
                s->reg_pdsr = (s->reg_pdsr & ~mask) | (s->reg_odsr & mask);
            } else {                        // configured as input
                s->reg_pdsr = (s->reg_pdsr & ~mask) | (s->pin_state_in & mask);
            }
        } else if (~s->reg_absr & mask) {   // peripheral A controlls this pin
            s->reg_pdsr = (s->reg_pdsr & ~mask) | (s->pin_state_periph_a & mask);
        } else {                            // peripheral B controlls this pin
            s->reg_pdsr = (s->reg_pdsr & ~mask) | (s->pin_state_periph_b & mask);
        }

        // force pin value
        qemu_set_irq(s->pin_out[pin], !!(s->reg_pdsr & mask));
    }

    // trigger interrupt on edge/change
    s->reg_isr |= (pdsr ^ s->reg_pdsr);
    pio_update_irq(s);

    if (pdsr != s->reg_pdsr)
        iox_send_pin_state(s);
}


static void pio_handle_gpio_pin(void *opaque, int n, int level)
{   // input via physical pin/pad
    PioState *s = opaque;
    uint32_t mask = 1 << n;
    uint32_t pdsr = s->reg_pdsr;

    // save pin state
    s->pin_state_in = (s->pin_state_in & ~mask) | ((!!level) << n);

    // check if PIO controls this pin
    if (~s->reg_psr & mask)
        return;

    // check if line is output
    if (s->reg_osr & mask)
        return;

    // set PIO output state
    s->reg_pdsr = (s->reg_pdsr & ~mask) | ((!!level) << n);

    // trigger interrupt on edge
    if (s->reg_pdsr != pdsr) {
        s->reg_isr |= mask;
        pio_update_irq(s);
    }

    // set associated output pin
    qemu_set_irq(s->pin_out[n], level);
}

static void pio_handle_gpio_periph(PioState *s, int periph, int n, int level)
{   // input from peripheral output
    uint32_t mask = 1 << n;
    uint32_t pdsr = s->reg_pdsr;

    // save pin state
    if (periph == 0) {
        s->pin_state_periph_a = (s->pin_state_periph_a & ~mask) | ((!!level) << n);
    } else {
        s->pin_state_periph_b = (s->pin_state_periph_b & ~mask) | ((!!level) << n);
    }

    // check if PIO controls this pin (ie. peripheral output not used)
    if (s->reg_psr & mask)
        return;

    // check if correct peripheral is used
    if (!!(s->reg_absr & mask) != !!periph)
        return;

    // set PIO output state
    s->reg_pdsr = (s->reg_pdsr & ~mask) | ((!!level) << n);

    // trigger interrupt on edge
    if (s->reg_pdsr != pdsr) {
        s->reg_isr |= mask;
        pio_update_irq(s);
        iox_send_pin_state(s);
    }

    // set associated output pin
    qemu_set_irq(s->pin_out[n], level);
}

static void pio_handle_gpio_periph_a(void *opaque, int n, int level)
{
    pio_handle_gpio_periph(opaque, 0, n, level);
}

static void pio_handle_gpio_periph_b(void *opaque, int n, int level)
{
    pio_handle_gpio_periph(opaque, 1, n, level);
}


static uint64_t pio_mmio_read(void *opaque, hwaddr offset, unsigned size)
{
    PioState *s = opaque;
    uint32_t tmp;

    switch (offset) {
    case PIO_PSR:
        return s->reg_psr;

    case PIO_OSR:
        return s->reg_osr;

    case PIO_IFSR:
        return s->reg_ifsr;

    case PIO_ODSR:
        return s->reg_odsr;

    case PIO_PDSR:
        return s->reg_pdsr;

    case PIO_IMR:
        return s->reg_imr;

    case PIO_ISR:
        tmp = s->reg_isr;
        s->reg_isr = 0;
        pio_update_irq(s);
        return tmp;

    case PIO_MDSR:
        return s->reg_mdsr;

    case PIO_PUSR:
        return s->reg_pusr;

    case PIO_ABSR:
        return s->reg_absr;

    case PIO_OWSR:
        return s->reg_owsr;

    default:
        error_report("at91.pio: illegal read access at 0x%02lx", offset);
        abort();
    }
}

static void pio_mmio_write(void *opaque, hwaddr offset, uint64_t value, unsigned size)
{
    PioState *s = opaque;

    switch (offset) {
    case PIO_PER:
        s->reg_psr |= value;
        break;

    case PIO_PDR:
        s->reg_psr &= ~value;
        break;

    case PIO_OER:
        s->reg_osr |= value;
        break;

    case PIO_ODR:
        s->reg_osr &= ~value;
        break;

    case PIO_IFER:
        s->reg_ifsr |= value;
        break;

    case PIO_IFDR:
        s->reg_ifsr &= ~value;
        break;

    case PIO_SODR:
        s->reg_odsr |= value;
        break;

    case PIO_CODR:
        s->reg_odsr &= ~value;
        break;

    case PIO_ODSR:
        s->reg_odsr |= (s->reg_owsr & value);
        s->reg_odsr &= (~s->reg_owsr | ~value);
        break;

    case PIO_IER:
        s->reg_imr |= value;
        break;

    case PIO_IDR:
        s->reg_imr &= ~value;
        break;

    case PIO_MDER:
        s->reg_mdsr |= value;
        break;

    case PIO_MDDR:
        s->reg_mdsr &= ~value;
        break;

    case PIO_PUER:
        s->reg_pusr &= ~value;
        break;

    case PIO_PUDR:
        s->reg_pusr |= value;
        break;

    case PIO_ASR:
        s->reg_absr &= ~value;
        break;

    case PIO_BSR:
        s->reg_absr |= value;
        break;

    case PIO_OWER:
        s->reg_owsr |= value;
        break;

    case PIO_OWDR:
        s->reg_owsr &= ~value;
        break;

    default:
        error_report("at91.pio: illegal read access at 0x%02lx", offset);
        abort();
    }

    // TODO: move to appropriate scopes?
    pio_update_pins(s);     // also updates IRQs
}

static const MemoryRegionOps pio_mmio_ops = {
    .read = pio_mmio_read,
    .write = pio_mmio_write,
    .impl.min_access_size = 4,
    .impl.max_access_size = 4,
    .valid.min_access_size = 4,
    .valid.max_access_size = 4,
    .endianness = DEVICE_NATIVE_ENDIAN,
};


static void pio_device_init(Object *obj)
{
    SysBusDevice *sbd = SYS_BUS_DEVICE(obj);
    PioState *s = AT91_PIO(obj);

    sysbus_init_irq(sbd, &s->irq);

    memory_region_init_io(&s->mmio, OBJECT(s), &pio_mmio_ops, s, "at91.pio", 0x200);
    sysbus_init_mmio(SYS_BUS_DEVICE(s), &s->mmio);

    qdev_init_gpio_out_named(DEVICE(s), s->pin_out, "pin.out", AT91_PIO_NUM_PINS);
    qdev_init_gpio_in_named(DEVICE(s), pio_handle_gpio_pin, "pin.in", AT91_PIO_NUM_PINS);
    qdev_init_gpio_in_named(DEVICE(s), pio_handle_gpio_periph_a, "periph.in.a", AT91_PIO_NUM_PINS);
    qdev_init_gpio_in_named(DEVICE(s), pio_handle_gpio_periph_b, "periph.in.b", AT91_PIO_NUM_PINS);
}

static void pio_reset_registers(PioState *s)
{
    uint32_t pdsr = s->reg_pdsr;

    s->reg_psr  = 0;    // TODO: implementation dependent (Sec. 9.3), implement as property?
    s->reg_osr  = 0;
    s->reg_ifsr = 0;
    s->reg_odsr = 0;
    s->reg_pdsr = 0;
    s->reg_imr  = 0;
    s->reg_isr  = 0;
    s->reg_mdsr = 0;
    s->reg_pusr = 0;
    s->reg_absr = 0;
    s->reg_owsr = 0;

    if (pdsr != s->reg_pdsr)
        iox_send_pin_state(s);
}

static void pio_device_realize(DeviceState *dev, Error **errp)
{
    PioState *s = AT91_PIO(dev);

    pio_reset_registers(s);

    if (s->socket) {
        SocketAddress addr;
        addr.type = SOCKET_ADDRESS_TYPE_UNIX;
        addr.u.q_unix.path = s->socket;

        IoXferServer *srv = iox_server_new();
        if (!srv) {
            error_set(errp, ERROR_CLASS_GENERIC_ERROR, "cannot allocate server");
            return;
        }

        iox_server_set_handler(srv, iox_receive, s);

        if (iox_server_open(srv, &addr, errp))
            return;

        s->server = srv;
        info_report("at91.pio: listening on %s", s->socket);
    }
}

static void pio_device_unrealize(DeviceState *dev, Error **errp)
{
    PioState *s = AT91_PIO(dev);

    if (s->server) {
        iox_server_free(s->server);
        s->server = NULL;
    }
}

static void pio_device_reset(DeviceState *dev)
{
    PioState *s = AT91_PIO(dev);
    pio_reset_registers(s);
}

static Property pio_device_properties[] = {
    DEFINE_PROP_STRING("socket", PioState, socket),
    DEFINE_PROP_END_OF_LIST(),
};

static void pio_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    dc->realize = pio_device_realize;
    dc->unrealize = pio_device_unrealize;
    dc->reset = pio_device_reset;
    device_class_set_props(dc, pio_device_properties);
}

static const TypeInfo pio_device_info = {
    .name = TYPE_AT91_PIO,
    .parent = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(PioState),
    .instance_init = pio_device_init,
    .class_init = pio_class_init,
};

static void pio_register_types(void)
{
    type_register_static(&pio_device_info);
}

type_init(pio_register_types)
