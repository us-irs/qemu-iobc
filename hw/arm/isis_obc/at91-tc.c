/*
 * AT91 Timer/Counter.
 *
 * Copyright (c) 2019-2020 KSat e.V. Stuttgart
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or, at your
 * option, any later version. See the COPYING file in the top-level directory.
 */

// Overview of TODOs:
// - Burst mode, clock chaining (XP0, XP1, XP2 clock signals)
// - Digital signal outputs
// - Digital signal inputs, edge detector, ...


#include "at91-tc.h"
#include "at91-pmc.h"
#include "qemu/error-report.h"
#include "hw/irq.h"


#define TC_CCR      0x00
#define TC_CMR      0x04
#define TC_CV       0x10
#define TC_RA       0x14
#define TC_RB       0x18
#define TC_RC       0x1C
#define TC_SR       0x20
#define TC_IER      0x24
#define TC_IDR      0x28
#define TC_IMR      0x2C

#define TC_BCR      0xC0
#define TC_BMR      0xC4

#define TCC0_START  0x00
#define TCC0_END    0x2C
#define TCC1_START  0x40
#define TCC1_END    0x6C
#define TCC2_START  0x80
#define TCC2_END    0xAC


#define BCR_SYNC        BIT(0)

#define BMR_TC0XC0S(s)  (s->reg_bmr & 0x03)
#define BMR_TC1XC1S(s)  ((s->reg_bmr >> 2) & 0x03)
#define BMR_TC2XC2S(s)  ((s->reg_bmr >> 4) & 0x03)

#define CCR_CLKEN       BIT(0)
#define CCR_CLKDIS      BIT(1)
#define CCR_SWTRG       BIT(2)

#define CMR_CLKI        BIT(3)                          // common
#define CMR_WAVE        BIT(15)
#define CMR_TCCLKS(s)   (s->reg_cmr & 0x07)
#define CMR_BURST(s)    ((s->reg_cmr >> 4) & 0x03)

#define CMR_LDBSTOP     BIT(6)                          // capture mode
#define CMR_LDBDIS      BIT(7)
#define CMR_ABETRG      BIT(10)
#define CMR_CPCTRG      BIT(14)
#define CMR_ETRGEDG(s)  ((s->reg_cmr >> 8) & 0x03)
#define CMR_LDRA(s)     ((s->reg_cmr >> 16) & 0x03)
#define CMR_LDRB(s)     ((s->reg_cmr >> 18) & 0x03)

#define CMR_CPCSTOP     BIT(6)                          // waveform mode
#define CMR_CPCDIS      BIT(7)
#define CMR_ENETRG      BIT(12)
#define CMR_EEVTEDG(s)  ((s->reg_cmr >> 8) & 0x03)
#define CMR_EEVT(s)     ((s->reg_cmr >> 10) & 0x03)
#define CMR_WAVSEL(s)   ((s->reg_cmr >> 13) & 0x03)
#define CMR_ACPA(s)     ((s->reg_cmr >> 16) & 0x03)
#define CMR_ACPC(s)     ((s->reg_cmr >> 18) & 0x03)
#define CMR_AEEVT(s)    ((s->reg_cmr >> 20) & 0x03)
#define CMR_ASWTRG(s)   ((s->reg_cmr >> 22) & 0x03)
#define CMR_BCPB(s)     ((s->reg_cmr >> 24) & 0x03)
#define CMR_BCPC(s)     ((s->reg_cmr >> 26) & 0x03)
#define CMR_BEEVT(s)    ((s->reg_cmr >> 28) & 0x03)
#define CMR_BSWTRG(s)   ((s->reg_cmr >> 30) & 0x03)

#define SR_COVFS        BIT(0)
#define SR_LOVRS        BIT(1)
#define SR_CPAS         BIT(2)
#define SR_CPBS         BIT(3)
#define SR_CPCS         BIT(4)
#define SR_LDRAS        BIT(5)
#define SR_LDRBS        BIT(6)
#define SR_ETRGS        BIT(7)
#define SR_CLKSTA       BIT(16)
#define SR_MTIOA        BIT(17)
#define SR_MTIOB        BIT(18)

