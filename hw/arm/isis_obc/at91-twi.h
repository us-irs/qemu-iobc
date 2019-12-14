#ifndef HW_ARM_ISIS_OBC_TWI_H
#define HW_ARM_ISIS_OBC_TWI_H

#include "qemu/osdep.h"
#include "hw/sysbus.h"
#include "hw/ptimer.h"

#include "at91-pdc.h"
#include "ioxfer-server.h"


#define TYPE_AT91_TWI "at91-twi"
#define AT91_TWI(obj) OBJECT_CHECK(TwiState, (obj), TYPE_AT91_TWI)


typedef enum {
    AT91_TWI_MODE_OFFLINE,
    AT91_TWI_MODE_MASTER,
    AT91_TWI_MODE_SLAVE,
} TwiMode;

typedef struct {
    SysBusDevice parent_obj;

    MemoryRegion mmio;
    qemu_irq irq;

    char* socket;
    IoXferServer *server;
    Buffer rcvbuf;
    Buffer sendbuf;
    ptimer_state *chrtx_timer;

    TwiMode mode;
    unsigned mclk;
    unsigned clock;

    uint32_t reg_mmr;
    uint32_t reg_smr;
    uint32_t reg_iadr;
    uint32_t reg_cwgr;
    uint32_t reg_sr;
    uint32_t reg_imr;
    uint32_t reg_rhr;

    At91Pdc pdc;
    bool dma_rx_enabled;
} TwiState;


void at91_twi_set_master_clock(TwiState *s, unsigned mclk);

#endif /* HW_ARM_ISIS_OBC_TWI_H */
