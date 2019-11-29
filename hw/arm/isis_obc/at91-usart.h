#ifndef HW_ARM_ISIS_OBC_USART_H
#define HW_ARM_ISIS_OBC_USART_H

#include "qemu/osdep.h"
#include "qemu/buffer.h"
#include "hw/sysbus.h"
#include "hw/ptimer.h"

#include "at91-pdc.h"
#include "ioxfer-server.h"


#define TYPE_AT91_USART "at91-usart"
#define AT91_USART(obj) OBJECT_CHECK(UsartState, (obj), TYPE_AT91_USART)


typedef struct {
    SysBusDevice parent_obj;
    MemoryRegion mmio;
    qemu_irq irq;

    char* socket;
    IoXferServer *server;
    Buffer rcvbuf;

    unsigned mclk;
    unsigned baud;

    uint32_t reg_mr;
    uint32_t reg_imr;
    uint32_t reg_csr;
    uint32_t reg_rhr;
    uint32_t reg_brgr;
    uint32_t reg_rtor;
    uint32_t reg_ttgr;
    uint32_t reg_fidi;
    uint32_t reg_ner;
    uint32_t reg_if;
    uint32_t reg_man;

    bool rx_dma_enabled;
    bool rx_enabled;
    bool tx_enabled;

    At91Pdc pdc;
} UsartState;


void at91_usart_set_master_clock(UsartState *s, unsigned mclk);

#endif /* HW_ARM_ISIS_OBC_USART_H */