#define TCCLKS_TC1      0
#define TCCLKS_TC2      1
#define TCCLKS_TC3      2
#define TCCLKS_TC4      3
#define TCCLKS_TC5      4
#define TCCLKS_XC0      5
#define TCCLKS_XC1      6
#define TCCLKS_XC2      7


static void tc_irq_update(TcChanState *s)
{
    qemu_set_irq(s->irq, !!(s->reg_sr & s->reg_imr & 0xff));
}

static void tc_clk_update(TcChanState *s)
{
    unsigned clock = 0;

    // TODO: XC0, XC1, XC2

    switch (CMR_TCCLKS(s)) {
    case TCCLKS_TC1:
        clock = s->parent->mclk / 2;
        break;

    case TCCLKS_TC2:
        clock = s->parent->mclk / 8;
        break;

    case TCCLKS_TC3:
        clock = s->parent->mclk / 32;
        break;

    case TCCLKS_TC4:
        clock = s->parent->mclk / 128;
        break;

    case TCCLKS_TC5:
        clock = AT91_PMC_SLCK;
        break;

    case TCCLKS_XC0:
        error_report("XC0 clock not implemented");      // TODO
        abort();
        break;

    case TCCLKS_XC1:
        error_report("XC1 clock not implemented");      // TODO
        abort();
        break;

    case TCCLKS_XC2:
        error_report("XC2 clock not implemented");      // TODO
        abort();
        break;
    };

    // note: BURST is not implemented

    s->clk = clock;

    if (s->timer && s->clk) {
        ptimer_transaction_begin(s->timer);
        ptimer_set_freq(s->timer, s->clk);
        ptimer_transaction_commit(s->timer);
    }
}

static void tc_clk_start(TcChanState *s)
{
    if (!(s->reg_sr & SR_CLKSTA))
        return;

    ptimer_transaction_begin(s->timer);
    ptimer_set_freq(s->timer, s->clk);
    ptimer_set_limit(s->timer, 1, 0);
    ptimer_run(s->timer, 0);
    ptimer_transaction_commit(s->timer);
}

static void tc_clk_stop(TcChanState *s)
{
    ptimer_transaction_begin(s->timer);
    ptimer_stop(s->timer);
    ptimer_transaction_commit(s->timer);
}

void at91_tc_set_master_clock(TcState *s, unsigned mclk)
{
    s->mclk = mclk;

    for (int i = 0; i < AT91_TC_NUM_CHANNELS; i++)
        tc_clk_update(&s->chan[i]);
}

static void tc_trigger(TcChanState *s)
{
    if (s->reg_cmr & CMR_WAVE) {
        if (!(CMR_WAVSEL(s) & 0x01)) {      // sawtooth
            s->reg_cv = 0;
        } else {                            // triangular
            s->cstep = -(s->cstep);
        }

    } else {
        s->reg_cv = 0;
    }

    tc_clk_start(s);
}

