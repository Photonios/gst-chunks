#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <dirent.h>

#include <gst/gst.h>
#include <gst/rtsp-server/rtsp-server.h>

#include <gcs/dir.h>
#include <gcs/chunk.h>
#include <gcs/index.h>
#include <gcs/mem.h>

#define SERVER_PORT 8554

static GMainLoop *loop;

typedef struct {
    GstRTSPServer *server;
} SERVER_DATA;

typedef struct {
    SERVER_DATA *server_data;
    GstRTSPContext *context;
} CLIENT_DATA;

static SERVER_DATA *
create_server_data()
{
    SERVER_DATA *data = ALLOC_NULL(SERVER_DATA *, sizeof(SERVER_DATA));
    data->server = gst_rtsp_server_new();

    g_object_set(data->server, "service", SERVER_PORT, NULL);
    return data;
}

static CLIENT_DATA *
create_client_data(SERVER_DATA *server_data)
{
    CLIENT_DATA *client_data = ALLOC_NULL(CLIENT_DATA *, sizeof(CLIENT_DATA));
    client_data->server_data = server_data;

    return client_data;
}

static void
on_client_options_request(GstRTSPClient *client, GstRTSPContext *context,
    CLIENT_DATA *client_data)
{
    client_data->context = context;

    printf("[inf] client requesting %s\n", context->uri->abspath);
}

static void
on_client_connected(GstRTSPServer *server, GstRTSPClient *client,
    SERVER_DATA *server_data)
{
    printf("[inf] new client connected\n");

    /* create a structure that represents this client, this way
    we can keep references to important gstreamer structures */
    CLIENT_DATA *client_data = create_client_data(server_data);

    /* wait for the options request from the client, which is the
    first RTSP command that is send by the client */
    g_signal_connect(client, "options-request",
        G_CALLBACK(on_client_options_request), client_data);
}

int
main(int argc, char **argv)
{
    /* initialize gstreamer and set the directory that the graphs
    need to be written to */
    putenv("GST_DEBUG_DUMP_DOT_DIR=.");
    gst_init(&argc, &argv);

    /* initialze collection structure, will hold
    all data related to the server */
    SERVER_DATA *server_data = create_server_data();

    /* connect signals */
    g_signal_connect(server_data->server, "client-connected",
        G_CALLBACK(on_client_connected), server_data);

    /* start server */
    gst_rtsp_server_attach(server_data->server, NULL);

    /* run tha loop, we'll stop it when we run into trouble */
    loop = g_main_loop_new(NULL, FALSE);
    g_main_loop_run(loop);

    return 0;
}
