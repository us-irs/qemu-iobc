#include "at91-pio.h"
#include "qemu/error-report.h"

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
}

static void pio_device_realize(DeviceState *dev, Error **errp)
{
    PioState *s = AT91_PIO(dev);
    pio_reset_registers(s);
}

static void pio_device_reset(DeviceState *dev)
{
    PioState *s = AT91_PIO(dev);
    pio_reset_registers(s);
}

static void pio_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    dc->realize = pio_device_realize;
    dc->reset = pio_device_reset;
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