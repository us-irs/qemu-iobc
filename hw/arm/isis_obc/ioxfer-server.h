#ifndef HW_ARM_ISIS_OBC_IOXFER_SERVER_H
#define HW_ARM_ISIS_OBC_IOXFER_SERVER_H

#include "qemu/osdep.h"
#include "qemu/buffer.h"
#include "io/channel-socket.h"
#include "io/net-listener.h"


#define IOX_SEQ_DIRECTION_SET_IN(x)     ((x) & ~BIT(7))
#define IOX_SEQ_DIRECTION_SET_OUT(x)    ((x) | BIT(7))


__attribute__ ((packed))
struct iox_data_frame {
    uint8_t seq;            // sequence number
    uint8_t cat;            // command category
    uint8_t id;             // command ID
    uint8_t len;            // payload length
    uint8_t payload[0];     // payload
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
