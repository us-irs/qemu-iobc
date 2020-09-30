/*
 * AT91 Programmable Interrupt Timer
 *
 * See at91-pmc.h for details.
 */

// Overview of TODOs:
// - Simulate SR_MCKRDY: Currently, SR_MCKRDY is set instantly on clock change.
//   In reality, SR_MCKRDY should be unset until the clock has stabilized. This
//   stabilization process is currently not simulated.

#include "at91-pmc.h"
#include "qemu/error-report.h"
#include "hw/irq.h"


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


inline static void pmc_notify_mclk_change(PmcState *s)
{
    if (s->mclk_cb)
        s->mclk_cb(s->mclk_opaque, s->master_clock_freq);
}


static void pmc_update_mckr(PmcState *s)
{
    uint8_t css = s->reg_pmc_mckr & 0x03;
    bool ready = false;
    unsigned freq = s->master_clock_freq;

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

    if (ready) {
        switch (css) {
        case 0:     // slow clock
            freq = AT91_PMC_SLCK;
            break;

        case 1:     // main clock
            freq = AT91_PMC_MCK;
            break;

        case 2:     // PLLA clock
            freq = AT91_PMC_MCK;
            freq /= s->reg_ckgr_plla & 0xff;
            freq *= ((s->reg_ckgr_plla >> 16) & 0xff) + 1;
            break;

        case 3:     // PLLB clock
            freq = AT91_PMC_MCK;
            freq /= s->reg_ckgr_pllb & 0xff;
            freq *= ((s->reg_ckgr_pllb >> 16) & 0x3f) + 1;
            break;
        }

        freq /= 1 << ((s->reg_pmc_mckr >> 2) & 0x07);
        if ((s->reg_pmc_mckr >> 8) & 0x03) {
            freq /= 2 * ((s->reg_pmc_mckr >> 8) & 0x03);
        }
    }

    // TODO: set master clock to zero if not ready?

    if (s->master_clock_freq != freq) {
        s->master_clock_freq = freq;
        pmc_notify_mclk_change(s);
    }
}

static uint64_t pmc_mmio_read(void *opaque, hwaddr offset, unsigned size)
{
    PmcState *s = opaque;

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
        s->reg_ckgr_mcfr = (value & 1) ? (1 << 16) | (AT91_PMC_MCK / AT91_PMC_SLCK / 16) : 0;
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

static void pmc_reset_registers_from_init_state(PmcState *s)
{
    pmc_reset_registers(s);

    if (!s->init_state)
        return;

    s->reg_ckgr_mor  = s->init_state->reg_ckgr_mor;
    s->reg_pmc_sr = (s->reg_pmc_sr & ~0x00000001) | (s->init_state->reg_ckgr_mor & 0x00000001);
    s->reg_ckgr_mcfr = (s->init_state->reg_ckgr_mor & 1) ? (1 << 16)
                       | (AT91_PMC_MCK / AT91_PMC_SLCK / 16) : 0;

    s->reg_ckgr_plla = s->init_state->reg_ckgr_plla;
    s->reg_pmc_sr |= SR_LOCKA;

    s->reg_ckgr_pllb = s->init_state->reg_ckgr_pllb;
    s->reg_pmc_sr |= SR_LOCKB;

    s->reg_pmc_mckr  = s->init_state->reg_pmc_mckr;
}

static void pmc_device_realize(DeviceState *dev, Error **errp)
{
    PmcState *s = AT91_PMC(dev);

    pmc_reset_registers_from_init_state(s);
    s->master_clock_freq = 0;

    pmc_update_mckr(s);
}

static void pmc_device_reset(DeviceState *dev)
{
    PmcState *s = AT91_PMC(dev);

    /**
     * Note: Do not set clock on reset. This prevents the clock from being set
     * externally at boot via the device loader options.
     */
    // pmc_reset_registers_from_init_state(s);

    s->master_clock_freq = 0;
    pmc_update_mckr(s);
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
