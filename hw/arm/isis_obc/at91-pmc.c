#include "at91-pmc.h"
#include "qemu/error-report.h"


enum reg_index {
    R_PMC_SCER    = 0x00 / 4,
    R_PMC_SCDR    = 0x04 / 4,
    R_PMC_SCSR    = 0x08 / 4,
    R_PMC_PCER    = 0x10 / 4,
    R_PMC_PCDR    = 0x14 / 4,
    R_PMC_PCSR    = 0x18 / 4,
    R_CKGR_MOR    = 0x20 / 4,
    R_CKGR_MCFR   = 0x24 / 4,
    R_CKGR_PLLAR  = 0x28 / 4,
    R_CKGR_PLLBR  = 0x2C / 4,
    R_PMC_MCKR    = 0x30 / 4,
    R_PMC_PCK0    = 0x40 / 4,
    R_PMC_PCK1    = 0x44 / 4,
    R_PMC_IER     = 0x60 / 4,
    R_PMC_IDR     = 0x64 / 4,
    R_PMC_SR      = 0x68 / 4,
    R_PMC_IMR     = 0x6C / 4,
    R_PMC_PLLICPR = 0x80 / 4,
};

enum reg_access {
    ACCESS_RESERVED,
    ACCESS_RW,
    ACCESS_RO,
    ACCESS_WO,
};

static enum reg_access pmc_reg_access[33] = {
    ACCESS_WO,
    ACCESS_WO,
    ACCESS_RO,
    ACCESS_RESERVED,
    ACCESS_WO,
    ACCESS_WO,
    ACCESS_RO,
    ACCESS_RESERVED,
    ACCESS_RW,
    ACCESS_RO,
    ACCESS_RW,
    ACCESS_RW,
    ACCESS_RW,
    ACCESS_RESERVED,
    ACCESS_RESERVED,
    ACCESS_RW,
    ACCESS_RW,
    ACCESS_RESERVED,
    ACCESS_RESERVED,
    ACCESS_RESERVED,
    ACCESS_RESERVED,
    ACCESS_RESERVED,
    ACCESS_RESERVED,
    ACCESS_WO,
    ACCESS_WO,
    ACCESS_RO,
    ACCESS_RO,
    ACCESS_RESERVED,
    ACCESS_RESERVED,
    ACCESS_RESERVED,
    ACCESS_RESERVED,
    ACCESS_RW,
};


static uint64_t pmc_mmio_read(void *opaque, hwaddr offset, unsigned size)
{
    PmcState *s = opaque;
    unsigned index = offset / 4;

    if (size != 0x04) {
        error_report("at91.pmc illegal read access at "
                      "0x%08lx with size: 0x%02x", offset, size);
        abort();
    }

    if (offset > 0x80) {
        error_report("at91.pmc illegal read access at 0x%08lx", offset);
        abort();
    }

    if (pmc_reg_access[index] == ACCESS_RESERVED || pmc_reg_access[index] == ACCESS_WO) {
        error_report("at91.pmc illegal read access at 0x%08lx", offset);
        abort();
    }

    info_report("at91.pmc read 0x%08lx", offset);
    return s->reg[index];
}

static void pmc_mmio_write(void *opaque, hwaddr offset, uint64_t value, unsigned size)
{
    PmcState *s = opaque;
    unsigned index = offset / 4;

    if (size != 0x04) {
        error_report("at91.pmc illegal write access at "
                      "0x%08lx with size: 0x%02x [value: 0x%08lx]",
                      offset, size, value);
        abort();
    }

    if (offset > 0x80) {
        error_report("at91.pmc illegal write access at "
                      "0x%08lx [value: 0x%08lx]", offset, value);
        abort();
    }

    if (pmc_reg_access[index] == ACCESS_RESERVED || pmc_reg_access[index] == ACCESS_RO) {
        error_report("at91.pmc illegal write access at "
                      "0x%08lx [value: 0x%08lx]", offset, value);
        abort();
    }

    info_report("at91.pmc write 0x%08lx [value: 0x%08lx]", offset, value);
    s->reg[index] = value;

    // somre register writes have side-effects...
    switch (index) {
    case R_CKGR_MOR:    // set/clear MOSCS
        s->reg[R_PMC_SR] = (s->reg[R_PMC_SR] & ~0x00000001) | (value & 0x00000001);
        break;

    case R_CKGR_PLLAR:  // set LOCKA
        s->reg[R_PMC_SR] = (s->reg[R_PMC_SR] & ~0x00000002) | 0x00000002;
        break;

    case R_CKGR_PLLBR:  // set LOCKB
        s->reg[R_PMC_SR] = (s->reg[R_PMC_SR] & ~0x00000004) | 0x00000004;
        break;

    case R_PMC_MCKR:    // set MCKRDY
        s->reg[R_PMC_SR] = (s->reg[R_PMC_SR] & ~0x00000008) | 0x00000008;
        break;
    }

    // TODO: implement missing/re-check existing side-effects
    // TODO: simulate non-instant change?
    // TODO: provide interrupts if configured in PMC_IMR
}

static const MemoryRegionOps pmc_mmio_ops = {
    .read = pmc_mmio_read,
    .write = pmc_mmio_write,
    .impl.min_access_size = 4,
    .impl.max_access_size = 4,
    .valid.min_access_size = 4,
    .valid.max_access_size = 4,
    .endianness = DEVICE_NATIVE_ENDIAN,
};

static void pmc_reset_registers(PmcState *s)
{
    s->reg[R_PMC_SCSR]    = 0x03;
    s->reg[R_PMC_PCSR]    = 0x00;
    s->reg[R_CKGR_MOR]    = 0x00;
    s->reg[R_CKGR_MCFR]   = 0x00;
    s->reg[R_CKGR_PLLAR]  = 0x3F00;
    s->reg[R_CKGR_PLLBR]  = 0x3F00;
    s->reg[R_PMC_MCKR]    = 0x00;
    s->reg[R_PMC_PCK0]    = 0x00;
    s->reg[R_PMC_PCK1]    = 0x00;
    s->reg[R_PMC_SR]      = 0x08;
    s->reg[R_PMC_IMR]     = 0x00;
    s->reg[R_PMC_PLLICPR] = 0x00;
}

static void pmc_device_realize(DeviceState *dev, Error **errp)
{
    PmcState *s = AT91_PMC(dev);
    pmc_reset_registers(s);

    memory_region_init_io(&s->mmio, OBJECT(s), &pmc_mmio_ops, s, "at91.pmc", 0x100);
    sysbus_init_mmio(SYS_BUS_DEVICE(s), &s->mmio);
}

static void pmc_device_reset(DeviceState *dev)
{
    pmc_reset_registers(AT91_PMC(dev));
}

static void pmc_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    dc->realize = pmc_device_realize;
    dc->reset = pmc_device_reset;
}

static const TypeInfo pmc_device_info = {
    .name = TYPE_AT91_PMC,
    .parent = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(PmcState),
    .class_init = pmc_class_init,
};

static void pmc_register_types(void)
{
    type_register_static(&pmc_device_info);
}

type_init(pmc_register_types)
