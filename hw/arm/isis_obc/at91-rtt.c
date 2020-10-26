/*
 * AT91 Real-Time Timer.
 */

#include "at91-rtt.h"
#include "qemu/error-report.h"
#include "hw/irq.h"


#define AT91_SCLK       0x8000

#define RTT_MR          0x00
#define RTT_AR          0x04
#define RTT_VR          0x08
#define RTT_SR          0x0C

#define MR_RTPRES       0xFFFF
#define MR_ALMIEN       BIT(16)
#define MR_RTTINCIEN    BIT(17)
#define MR_RTTRST       BIT(18)

#define SR_ALMS         BIT(0)
#define SR_RTTINC       BIT(1)

#define IRQMASK(s)      (((s)->reg_mr >> 16) & 0x03)


static void rtt_update_irq(RttState *s)
{
    qemu_set_irq(s->irq, !!(IRQMASK(s) & s->reg_sr));
}

static void rtt_update_timer_freq(RttState *s)
{
    unsigned rtpres = (s->reg_mr & MR_RTPRES) ? (s->reg_mr & MR_RTPRES) : AT91_SCLK;
    unsigned freq = AT91_SCLK / rtpres;

    ptimer_transaction_begin(s->timer);
    ptimer_set_freq(s->timer, freq);
    ptimer_run(s->timer, 0);
    ptimer_transaction_commit(s->timer);
}

static void rtt_timer_tick(void *opaque)
{
    RttState *s = opaque;

    s->reg_vr += 1;
    s->reg_sr |= SR_RTTINC;

    if (s->reg_vr == s->reg_ar + 1)
        s->reg_sr |= SR_ALMS;

    // do not use rtt_update_irq to avoid frequent calls
    if (IRQMASK(s) & s->reg_sr)
        qemu_set_irq(s->irq, 1);
}


static uint64_t rtt_mmio_read(void *opaque, hwaddr offset, unsigned size)
{
    RttState *s = opaque;
    uint32_t tmp;

    switch (offset) {
    case RTT_MR:
        return s->reg_mr;

    case RTT_AR:
        return s->reg_ar;

    case RTT_VR:
        return s->reg_vr;

    case RTT_SR:
        tmp = s->reg_sr;
        s->reg_sr = 0;
        qemu_set_irq(s->irq, 0);
        return tmp;

    default:
        error_report("at91.rtt: illegal read access at 0x%02lx", offset);
        abort();
    }
}

static void rtt_mmio_write(void *opaque, hwaddr offset, uint64_t value, unsigned size)
{
    RttState *s = opaque;

    switch (offset) {
    case RTT_MR:
        s->reg_mr = value;

        if (s->reg_mr & MR_RTTRST) {
            s->reg_vr = 0;
            rtt_update_timer_freq(s);
        }
        break;

    case RTT_AR:
        s->reg_ar = value;
        break;

    default:
        error_report("at91.rtt: illegal read access at 0x%02lx", offset);
        abort();
    }

    rtt_update_irq(s);
}

static const MemoryRegionOps rtt_mmio_ops = {
    .read = rtt_mmio_read,
    .write = rtt_mmio_write,
    .impl.min_access_size = 4,
    .impl.max_access_size = 4,
    .valid.min_access_size = 4,
    .valid.max_access_size = 4,
    .endianness = DEVICE_NATIVE_ENDIAN,
};


static void rtt_device_init(Object *obj)
{
    SysBusDevice *sbd = SYS_BUS_DEVICE(obj);
    RttState *s = AT91_RTT(obj);

    s->timer = ptimer_init(rtt_timer_tick, s, PTIMER_POLICY_DEFAULT);

    sysbus_init_irq(sbd, &s->irq);

    memory_region_init_io(&s->mmio, OBJECT(s), &rtt_mmio_ops, s, "at91.rtt", 0x10);
    sysbus_init_mmio(SYS_BUS_DEVICE(s), &s->mmio);
}

static void rtt_reset_registers(RttState *s)
{
    s->reg_mr = 0x8000;
    s->reg_ar = 0xFFFFFFFF;
    s->reg_vr = 0;
    s->reg_sr = 0;

    rtt_update_timer_freq(s);
}

static void rtt_device_realize(DeviceState *dev, Error **errp)
{
    RttState *s = AT91_RTT(dev);

    ptimer_transaction_begin(s->timer);
    ptimer_set_limit(s->timer, 1, 1);
    ptimer_transaction_commit(s->timer);

    rtt_reset_registers(s);
}

static void rtt_device_reset(DeviceState *dev)
{
    RttState *s = AT91_RTT(dev);

    rtt_reset_registers(s);
    qemu_set_irq(s->irq, 0);
}

static void rtt_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    dc->realize = rtt_device_realize;
    dc->reset = rtt_device_reset;
}

static const TypeInfo rtt_device_info = {
    .name = TYPE_AT91_RTT,
    .parent = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(RttState),
    .instance_init = rtt_device_init,
    .class_init = rtt_class_init,
};

static void rtt_register_types(void)
{
    type_register_static(&rtt_device_info);
}

type_init(rtt_register_types)
