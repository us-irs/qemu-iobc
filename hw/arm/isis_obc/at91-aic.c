/*
 * AT91 Advanced Interrupt Controller.
 *
 * See at91-aic.h for details.
 *
 * Copyright (c) 2019-2020 KSat e.V. Stuttgart
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or, at your
 * option, any later version. See the COPYING file in the top-level directory.
 */

#include "at91-aic.h"
#include "qemu/error-report.h"
#include "hw/irq.h"

#define AIC_SMR0            0x000
#define AIC_SMR31           0x07C
#define AIC_SVR0            0x080
#define AIC_SVR31           0x0FC
#define AIC_IVR             0x100
#define AIC_FVR             0x104
#define AIC_ISR             0x108
#define AIC_IPR             0x10C
#define AIC_IMR             0x110
#define AIC_CISR            0x114
#define AIC_IECR            0x120
#define AIC_IDCR            0x124
#define AIC_ICCR            0x128
#define AIC_ISCR            0x12C
#define AIC_EOICR           0x130
#define AIC_SPU             0x134
#define AIC_DCR             0x138
#define AIC_FFER            0x140
#define AIC_FFDR            0x144
#define AIC_FFSR            0x148

#define CISR_NIRQ           0x01
#define CISR_NFIQ           0x02

#define DCR_PROT            0x01
#define DCR_GMSK            0x02

#define ST_ACTIVE_LOW       0x00
#define ST_ACTIVE_FALLING   0x01
#define ST_ACTIVE_HIGH      0x02
#define ST_ACTIVE_RISING    0x03
#define ST_EDGE_MASK        0x01

#define IRQ_PRIO_LOWEST     0
#define IRQ_PRIO_HIGHEST    7
#define IRQ_PRIO_SPURIOUS   8
#define IRQ_NUM_SPURIOUS    0xFF


inline static uint8_t aic_irq_get_priority(AicState *s, uint8_t irq)
{
    return s->reg_smr[irq] & 7;
}

inline static uint8_t aic_irq_get_type(AicState *s, uint8_t irq)
{
    uint8_t srctype = (s->reg_smr[irq] & 0x60) >> 5;

    // internal sources are only configurable to be ACTIVE_HIGH or ACTIVE_RISING
    if (0 < irq && irq < 29) {
        if (srctype == ST_ACTIVE_LOW)
            return ST_ACTIVE_HIGH;
        if (srctype == ST_ACTIVE_FALLING)
            return ST_ACTIVE_RISING;
    }

    return srctype;
}

inline static bool aic_irq_is_edge_triggered(AicState *s, uint8_t irq)
{
    return aic_irq_get_type(s, irq) & ST_EDGE_MASK;
}

inline static bool aic_irq_is_level_triggered(AicState *s, uint8_t irq)
{
    return !aic_irq_is_edge_triggered(s, irq);
}

inline static bool aic_irq_is_fast(AicState *s, uint8_t irq)
{
    return !!((s->reg_ffsr | 0x01) & (1 << irq));
}


static int aic_irq_get_highest_pending(AicState *s)
{
    uint32_t pending = s->reg_ipr & s->reg_imr & ~s->reg_ffsr;
    int h_irq = -1;
    int h_pri = -1;
    int c_irq;
    int c_pri;

    // SPEC: If several interrupt sources of equal priority are pending and
    // enabled when the AIC_IVR is read, the interrupt with the lowest
    // interrupt source number is serviced first.

    // deliberately skip FIQ (irq=0) as this is the fast irq
    for (c_irq = 1; c_irq < 32; c_irq++) {
        c_pri = aic_irq_get_priority(s, c_irq);
        if ((pending & (1 << c_irq)) && c_pri > h_pri) {
            h_irq = c_irq;
            h_pri = c_pri;
        }
    }

    return h_irq;
}


inline static void aic_irq_stack_push(AicState *s, uint8_t irq, uint8_t pri)
{
    if (s->irq_stack_pos >= 8) {
        error_report("at91.aic: too many interrupts");
        abort();
    }

    s->irq_stack_pos += 1;
    s->irq_stack[s->irq_stack_pos].irq = irq;
    s->irq_stack[s->irq_stack_pos].irq = pri;
}

inline static void aic_irq_stack_pop(AicState *s)
{
    if (s->irq_stack_pos >= 0) {
        s->irq_stack_pos -= 1;
    }
}

inline static AicIrqStackElem *aic_irq_stack_top(AicState *s)
{
    if (s->irq_stack_pos < 0) {
        return NULL;
    }

    return &s->irq_stack[s->irq_stack_pos];
}


