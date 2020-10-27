/*
 * I/O Transfer Server (IOX).
 *
 * Framework to simplify I/O access to the devices emulated by QEMU from
 * outside of the emulator, e.g. python scripts emulating devices connected to
 * USART/SPI/TWI. Defines a common data frame format with commands described
 * by category, ID, and payload (see struct iox_data_frame). Details, such as
 * category, ID, payload values and socket address depend on the device
 * implementing this server. Currently only supports unix domain sockets but
 * extension to/replacement with TCP is possible. The IOX server can entertain
 * multiple clients.
 *
 * The goal of this framework is a easy-to-setup easy-to-use server
 * facilitating communication with external processes via a common interface.
 * To this end, the basic unit of communication is the struct iox_data_frame.
 * This structure contains, in order, the following four bytes:
 * - Sequence ID: A sequence ID which can be used for request-response
 *   scenarios or in general link multiple data frames together. The 7th bit of
 *   the sequence ID contains the direction of travel for this frame, with the
 *   bit being zero for frames transmitted from the server to the client
 *   (output) and one for the other direction (input).
 * - Command category: The general category of the command.
 * - Command ID: The ID of the command. Together with the command category,
 *   they describe a unique functionality of the device. A command ID is not
 *   necessarily unique by itself, only in combination with the category.
 * - Payload length: The size of the payload in bytes, up to 255.
 * This structure is followed immediately by the payload itself, if there is
 * any. The payload size is constrained by the maximal value for the payload
 * length field, meaning 255, but device implementations may choose to allow
 * multi-frame transfers, meaning multiple frames being chained together (e.g.
 * via the same sequence number) to allow larger payloads by concatenating
 * them (though this is currently not implemented in any such device).
 *
 * Copyright (c) 2019-2020 KSat e.V. Stuttgart
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or, at your
 * option, any later version. See the COPYING file in the top-level directory.
 */

#ifndef HW_ARM_ISIS_OBC_IOXFER_SERVER_H
#define HW_ARM_ISIS_OBC_IOXFER_SERVER_H

#include "qemu/osdep.h"
#include "qemu/buffer.h"
#include "io/channel-socket.h"
#include "io/net-listener.h"

#define IOX_SEQ_DIRECTION_SET_IN(x)     ((x) & ~BIT(7))
#define IOX_SEQ_DIRECTION_SET_OUT(x)    ((x) | BIT(7))

/*
 * The data frame transmitted and expected by the IOX server.
 *
 * Command cateogry, ID, and payload depend on the endpoint/device
 * implementing this server.
 */
__attribute__ ((packed))
struct iox_data_frame {
    uint8_t seq;            // sequence number, bit 7 indicates direction (in: 0 / out: 1)
    uint8_t cat;            // command category
    uint8_t id;             // command ID
    uint8_t len;            // payload length
    uint8_t payload[0];     // payload (variable length, lenght given by "len" field)
};

typedef void(iox_frame_handler)(struct iox_data_frame *cmd, void* opaque);


typedef struct {
    QIONetListener *listener;
    QIOChannelSocket *client;

    iox_frame_handler *handler;
    void *handler_opaque;

    uint8_t buffer[sizeof(struct iox_data_frame) + 256];
    unsigned buffer_used;

    uint8_t seq;
} IoXferServer;


IoXferServer *iox_server_new(void);
void iox_server_free(IoXferServer *srv);

void iox_server_set_handler(IoXferServer *srv, iox_frame_handler *handler, void* opaque);
int iox_server_open(IoXferServer *srv, SocketAddress *addr, Error **errp);
void iox_server_close(IoXferServer *srv);

static inline uint8_t iox_next_seqid(IoXferServer *srv)
{
    if (!srv)
        return IOX_SEQ_DIRECTION_SET_OUT(0);

    srv->seq = IOX_SEQ_DIRECTION_SET_OUT(srv->seq + 1);
    return srv->seq;
}

int iox_send_frame(IoXferServer *srv, struct iox_data_frame *frame);
int iox_send_data(IoXferServer *srv, uint8_t seq, uint8_t cat, uint8_t id, uint8_t len, uint8_t *data);
int iox_send_data_multiframe(IoXferServer *srv, uint8_t seq, uint8_t cat, uint8_t id, unsigned len, uint8_t *data);
int iox_send_command(IoXferServer *srv, uint8_t seq, uint8_t cat, uint8_t id);

int iox_send_u32(IoXferServer *srv, uint8_t seq, uint8_t cat, uint8_t id, uint32_t value);

static inline int iox_send_frame_new(IoXferServer *srv, struct iox_data_frame *frame)
{
    frame->seq = iox_next_seqid(srv);
    return iox_send_frame(srv, frame);
}

static inline int iox_send_data_new(IoXferServer *srv, uint8_t cat,
                                          uint8_t id, uint8_t len, uint8_t *data)
{
    return iox_send_data(srv, iox_next_seqid(srv), cat, id, len, data);
}

static inline int iox_send_data_multiframe_new(IoXferServer *srv, uint8_t cat,
                                          uint8_t id, unsigned len, uint8_t *data)
{
    return iox_send_data_multiframe(srv, iox_next_seqid(srv), cat, id, len, data);
}

static inline int iox_send_command_new(IoXferServer *srv, uint8_t cat, uint8_t id)
{
    return iox_send_command(srv, iox_next_seqid(srv), cat, id);
}

static inline int iox_send_u32_new(IoXferServer *srv, uint8_t cat, uint8_t id, uint32_t value)
{
    return iox_send_u32(srv, iox_next_seqid(srv), cat, id, value);
}

static inline int iox_send_u32_resp(IoXferServer *srv, struct iox_data_frame *frame, uint32_t value)
{
    return iox_send_u32(srv, frame->seq, frame->cat, frame->id, value);
}

#endif /* HW_ARM_ISIS_OBC_IOXFER_SERVER_H */
