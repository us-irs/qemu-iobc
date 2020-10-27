/*
 * I/O Transfer Server (IOX).
 *
 * See ioxfer-server.h for details.
 *
 * Copyright (c) 2019-2020 KSat e.V. Stuttgart
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or, at your
 * option, any later version. See the COPYING file in the top-level directory.
 */

#include "ioxfer-server.h"
#include "qemu/error-report.h"
#include "qapi/error.h"


static void server_accept(QIONetListener *listener, QIOChannelSocket *sioc, gpointer data);
static gboolean client_receive(QIOChannel *ioc, GIOCondition cond, gpointer data);
static gboolean client_hup(QIOChannel *ioc, GIOCondition cond, gpointer data);


static void iox_client_connect(IoXferServer *srv, QIOChannelSocket *client)
{
    QIOChannel *ioc = QIO_CHANNEL(client);

    // do not accept any new clients
    qio_net_listener_set_client_func(srv->listener, NULL, NULL, NULL);

    qio_channel_add_watch(ioc, G_IO_IN, client_receive, srv, NULL);
    qio_channel_add_watch(ioc, G_IO_HUP, client_hup, srv, NULL);

    qio_channel_set_blocking(ioc, false, &error_abort);

    srv->client = client;
}

static void iox_client_disconnect(IoXferServer *srv)
{
    if (!srv->client)
        return;

    qio_channel_close(QIO_CHANNEL(srv->client), NULL);
    srv->client = NULL;

    // we can now accept new clients again
    qio_net_listener_set_client_func(srv->listener, server_accept, srv, NULL);
}


IoXferServer *iox_server_new(void)
{
    IoXferServer *srv = g_new0(IoXferServer, 1);

    srv->listener = qio_net_listener_new();
    if (!srv->listener) {
        g_free(srv);
        return NULL;
    }

    srv->buffer_used = 0;
    srv->seq = 0;
    return srv;
}

void iox_server_free(IoXferServer *srv)
{
    iox_server_close(srv);
    g_free(srv->listener);
    g_free(srv);
}

void iox_server_set_handler(IoXferServer *srv, iox_frame_handler *handler, void* opaque)
{
    srv->handler = handler;
    srv->handler_opaque = opaque;
}


int iox_server_open(IoXferServer *srv, SocketAddress *addr, Error **errp)
{
    qio_net_listener_set_client_func(srv->listener, server_accept, srv, NULL);
    return qio_net_listener_open_sync(srv->listener, addr, 1, errp);
}

void iox_server_close(IoXferServer *srv)
{
    iox_client_disconnect(srv);

    if (qio_net_listener_is_connected(srv->listener))
        qio_net_listener_disconnect(srv->listener);
}


int iox_send_frame(IoXferServer *srv, struct iox_data_frame *frame)
{
    if (!srv || !srv->client)
        return 0;

    int len = sizeof(struct iox_data_frame) + frame->len;
    return qio_channel_write_all(QIO_CHANNEL(srv->client), (char *)frame, len, NULL);
}

int iox_send_data(IoXferServer *srv, uint8_t seq, uint8_t cat, uint8_t id, uint8_t len, uint8_t *data)
{
    uint8_t *buf = g_new0(uint8_t, sizeof(struct iox_data_frame) + len);
    struct iox_data_frame *frame = (struct iox_data_frame *)buf;

    frame->seq = seq;
    frame->cat = cat;
    frame->id = id;
    frame->len = len;
    memcpy(frame->payload, data, len);

    int status = iox_send_frame(srv, frame);
    g_free(buf);
    return status;
}

int iox_send_data_multiframe(IoXferServer *srv, uint8_t seq, uint8_t cat, uint8_t id, unsigned len, uint8_t *data)
{
    int status;

    while (len > 0xff) {
        status = iox_send_data(srv, seq, cat, id, 0xff, data);
        if (status)
            return status;

        len -= 0xff;
        data += 0xff;
    }

    return iox_send_data(srv, seq, cat, id, len, data);
}

int iox_send_command(IoXferServer *srv, uint8_t seq, uint8_t cat, uint8_t id)
{
    struct iox_data_frame frame = {
        .seq = seq,
        .cat = cat,
        .id  = id,
        .len = 0,
    };

    return iox_send_frame(srv, &frame);
}

int iox_send_u32(IoXferServer *srv, uint8_t seq, uint8_t cat, uint8_t id, uint32_t value)
{
    uint8_t buf[sizeof(struct iox_data_frame) + sizeof(uint32_t)];
    struct iox_data_frame *frame = (struct iox_data_frame *)buf;

    frame->seq = seq;
    frame->cat = cat;
    frame->id  = id;
    frame->len = sizeof(uint32_t);
    *((uint32_t *)frame->payload) = value;

    return iox_send_frame(srv, frame);
}


static void server_accept(QIONetListener *listener, QIOChannelSocket *sioc, gpointer data)
{
    IoXferServer *srv = data;

    if (srv->client) {
        qio_channel_close(QIO_CHANNEL(sioc), NULL);
        warn_report("iox: server already has a client");
        return;
    }

    iox_client_connect(srv, sioc);
}

static gboolean client_receive(QIOChannel *ioc, GIOCondition cond, gpointer data)
{
    IoXferServer *srv = data;

    while (true) {      // loop until all received data has been handled
        if (srv->buffer_used < sizeof(struct iox_data_frame)) {
            unsigned remaining = sizeof(struct iox_data_frame) - srv->buffer_used;
            char *buf = (char *)srv->buffer;

            ssize_t nread = qio_channel_read(ioc, buf, remaining, NULL);
            if (nread == QIO_CHANNEL_ERR_BLOCK || nread == 0)
                return G_SOURCE_CONTINUE;       // no more data to process
            if (nread < 0)
                return G_SOURCE_REMOVE;

            srv->buffer_used += sizeof(struct iox_data_frame);
        }

        if (srv->buffer_used >= sizeof(struct iox_data_frame)) {
            unsigned len = sizeof(struct iox_data_frame) + ((struct iox_data_frame *)srv->buffer)->len;

            if (srv->buffer_used < len) {
                unsigned remaining = len - srv->buffer_used;
                char *buf = (char *)(srv->buffer + srv->buffer_used);

                ssize_t nread = qio_channel_read(ioc, buf, remaining, NULL);
                if (nread == QIO_CHANNEL_ERR_BLOCK || nread == 0)
                    return G_SOURCE_CONTINUE;   // no more data to process
                if (nread < 0)
                    return G_SOURCE_REMOVE;

                srv->buffer_used += nread;
            }

            if (srv->buffer_used == len) {
                struct iox_data_frame *frame = (struct iox_data_frame *)srv->buffer;

                if (srv->handler)
                    srv->handler(frame, srv->handler_opaque);

                srv->buffer_used = 0;
            }
        }
    }

    return G_SOURCE_CONTINUE;
}

static gboolean client_hup(QIOChannel *ioc, GIOCondition cond, gpointer data)
{
    IoXferServer *srv = data;

    iox_client_disconnect(srv);
    return G_SOURCE_REMOVE;
}