static void aic_core_irq_update(AicState *s)
{
    AicIrqStackElem *current = aic_irq_stack_top(s);
    uint32_t irq_pending = s->reg_ipr & s->reg_imr;
    uint32_t irq_fast = s->reg_ffsr | 1;
    bool nirq;
    bool nfiq;
    int irq;

    if (s->reg_dcr & DCR_GMSK) {
        s->reg_cisr = 0;
    } else {
        nfiq = irq_pending & irq_fast;
        nirq = irq_pending & ~irq_fast;

        if (nirq && current) {
            irq = aic_irq_get_highest_pending(s);
            nirq = aic_irq_get_priority(s, irq) > current->pri;
        }

        s->reg_cisr = (nirq ? CISR_NIRQ : 0) | (nfiq ? CISR_NFIQ : 0);
    }

    qemu_set_irq(s->fiq, !!(s->reg_cisr & CISR_NFIQ));
    qemu_set_irq(s->irq, !!(s->reg_cisr & CISR_NIRQ));
}


static void aic_irq_handle(void *opaque, int n, int level)
{
    AicState *s = AT91_AIC(opaque);
    const uint32_t mask = 1 << n;
    const uint32_t newbit = (!!level) << n;
    bool active = false;

    // check for rising/falling edges
    if ((s->line_state & mask) != newbit && level) {            // rising edge
        active = aic_irq_get_type(s, n) == ST_ACTIVE_RISING;
    } else if ((s->line_state & mask) != newbit && !level) {    // falling edge
        active = aic_irq_get_type(s, n) == ST_ACTIVE_FALLING;
    }
    s->line_state = (s->line_state & ~mask) | newbit;

    // check for high/low state
    if (level) {                                                // high
        active |= aic_irq_get_type(s, n) == ST_ACTIVE_HIGH;
    } else {                                                    // low
        active |= aic_irq_get_type(s, n) == ST_ACTIVE_LOW;
    }

    if (active) {
        s->reg_ipr |= mask;
    } else if (!aic_irq_is_edge_triggered(s, n)) {
        // edge-triggered IRQs are cleared during handling, only clear
        // level-triggered
        s->reg_ipr &= ~mask;
    }

    aic_core_irq_update(s);
}


static uint64_t aic_mmio_read(void *opaque, hwaddr offset, unsigned size)
{
    AicState *s = opaque;
    AicIrqStackElem *elem;
    int irq;

    if (size != 0x04) {
        error_report("at91.aic illegal read access at 0x%03lx with size: 0x%02x", offset, size);
        abort();
    }

    switch (offset) {
    case AIC_SMR0 ... AIC_SMR31:
        return s->reg_smr[(offset - AIC_SMR0) / 4];

    case AIC_SVR0 ... AIC_SVR31:
        return s->reg_svr[(offset - AIC_SVR0) / 4];

    case AIC_IVR:   // entry point to interrupt handling
        irq = aic_irq_get_highest_pending(s);

        if (!(s->reg_dcr & DCR_PROT)) {
            if (irq < 0) {      // handle spurious interrupt
                aic_irq_stack_push(s, IRQ_NUM_SPURIOUS, IRQ_PRIO_SPURIOUS);
            } else {            // handle normal interrupt
                aic_irq_stack_push(s, irq, aic_irq_get_priority(s, irq));

                // automatic clear for edge-triggered non-fast-forced interrupts
                if (aic_irq_is_edge_triggered(s, irq) && !aic_irq_is_fast(s, irq)) {
                    s->reg_ipr &= ~(1 << irq);
                }
            }

            // de-assert nIRQ line
            aic_core_irq_update(s);
        }

        if (irq < 0) {
            return s->reg_spu;
        } else {
            return s->reg_svr[irq];
        }

    case AIC_FVR:
        if (s->reg_ipr & (s->reg_ffsr | 1)) {
            if ((s->reg_ipr & 1) && aic_irq_is_edge_triggered(s, 0)) {
                s->reg_ipr &= ~1;               // clear FIQ pending bit
                aic_core_irq_update(s);
            }

            return s->reg_svr[0];
        } else {                                // spurious interrupt
            return s->reg_spu;
        }

    case AIC_ISR:
        // FIXME: handle fast interrupts?
        elem = aic_irq_stack_top(s);
        if (!elem) {
            error_report("at91.aic: read access to ISR while no interrupt is active");
            abort();
        }
        if (elem->irq == IRQ_NUM_SPURIOUS) {
            error_report("at91.aic: read access to ISR while handling spurious interrupt");
            abort();
        }
        return elem->irq;

    case AIC_IPR:
        return s->reg_ipr;

    case AIC_IMR:
        return s->reg_imr;

    case AIC_CISR:
        return s->reg_cisr;

    case AIC_SPU:
        return s->reg_spu;

    case AIC_DCR:
        return s->reg_dcr;

    case AIC_FFSR:
        return s->reg_ffsr;

    default:
        error_report("at91.aic illegal read access at 0x%03lx", offset);
        abort();
    }
}

