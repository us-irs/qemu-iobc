#include "at91-dbgu.h"
#include "qemu/error-report.h"


// TODO: export chip IDs as properties

#define IOBC_CIDR       0x00000000      // TODO
#define IOBC_EXID       0x00000000      // TODO

#define DBGU_CR         0x00
#define DBGU_MR         0x04
#define DBGU_IER        0x08
#define DBGU_IDR        0x0C
#define DBGU_IMR        0x10
#define DBGU_SR         0x14
#define DBGU_RHR        0x18
#define DBGU_THR        0x1C
#define DBGU_BRGR       0x20
#define DBGU_CIDR       0x40
#define DBGU_EXID       0x44
#define DBGU_FNR        0x48

#define PDC_AREA_OFFS   0x100
#define PDC_AREA_LEN    0x024


#define CR_RSTRX        (1 << 2)
#define CR_RSTTX        (1 << 3)
#define CR_RXEN         (1 << 4)
#define CR_RXDIS        (1 << 5)
#define CR_TXEN         (1 << 6)
#define CR_TXDIS        (1 << 7)
#define CR_RSTSTA       (1 << 8)

#define SR_RXRDY        (1 << 0)
#define SR_TXRDY        (1 << 1)
#define SR_ENDRX        (1 << 3)
#define SR_ENDTX        (1 << 4)
#define SR_OVRE         (1 << 5)
#define SR_FRAME        (1 << 6)
#define SR_PARE         (1 << 7)
#define SR_TXEMPTY      (1 << 9)
#define SR_TXBUFE       (1 << 11)
#define SR_RXBUFF       (1 << 12)
#define SR_COMMTX       (1 << 30)
#define SR_COMMRX       (1 << 31)


static int dbgu_uart_can_receive(void *opaque)
{
    return 1;   // TODO
}

static void dbgu_uart_receive(void *opaque, const uint8_t *buf, int size)
{
    // TODO
}


static uint64_t dbgu_mmio_read(void *opaque, hwaddr offset, unsigned size)
{
    DbguState *s = opaque;

    if (size != 0x04) {
        error_report("at91.dbgu illegal read access at "
                      "0x%03lx with size: 0x%02x", offset, size);
        abort();
    }

    info_report("at91.dbgu read 0x%03lx", offset);

    if (offset >= PDC_AREA_OFFS && offset < PDC_AREA_OFFS + PDC_AREA_LEN) {
    }

    switch (offset) {
    case DBGU_MR:
        return s->reg_mr;

    case DBGU_IMR:
        return s->reg_imr;

    case DBGU_SR:
        return s->reg_sr;

    case DBGU_RHR:
        s->reg_sr &= ~SR_RXRDY;
        // TODO: also clear RXBUFF?
        // TODO: update interrupts?
        return s->reg_rhr;

    case DBGU_BRGR:
        return s->reg_brgr;

    case DBGU_CIDR:
        return s->reg_cidr;

    case DBGU_EXID:
        return s->reg_exid;

    case DBGU_FNR:
        return s->reg_fnr;

    case PDC_AREA_OFFS ... PDC_AREA_OFFS + PDC_AREA_LEN - 1:
        error_report("at91.dbgu: PDC area is unimplemented");
        abort();    // TODO: implement PDC support

    default:
        error_report("at91.dbgu illegal read access at 0x%03lx", offset);
        abort();
    }
}

