/*
 * AT91 Serial Peripheral Interface.
 *
 * Emulation of devices connected to the SPI is done via outside processes
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
 * Particular care should be taken regarding the synchronous transmit/receive
 * nature of the SPI interface: SPI tranfers can only read and write at the
 * same time, meaning when data is being sent by the AT91, it intrinsically
 * receives the same amount of data at the same time. Due to this, as soon as
 * the AT91 (master mode) initiates a data transfer (sends data), the
 * emulation is paused until the client has sent back the same amount of data,
 * which is considered to be read during the transmit operation. Failure of
 * the client to send this data will block the emulation. Excess data is
 * ignored. In essence, a client for the AT91 SPI in master mode should always
 * follow up a data frame receival by sending the exact same amount of data
 * back.
 *
 * As due to the different nature of the transport it is not possible to
 * emulate all failure modes and flags. Thus a mechanism for fault injection
 * is provided, allowing to set
 * - MODF (category IOX_CAT_FAULT, ID IOX_CID_FAULT_MODF)
 * - OVRES (category IOX_CAT_FAULT, ID IOX_CID_FAULT_OVRES)
 *
 * Additional notes:
 * - Master clock of AT91 must be set/updated via at91_spi_set_master_clock.
 *
 * See at91-spi.c for implementation status.
 *
 * Copyright (c) 2019-2020 KSat e.V. Stuttgart
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or, at your
 * option, any later version. See the COPYING file in the top-level directory.
 */

#ifndef HW_ARM_ISIS_OBC_SPI_H
#define HW_ARM_ISIS_OBC_SPI_H

#include "qemu/osdep.h"
#include "hw/sysbus.h"

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