static void aic_mmio_write(void *opaque, hwaddr offset, uint64_t value, unsigned size)
{
    AicState *s = opaque;
    int irq;

    if (size != 0x04) {
        error_report("at91.aic illegal write access at 0x%03lx with size: 0x%02x [value: 0x%08lx]",
                     offset, size, value);
        abort();
    }

    switch (offset) {
    case AIC_SMR0 ... AIC_SMR31:
        s->reg_smr[(offset - AIC_SMR0) / 4] = value;
        break;

    case AIC_SVR0 ... AIC_SVR31:
        s->reg_svr[(offset - AIC_SVR0) / 4] = value;
        break;

    case AIC_IVR:
        if (s->reg_dcr & DCR_PROT) {    // only valid in protect mode
            irq = aic_irq_get_highest_pending(s);

            if (irq < 0) {      // handle spurious interrupt
                aic_irq_stack_push(s, IRQ_NUM_SPURIOUS, IRQ_PRIO_SPURIOUS);
            } else {            // handle normal interrupt
                aic_irq_stack_push(s, irq, aic_irq_get_priority(s, irq));

                // automatic clear for edge-triggered non-fast-forced interrupts
                if (aic_irq_is_edge_triggered(s, irq) && !aic_irq_is_fast(s, irq)) {
                    s->reg_ipr &= ~(1 << irq);
                }
            }
        }
        break;

    case AIC_IECR:
        s->reg_imr |= value;
        break;

    case AIC_IDCR:
        s->reg_imr &= ~value;
        break;

    case AIC_ICCR:
        // can only clear edge-triggered interrupts
        for (irq = 0; irq < 32; irq++) {
            if (!aic_irq_is_edge_triggered(s, irq))
                value &= ~(1 << irq);
        }
        s->reg_ipr &= ~value;
        break;

    case AIC_ISCR:
        // can only set edge-triggered interrupts
        for (irq = 0; irq < 32; irq++) {
            if (!aic_irq_is_edge_triggered(s, irq))
                value &= ~(1 << irq);
        }
        s->reg_ipr |= value;

    case AIC_EOICR:
        aic_irq_stack_pop(s);
        break;

    case AIC_SPU:
        s->reg_spu = value;
        break;

    case AIC_DCR:
        s->reg_dcr = value;
        break;

    case AIC_FFER:
        s->reg_ffsr |= value;
        break;

    case AIC_FFDR:
        s->reg_ffsr &= ~value;
        break;

    default:
        error_report("at91.aic illegal write access at "
                      "0x%03lx [value: 0x%08lx]", offset, value);
        abort();
    }

    aic_core_irq_update(s);
}

static const MemoryRegionOps aic_mmio_ops = {
    .read = aic_mmio_read,
    .write = aic_mmio_write,
    .impl.min_access_size = 4,
    .impl.max_access_size = 4,
    .valid.min_access_size = 4,
    .valid.max_access_size = 4,
    .endianness = DEVICE_NATIVE_ENDIAN,
};


static void aic_reset_registers(AicState *s)
{
    int i;

    for (i = 0; i < 32; i++) {
        s->reg_smr[i] = 0;
        s->reg_svr[i] = 0;
    }

    s->reg_ipr  = 0;
    s->reg_imr  = 0;
    s->reg_cisr = 0;
    s->reg_spu  = 0;
    s->reg_dcr  = 0;
    s->reg_ffsr = 0;
}

static void aic_device_init(Object *obj)
{
    SysBusDevice *sbd = SYS_BUS_DEVICE(obj);
    AicState *s = AT91_AIC(obj);

    sysbus_init_irq(sbd, &s->irq);
    sysbus_init_irq(sbd, &s->fiq);

    qdev_init_gpio_in_named(DEVICE(s), aic_irq_handle, "irq-line", 32);

    memory_region_init_io(&s->mmio, OBJECT(s), &aic_mmio_ops, s, "at91.aic", 0x200);
    sysbus_init_mmio(SYS_BUS_DEVICE(s), &s->mmio);
}

static void aic_device_realize(DeviceState *dev, Error **errp)
{
    AicState *s = AT91_AIC(dev);

    aic_reset_registers(s);
    s->irq_stack_pos = -1;
    s->line_state = 0;
}

static void aic_device_reset(DeviceState *dev)
{
    AicState *s = AT91_AIC(dev);

    aic_reset_registers(s);
    s->irq_stack_pos = -1;
    s->line_state = 0;
}

static void aic_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    dc->realize = aic_device_realize;
    dc->reset = aic_device_reset;
}

static const TypeInfo aic_device_info = {
    .name = TYPE_AT91_AIC,
    .parent = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(AicState),
    .instance_init = aic_device_init,
    .class_init = aic_class_init,
};

static void aic_register_types(void)
{
    type_register_static(&aic_device_info);
}

type_init(aic_register_types)
