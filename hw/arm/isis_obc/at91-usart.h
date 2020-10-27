/*
 * AT91 Universal Synchronous/Asynchronous Receiver/Transmitter.
 *
 * Emulation of devices connected to the USART is done via outside processes
 * communicating via the IOX server (for details see ioxfer-server.h). The
 * socket address can be set via the "socket" property (as is done and defined
 * in iobc_board.c).
 *
 * Multiple operations are possible via the IOX server. For data transfer
 * these are:
 * - Transfer data from AT19 to client process (category IOX_CAT_DATA, ID
 *   IOX_CID_DATA_OUT, Payload contains raw data).
 * - Transfer data from client process to AT91 (category IOX_CAT_DATA, ID
 *   IOX_CID_DATA_IN, payload contains raw data).
 * In case of transmission from client to AT91, the IOX server sends a
 * response with a 32 bit little-endian status code. Currently this code can
 * be one of the following Unix/Linux error codes:
 * - ENXIO: The USART receiver has not been enabled on the AT91.
 * - 0: Success.
 *
 * As due to the different nature of the transport it is not possible to
 * emulate all failure modes and flags. Thus a mechanism for fault injection
 * is provided, allowing to set
 * - OVRE (category IOX_CAT_FAULT, ID IOX_CID_FAULT_OVRE)
 * - FRAME (category IOX_CAT_FAULT, ID IOX_CID_FAULT_FRAME)
 * - PARE (category IOX_CAT_FAULT, ID IOX_CID_FAULT_PARE)
 * - TIMEOUT (category IOX_CAT_FAULT, ID IOX_CID_FAULT_TIMEOUT)
 *
 * Note especially that, since the receiver timeout can not be emulated, it is
 * imperative to inject this timeout manually if communication relies on it.
 * This is the case when a receive operation is started with a buffer that may
 * be larger than the expected length of the data to be recieved. In this case
 * one would rely on the timeout to detect the end of the
 * data-frame/transmission burst. This detection cannot be reliably emulated
 * and thus the timeout has to be manually fault-injected by the sender/client
 * after the data transmission has been completed.
 *
 * Additional notes:
 * - Master clock of AT91 must be set/updated via at91_usart_set_master_clock.
 *
 * See at91-usart.c for implementation status.
 *
 * Copyright (c) 2019-2020 KSat e.V. Stuttgart
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or, at your
 * option, any later version. See the COPYING file in the top-level directory.
 */

#ifndef HW_ARM_ISIS_OBC_USART_H
#define HW_ARM_ISIS_OBC_USART_H

#include "qemu/osdep.h"
#include "qemu/buffer.h"
#include "hw/sysbus.h"

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
