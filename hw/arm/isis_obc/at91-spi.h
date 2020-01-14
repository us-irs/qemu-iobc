#ifndef HW_ARM_ISIS_OBC_SPI_H
#define HW_ARM_ISIS_OBC_SPI_H

#include "qemu/osdep.h"
#include "hw/sysbus.h"
#include "hw/ptimer.h"

#include "at91-pdc.h"
#include "ioxfer-server.h"


#define TYPE_AT91_SPI "at91-spi"
#define AT91_SPI(obj) OBJECT_CHECK(SpiState, (obj), TYPE_AT91_SPI)


enum wait_rcv_type {
    AT91_SPI_WAIT_RCV_NONE,
    AT91_SPI_WAIT_RCV_TDR,
    AT91_SPI_WAIT_RCV_DMA,
};


typedef struct {
    SysBusDevice parent_obj;

    MemoryRegion mmio;
    qemu_irq irq;

    char* socket;
    IoXferServer *server;
    Buffer rcvbuf;

    unsigned mclk;

    uint32_t reg_mr;
    uint32_t reg_sr;
    uint32_t reg_imr;
    uint32_t reg_rdr;
    uint32_t reg_tdr;
    uint32_t reg_csr[4];

    uint16_t serializer;
    bool dma_rx_enabled;
    bool dma_tx_enabled;

    struct {
        enum wait_rcv_type ty;
        uint32_t n;
    } wait_rcv;

    At91Pdc pdc;
} SpiState;

void at91_spi_set_master_clock(SpiState *s, unsigned mclk);

#endif /* HW_ARM_ISIS_OBC_SPI_H */