static void tc_timer_tick(void *opaque)
{
    TcChanState *s = opaque;

    if (s->reg_cv == 0xffff)
        s->reg_sr |= SR_COVFS;

    if (s->reg_cmr & CMR_WAVE) {
        uint32_t cmp = (CMR_WAVSEL(s) & 0x02) ? s->reg_rc : 0xffff;

        if (!(CMR_WAVSEL(s) & 0x01)) {      // sawtooth
            if (s->reg_cv == cmp)
                s->reg_cv = 0;
            else
                s->reg_cv = (s->reg_cv + 1) & 0xffff;
        } else {                            // triangular
            if (s->reg_cv == cmp)
                s->cstep = -1;
            else if (s->reg_cv == 0)
                s->cstep = 1;

            s->reg_cv = (s->reg_cv + s->cstep) & 0xffff;
        }

        if (s->reg_cv == s->reg_ra)
            s->reg_sr |= SR_CPAS;

        if (s->reg_cv == s->reg_rb)
            s->reg_sr |= SR_CPAS;

        if (s->reg_cv == s->reg_rc) {
            s->reg_sr |= SR_CPCS;

            if (s->reg_cmr & CMR_CPCDIS) {
                s->reg_sr &= ~SR_CLKSTA;
                tc_clk_stop(s);
            }

            if (s->reg_cmr & CMR_CPCSTOP)
                tc_clk_stop(s);
        }

    } else {
        s->reg_cv = (s->reg_cv + 1) & 0xffff;

        if (s->reg_cv == s->reg_rc) {
            s->reg_sr |= SR_CPCS;

            if (s->reg_cmr & CMR_CPCTRG)
                s->reg_cv = 0;
        }
    }

    // not implemented: register capture on edge detection

    tc_irq_update(s);
}

static uint64_t tc_chan_mmio_read(TcChanState *s, hwaddr offset, unsigned size)
{
    switch (offset) {
    case TC_CMR:
        return s->reg_cmr;

    case TC_CV:
        return s->reg_cv;

    case TC_RA:
        return s->reg_ra;

    case TC_RB:
        return s->reg_rb;

    case TC_RC:
        return s->reg_rc;

    case TC_SR:
        {
            uint32_t tmp = s->reg_sr;
            s->reg_sr &= ~(SR_COVFS | SR_LOVRS | SR_CPAS | SR_CPBS | SR_CPCS
                           | SR_LDRAS | SR_LDRBS | SR_ETRGS);
            tc_irq_update(s);
            return tmp;
        }

    case TC_IMR:
        return s->reg_imr;

    default:
        error_report("at91.tc: illegal read access at 0x%02lx", offset);
        abort();
    }
}

static void tc_chan_mmio_write(TcChanState *s, hwaddr offset, uint64_t value, unsigned size)
{
    switch (offset) {
    case TC_CCR:
        if ((value & CCR_CLKEN) && !(value & CCR_CLKDIS)) {
            s->reg_sr |= SR_CLKSTA;
        }
        if (value & CCR_CLKDIS) {
            s->reg_sr &= ~SR_CLKSTA;
            tc_clk_stop(s);
        }
        if (value & CCR_SWTRG) {
            tc_trigger(s);
        }
        break;

    case TC_CMR:
        s->reg_cmr = value;

        if (CMR_BURST(s)) {
            error_report("at91.tc: TC_CMR:BURST not supported");
            abort();
        }

        tc_clk_update(s);
        break;

    case TC_RA:
        if (s->reg_cmr & CMR_WAVE) {
            s->reg_ra = value;
        } else {
            error_report("at91.tc: write to TC_RA while WAVE = 0");
            abort();
        }
        break;

    case TC_RB:
        if (s->reg_cmr & CMR_WAVE) {
            s->reg_rb = value;
        } else {
            error_report("at91.tc: write to TC_RB while WAVE = 0");
            abort();
        }
        break;

    case TC_RC:
        if (value > 0xffff) {
            error_report("at91.tc: write to TC_RC with value 0x%lx > 0xffff, truncating", value);
        }
        s->reg_rc = value & 0xffff;
        break;

    case TC_IER:
        s->reg_imr |= value;
        tc_irq_update(s);
        break;

    case TC_IDR:
        s->reg_imr &= ~value;
        tc_irq_update(s);
        break;

    default:
        error_report("at91.tc: illegal write access at 0x%02lx (value: 0x%02lx)", offset, value);
        abort();
    }
}


