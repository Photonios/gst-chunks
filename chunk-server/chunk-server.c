#include <stdio.h>
#include <string.h>

#include <gst/gst.h>
#include <gst/rtsp-server/rtsp-server.h>

#include <gcs/mem.h>

typedef struct {
    GstRTSPServer *server;
} SERVER_DATA;

static GMainLoop *loop;

void
on_client_options_request(GstRTSPClient *client, GstRTSPContext *context,
    SERVER_DATA *data)
{
    int path_len = strlen(context->uri->abspath);
    if(path_len <= 0) {
        return;
    }

    printf("New client requesting %s\n", context->uri->abspath);
}

void
on_client_connected(GstRTSPServer *server, GstRTSPClient *client, SERVER_DATA *data)
{
    g_signal_connect(client, "options-request",
        G_CALLBACK(on_client_options_request), data);
}

int
main(int argc, char **argv)
{
    gst_init(&argc, &argv);

    loop = g_main_loop_new(NULL, FALSE);

    SERVER_DATA *data = ALLOC_NULL(SERVER_DATA *, sizeof(SERVER_DATA));

    data->server = gst_rtsp_server_new();

    g_object_set(data->server, "service", "8554", NULL);

    g_signal_connect(data->server, "client-connected",
        G_CALLBACK(on_client_connected), data);

    gst_rtsp_server_attach(data->server, NULL);
    g_main_loop_run(loop);

cleanup:
    g_object_unref(data->server);
    return 0;
}
