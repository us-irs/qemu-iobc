#include "at91-spi.h"
#include "qemu/error-report.h"


static uint64_t spi_mmio_read(void *opaque, hwaddr offset, unsigned size)
{
    switch (offset) {
        // TODO

    default:
        error_report("at91.spi: illegal read access at 0x%02lx", offset);
        abort();
    }
}

static void spi_mmio_write(void *opaque, hwaddr offset, uint64_t value, unsigned size)
{
    switch (offset) {
        // TODO

    default:
        error_report("at91.spi: illegal read access at 0x%02lx", offset);
        abort();
    }
}

static const MemoryRegionOps spi_mmio_ops = {
    .read = spi_mmio_read,
    .write = spi_mmio_write,
    .impl.min_access_size = 4,
    .impl.max_access_size = 4,
    .valid.min_access_size = 4,
    .valid.max_access_size = 4,
    .endianness = DEVICE_NATIVE_ENDIAN,
};


static void spi_device_init(Object *obj)
{
    SysBusDevice *sbd = SYS_BUS_DEVICE(obj);
    SpiState *s = AT91_SPI(obj);

    sysbus_init_irq(sbd, &s->irq);

    memory_region_init_io(&s->mmio, OBJECT(s), &spi_mmio_ops, s, "at91.spi", 0x4000);
    sysbus_init_mmio(SYS_BUS_DEVICE(s), &s->mmio);
}

static void spi_reset_registers(SpiState *s)
{
    // TODO
}

static void spi_device_realize(DeviceState *dev, Error **errp)
{
    SpiState *s = AT91_SPI(dev);
    spi_reset_registers(s);
}

static void spi_device_reset(DeviceState *dev)
{
    SpiState *s = AT91_SPI(dev);
    spi_reset_registers(s);
}

static void spi_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    dc->realize = spi_device_realize;
    dc->reset = spi_device_reset;
}

static const TypeInfo spi_device_info = {
    .name = TYPE_AT91_SPI,
    .parent = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(SpiState),
    .instance_init = spi_device_init,
    .class_init = spi_class_init,
};

static void spi_register_types(void)
{
    type_register_static(&spi_device_info);
}

type_init(spi_register_types)