static uint64_t tc_mmio_read(void *opaque, hwaddr offset, unsigned size)
{
    TcState *s = opaque;

    switch (offset) {
    case TCC0_START ... TCC0_END:
        return tc_chan_mmio_read(&s->chan[0], offset, size);

    case TCC1_START ... TCC1_END:
        return tc_chan_mmio_read(&s->chan[1], offset - TCC1_START, size);

    case TCC2_START ... TCC2_END:
        return tc_chan_mmio_read(&s->chan[2], offset - TCC2_START, size);

    case TC_BMR:
        return s->reg_bmr;

    default:
        error_report("at91.tc: illegal read access at 0x%02lx", offset);
        abort();
    }
}

static void tc_mmio_write(void *opaque, hwaddr offset, uint64_t value, unsigned size)
{
    TcState *s = opaque;

    // debug output
    // error_report("at91.tc: write access at 0x%02lx (value: 0x%02lx)", offset, value);

    switch (offset) {
    case TCC0_START ... TCC0_END:
        tc_chan_mmio_write(&s->chan[0], offset, value, size);
        return;

    case TCC1_START ... TCC1_END:
        tc_chan_mmio_write(&s->chan[1], offset - TCC1_START, value, size);
        return;

    case TCC2_START ... TCC2_END:
        tc_chan_mmio_write(&s->chan[2], offset - TCC2_START, value, size);
        return;

    case TC_BCR:
        if (value & BCR_SYNC) {
            for (int i = 0; i < AT91_TC_NUM_CHANNELS; i++)
                tc_trigger(&s->chan[i]);
        }
        return;

    case TC_BMR:
        s->reg_bmr = value;
        // TODO: update clock?
        return;

    default:
        error_report("at91.tc: illegal write access at 0x%02lx (value: 0x%02lx)", offset, value);
        abort();
    }
}

static const MemoryRegionOps tc_mmio_ops = {
    .read = tc_mmio_read,
    .write = tc_mmio_write,
    .impl.min_access_size = 4,
    .impl.max_access_size = 4,
    .valid.min_access_size = 4,
    .valid.max_access_size = 4,
    .endianness = DEVICE_NATIVE_ENDIAN,
};


static void tc_device_init(Object *obj)
{
    SysBusDevice *sbd = SYS_BUS_DEVICE(obj);
    TcState *s = AT91_TC(obj);

    for (int i = 0; i < AT91_TC_NUM_CHANNELS; i++) {
        s->chan[i].parent = s;
        s->chan[i].timer = ptimer_init(tc_timer_tick, &s->chan[i], PTIMER_POLICY_DEFAULT);
        sysbus_init_irq(sbd, &s->chan[i].irq);
    }

    memory_region_init_io(&s->mmio, OBJECT(s), &tc_mmio_ops, s, "at91.tc", 0x4000);
    sysbus_init_mmio(SYS_BUS_DEVICE(s), &s->mmio);
}

static void tc_reset_registers(TcState *s)
{
    s->reg_bmr = 0;

    for (int i = 0; i < AT91_TC_NUM_CHANNELS; i++) {
        s->chan[i].cstep   = 1;
        s->chan[i].reg_cmr = 0;
        s->chan[i].reg_cv  = 0;
        s->chan[i].reg_ra  = 0;
        s->chan[i].reg_rb  = 0;
        s->chan[i].reg_rc  = 0;
        s->chan[i].reg_sr  = 0;
        s->chan[i].reg_imr = 0;
    }
}

static void tc_device_realize(DeviceState *dev, Error **errp)
{
    TcState *s = AT91_TC(dev);
    tc_reset_registers(s);
}

static void tc_device_reset(DeviceState *dev)
{
    TcState *s = AT91_TC(dev);
    tc_reset_registers(s);
}

static void tc_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    dc->realize = tc_device_realize;
    dc->reset = tc_device_reset;
}

static const TypeInfo tc_device_info = {
    .name = TYPE_AT91_TC,
    .parent = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(TcState),
    .instance_init = tc_device_init,
    .class_init = tc_class_init,
};

static void tc_register_types(void)
{
    type_register_static(&tc_device_info);
}

type_init(tc_register_types)
