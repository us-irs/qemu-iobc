/*
 * AT91 Programmable Interrupt Timer
 *
 * See at91-pit.h for details.
 *
 * Copyright (c) 2019-2020 KSat e.V. Stuttgart
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or, at your
 * option, any later version. See the COPYING file in the top-level directory.
 */

#include "at91-pit.h"
#include "qemu/error-report.h"
#include "hw/irq.h"


#define PIT_MR      0x00
#define PIT_SR      0x04
#define PIT_PIVR    0x08
#define PIT_PIIR    0x0C

#define MR_PIV      0x0FFFFF
#define MR_PITEN    (1 << 24)
#define MR_PITIEN   (1 << 25)

#define SR_PITS     0x01


void at91_pit_set_master_clock(PitState *s, unsigned mclk)
{
    s->mclk = mclk;

    if (s->timer) {
        ptimer_transaction_begin(s->timer);
        ptimer_set_freq(s->timer, s->mclk / 16);
        ptimer_transaction_commit(s->timer);
    }
}


static void pit_timer_tick(void *opaque)
{
    PitState *s = opaque;

    s->reg_sr |= SR_PITS;
    s->picnt = (s->picnt + 1) & 0xFFF;

    // trigger interrupt, if enabled
    if (s->reg_mr & MR_PITIEN) {
        qemu_set_irq(s->irq, 1);
    }

    // disable timer if requested
    if (!(s->reg_mr & MR_PITEN)) {
        ptimer_transaction_begin(s->timer);
        ptimer_stop(s->timer);
        ptimer_transaction_commit(s->timer);
    }
}


inline static uint32_t pit_timer_period(PitState *s)
{
    return 1 + (s->reg_mr & MR_PIV);
}

inline static uint32_t pit_timer_cpiv(PitState *s)
{
    return (s->picnt << 20) | ((pit_timer_period(s) - ptimer_get_count(s->timer)) & 0xFFFFF);
}


static uint64_t pit_mmio_read(void *opaque, hwaddr offset, unsigned size)
{
    PitState *s = opaque;
    uint32_t picnt, cpiv;

    switch (offset) {
    case PIT_MR:
        return s->reg_mr;

    case PIT_SR:
        return s->reg_sr;

    case PIT_PIVR:
        picnt = s->picnt;
        cpiv = pit_timer_cpiv(s);

        // reset overflow counter and interrupt
        s->picnt = 0;
        s->reg_sr &= ~SR_PITS;
        qemu_set_irq(s->irq, 0);

        return (picnt << 20) | cpiv;

    case PIT_PIIR:
        return (s->picnt << 20) | pit_timer_cpiv(s);

    default:
        error_report("at91.pit: illegal read access at 0x%02lx", offset);
        abort();
    }
}

static void pit_mmio_write(void *opaque, hwaddr offset, uint64_t value, unsigned size)
{
    PitState *s = opaque;

    switch (offset) {
    case PIT_MR:
        s->reg_mr = value;

        if (value & MR_PITEN) {
            ptimer_transaction_begin(s->timer);
            ptimer_set_freq(s->timer, s->mclk / 16);
            ptimer_set_limit(s->timer, pit_timer_period(s), 1);
            ptimer_run(s->timer, 0);
            ptimer_transaction_commit(s->timer);
        } else {
            // do nothing: timer is disabled and stopped once CPIV reaches zero
        }

        break;

    default:
        error_report("at91.pit: illegal read access at 0x%02lx", offset);
        abort();
    }
}

static const MemoryRegionOps pit_mmio_ops = {
    .read = pit_mmio_read,
    .write = pit_mmio_write,
    .impl.min_access_size = 4,
    .impl.max_access_size = 4,
    .valid.min_access_size = 4,
    .valid.max_access_size = 4,
    .endianness = DEVICE_NATIVE_ENDIAN,
};


static void pit_reset_registers(PitState *s)
{
    s->reg_mr = 0xFFFFF;
    s->reg_sr = 0;
    s->picnt  = 0;
}

static void pit_device_init(Object *obj)
{
    SysBusDevice *sbd = SYS_BUS_DEVICE(obj);
    PitState *s = AT91_PIT(obj);

    s->timer = ptimer_init(pit_timer_tick, s, PTIMER_POLICY_DEFAULT);

    sysbus_init_irq(sbd, &s->irq);

    memory_region_init_io(&s->mmio, OBJECT(s), &pit_mmio_ops, s, "at91.pit", 0x10);
    sysbus_init_mmio(SYS_BUS_DEVICE(s), &s->mmio);
}

static void pit_device_realize(DeviceState *dev, Error **errp)
{
    PitState *s = AT91_PIT(dev);

    pit_reset_registers(s);
}

static void pit_device_reset(DeviceState *dev)
{
    PitState *s = AT91_PIT(dev);

    ptimer_transaction_begin(s->timer);
    ptimer_stop(s->timer);
    ptimer_transaction_commit(s->timer);

    pit_reset_registers(s);
    qemu_set_irq(s->irq, 0);
}

static void pit_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    dc->realize = pit_device_realize;
    dc->reset = pit_device_reset;
}

static const TypeInfo pit_device_info = {
    .name = TYPE_AT91_PIT,
    .parent = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(PitState),
    .instance_init = pit_device_init,
    .class_init = pit_class_init,
};

static void pit_register_types(void)
{
    type_register_static(&pit_device_info);
}

type_init(pit_register_types)