static void dbgu_mmio_write(void *opaque, hwaddr offset, uint64_t value, unsigned size)
{
    DbguState *s = opaque;
    uint8_t ch;

    if (size != 0x04) {
        error_report("at91.dbgu illegal write access at "
                      "0x%03lx with size: 0x%02x [value: 0x%08lx]",
                      offset, size, value);
        abort();
    }

    info_report("at91.dbgu write 0x%03lx [value: 0x%08lx]", offset, value);

    switch (offset) {
    case DBGU_CR:
        if (value & CR_RSTRX) {     // reset and disable receiver
            // TODO: reset receiver, clear/set buffer flags?
            s->rx_enabled = false;
        }
        if (value & CR_RSTTX) {     // reset and disable transmitter
            // TODO: reset transmitter, set txrdy|txbufe|txempty?
            s->tx_enabled = false;
        }
        if (value & CR_RXEN) {      // enable receiver
            s->rx_enabled = true;
        }
        if (value & CR_RXDIS) {     // disable receiver (overrides RXEN)
            s->rx_enabled = false;
        }
        if (value & CR_TXEN) {      // enable transmitter
            s->tx_enabled = true;
        }
        if (value & CR_TXDIS) {     // disable transmitter (overrides TXDIS)
            s->tx_enabled = false;
        }
        if (value & CR_RSTSTA) {    // reset status bits
            s->reg_sr &= ~(SR_PARE | SR_FRAME | SR_OVRE);
        }
        break;

    case DBGU_MR:
        s->reg_mr = value;
        // TODO: update mode
        break;

    case DBGU_IER:
        s->reg_imr |= ~value;
        break;

    case DBGU_IDR:
        s->reg_imr &= ~value;
        break;

    case DBGU_THR:
        ch = (uint8_t)value;
        qemu_chr_fe_write_all(&s->chr, &ch, 1);
        // TODO: initiate transmission, set TXEMPTY when done
        break;

    case DBGU_BRGR:
        s->reg_brgr = value;
        // TODO: update baud rate
        break;

    case DBGU_FNR:
        s->reg_fnr = value;
        warn_report("at91.dbgu: FNR register writes not implemented");
        break;

    case PDC_AREA_OFFS ... PDC_AREA_OFFS + PDC_AREA_LEN - 1:
        error_report("at91.dbgu: PDC area is unimplemented");
        // TODO: implement PDC support
        break;

    default:
        error_report("at91.dbgu illegal write access at "
                      "0x%03lx [value: 0x%08lx]", offset, value);
        abort();
    }

    // TODO: update interrupts?
}

static const MemoryRegionOps dbgu_mmio_ops = {
    .read = dbgu_mmio_read,
    .write = dbgu_mmio_write,
    .endianness = DEVICE_NATIVE_ENDIAN,
};

static Property dbgu_device_properties[] = {
    DEFINE_PROP_CHR("chardev", DbguState, chr),
    DEFINE_PROP_END_OF_LIST(),
};

static void dbgu_reset_registers(DbguState *s)
{
    // indicate transmitter ready
    s->reg_sr = SR_TXRDY | SR_TXBUFE | SR_TXEMPTY;

    s->reg_mr   = 0x00;
    s->reg_imr  = 0x00;
    s->reg_rhr  = 0x00;
    s->reg_brgr = 0x00;
    s->reg_fnr  = 0x00;

    s->reg_cidr = IOBC_CIDR;
    s->reg_exid = IOBC_EXID;

    s->rx_enabled = false;
    s->tx_enabled = false;
}

static void dbgu_device_init(Object *obj)
{
    DbguState *s = AT91_DBGU(obj);

    memory_region_init_io(&s->mmio, OBJECT(s), &dbgu_mmio_ops, s, "at91.dbgu", 0x200);
    sysbus_init_mmio(SYS_BUS_DEVICE(s), &s->mmio);
}

static void dbgu_device_realize(DeviceState *dev, Error **errp)
{
    DbguState *s = AT91_DBGU(dev);

    dbgu_reset_registers(s);
    qemu_chr_fe_set_handlers(&s->chr, dbgu_uart_can_receive, dbgu_uart_receive,
                             NULL, NULL, s, NULL, true);
}

static void dbgu_device_reset(DeviceState *dev)
{
    dbgu_reset_registers(AT91_DBGU(dev));
}

static void dbgu_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    dc->realize = dbgu_device_realize;
    dc->reset = dbgu_device_reset;
    dc->props = dbgu_device_properties;
}

static const TypeInfo dbgu_device_info = {
    .name = TYPE_AT91_DBGU,
    .parent = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(DbguState),
    .instance_init = dbgu_device_init,
    .class_init = dbgu_class_init,
};

static void dbgu_register_types(void)
{
    type_register_static(&dbgu_device_info);
}

type_init(dbgu_register_types)
