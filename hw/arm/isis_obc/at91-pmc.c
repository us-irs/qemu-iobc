#include "at91-pmc.h"
#include "qemu/error-report.h"

#define SO_FREQ        32768        // slow clock oscillator frequency
#define MO_FREQ     18432000        // main oscillator frequency

#define SR_MOSCS    0x00000001
#define SR_LOCKA    0x00000002
#define SR_LOCKB    0x00000004
#define SR_MCKRDY   0x00000008

#define PMC_SCER        0x00
#define PMC_SCDR        0x04
#define PMC_SCSR        0x08
#define PMC_PCER        0x10
#define PMC_PCDR        0x14
#define PMC_PCSR        0x18
#define CKGR_MOR        0x20
#define CKGR_MCFR       0x24
#define CKGR_PLLAR      0x28
#define CKGR_PLLBR      0x2C
#define PMC_MCKR        0x30
#define PMC_PCK0        0x40
#define PMC_PCK1        0x44
#define PMC_IER         0x60
#define PMC_IDR         0x64
#define PMC_SR          0x68
#define PMC_IMR         0x6C
#define PMC_PLLICPR     0x80

#define PMC_IRQ_MASK    0x30F


static void pmc_update_mckr(PmcState *s)
{
    uint8_t css = s->reg_pmc_mckr & 0x03;
    bool ready = false;

    switch (css) {
    case 0:     // slow clock
        ready = true;
        break;

    case 1:     // main clock
        ready = s->reg_pmc_sr & SR_MOSCS;
        break;

    case 2:     // PLLA clock
        ready = !!(s->reg_pmc_sr & SR_LOCKA);       // clock must be locked/stable
        ready &= !!(s->reg_ckgr_plla & 0x0000ff);   // non-zero divider
        ready &= !!(s->reg_ckgr_plla & 0xff0000);   // non-zero multiplier
        break;

    case 3:     // PLLB clock
        ready = !!(s->reg_pmc_sr & SR_LOCKB);       // clock must be locked/stable
        ready &= !!(s->reg_ckgr_pllb & 0x0000ff);   // non-zero divider
        ready &= !!(s->reg_ckgr_pllb & 0x3f0000);   // non-zero multiplier
        break;
    }

    if (ready)
        s->reg_pmc_sr |= SR_MCKRDY;
    else
        s->reg_pmc_sr &= ~SR_MCKRDY;
}

static uint64_t pmc_mmio_read(void *opaque, hwaddr offset, unsigned size)
{
    PmcState *s = opaque;

    if (size != 0x04) {
        error_report("at91.pmc illegal read access at "
                      "0x%08lx with size: 0x%02x", offset, size);
        abort();
    }

    info_report("at91.pmc read 0x%08lx", offset);

    switch (offset) {
    case PMC_SCSR:
        return s->reg_pmc_scsr;

    case PMC_PCSR:
        return s->reg_pmc_pcsr;

    case CKGR_MOR:
        return s->reg_ckgr_mor;

    case CKGR_MCFR:
        return s->reg_ckgr_mcfr;

    case CKGR_PLLAR:
        return s->reg_ckgr_plla;

    case CKGR_PLLBR:
        return s->reg_ckgr_pllb;

    case PMC_MCKR:
        return s->reg_pmc_mckr;

    case PMC_PCK0:
        return s->reg_pmc_pck0;

    case PMC_PCK1:
        return s->reg_pmc_pck1;

    case PMC_SR:
        return s->reg_pmc_sr;

    case PMC_IMR:
        return s->reg_pmc_imr;

    case PMC_PLLICPR:
        return s->reg_pmc_pllicpr;

    default:
        error_report("at91.pmc illegal read access at 0x%08lx", offset);
        abort();
    }
}

static void pmc_mmio_write(void *opaque, hwaddr offset, uint64_t value, unsigned size)
{
    PmcState *s = opaque;

    if (size != 0x04) {
        error_report("at91.pmc illegal write access at "
                      "0x%08lx with size: 0x%02x [value: 0x%08lx]",
                      offset, size, value);
        abort();
    }

    info_report("at91.pmc write 0x%08lx [value: 0x%08lx]", offset, value);

    switch (offset) {
    case PMC_SCER:
        s->reg_pmc_scsr |= value;
        break;

    case PMC_SCDR:
        s->reg_pmc_scsr &= ~value;
        break;

    case PMC_PCER:
        s->reg_pmc_pcsr |= value;
        break;

    case PMC_PCDR:
        s->reg_pmc_pcsr &= ~value;
        break;

    case CKGR_MOR:
        s->reg_ckgr_mor = value;
        s->reg_pmc_sr = (s->reg_pmc_sr & ~0x00000001) | (value & 0x00000001);
        s->reg_ckgr_mcfr = (value & 1) ? (1 << 16) | (MO_FREQ / SO_FREQ / 16) : 0;
        break;

    case CKGR_PLLAR:
        s->reg_ckgr_plla = value;
        s->reg_pmc_sr |= SR_LOCKA;
        break;

    case CKGR_PLLBR:
        s->reg_ckgr_pllb = value;
        s->reg_pmc_sr |= SR_LOCKB;
        break;

    case PMC_MCKR:
        s->reg_pmc_mckr = value;
        break;

    case PMC_PCK0:
        s->reg_pmc_pck0 = value;
        break;

    case PMC_PCK1:
        s->reg_pmc_pck1 = value;
        break;

    case PMC_IER:
        s->reg_pmc_imr |= value;
        break;

    case PMC_IDR:
        s->reg_pmc_imr &= ~value;
        break;

    case PMC_PLLICPR:
        s->reg_pmc_pllicpr = value;
        break;

    default:
        error_report("at91.pmc illegal write access at "
                      "0x%08lx [value: 0x%08lx]", offset, value);
        abort();
    }

    pmc_update_mckr(s);

    // set interrupt if requested
    qemu_set_irq(s->irq, !!(s->reg_pmc_sr & s->reg_pmc_imr & PMC_IRQ_MASK));
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
    s->reg_pmc_scsr    = 0x03;
    s->reg_pmc_pcsr    = 0x00;
    s->reg_ckgr_mor    = 0x00;
    s->reg_ckgr_mcfr   = 0x00;
    s->reg_ckgr_plla   = 0x3F00;
    s->reg_ckgr_pllb   = 0x3F00;
    s->reg_pmc_mckr    = 0x00;
    s->reg_pmc_pck0    = 0x00;
    s->reg_pmc_pck1    = 0x00;
    s->reg_pmc_sr      = 0x08;
    s->reg_pmc_imr     = 0x00;
    s->reg_pmc_pllicpr = 0x00;
}

static void pmc_device_realize(DeviceState *dev, Error **errp)
{
    PmcState *s = AT91_PMC(dev);
    pmc_reset_registers(s);
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

static void pmc_instance_init(Object *obj)
{
    SysBusDevice *sbd = SYS_BUS_DEVICE(obj);
    PmcState *s = AT91_PMC(obj);

    sysbus_init_irq(sbd, &s->irq);

    memory_region_init_io(&s->mmio, OBJECT(s), &pmc_mmio_ops, s, "at91.pmc", 0x100);
    sysbus_init_mmio(SYS_BUS_DEVICE(s), &s->mmio);
}

static const TypeInfo pmc_device_info = {
    .name = TYPE_AT91_PMC,
    .parent = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(PmcState),
    .instance_init = pmc_instance_init,
    .class_init = pmc_class_init,
};

static void pmc_register_types(void)
{
    type_register_static(&pmc_device_info);
}

type_init(pmc_register_types)
