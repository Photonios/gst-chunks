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
#include <gcs/player.h>
#include <gcs/gst.h>

#define SERVER_PORT "8554"

static GMainLoop *loop;

typedef struct {
    GstRTSPServer *server;
    GcsPlayer *player;
} SERVER_DATA;

typedef struct {
    SERVER_DATA *server_data;
    GstRTSPContext *context;
} CLIENT_DATA;

static void
free_server_data(SERVER_DATA *data)
{
    if(!data) {
        return;
    }

    GSTREAMER_FREE(data->server);
    free(data);
}

static void
free_client_data(CLIENT_DATA *data)
{
    if(!data) {
        return;
    }

    GSTREAMER_FREE(data->context);
    free(data);
}

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
on_client_media_configure(GstRTSPMediaFactory *factory, GstRTSPMedia *media,
    CLIENT_DATA *client_data)
{
    printf("[inf] configuring media for client\n");

    GstElement *ppp = client_data->server_data->player->pipeline;
    GstPipeline *pipeline = GST_PIPELINE(ppp);

    gcs_player_prepare(client_data->server_data->player);
    //gst_rtsp_media_take_pipeline(media, pipeline);

    GST_DEBUG_BIN_TO_DOT_FILE(GST_BIN(client_data->server_data->player->pipeline),
        GST_DEBUG_GRAPH_SHOW_ALL, "hallo");
}

CLIENT_DATA *client_ding;

GstElement *
on_ding(GstRTSPMediaFactory *factory, const GstRTSPUrl *url)
{
    printf("ON DING\n");
    return client_ding->server_data->player->pipeline;
}

static void
on_client_options_request(GstRTSPClient *client, GstRTSPContext *context,
    CLIENT_DATA *client_data)
{
    client_data->context = context;
    client_ding = client_data;

    /* ignore */
    int path_len = strlen(context->uri->abspath);
    if(path_len <= 0) {
        return;
    }

    /* get the mount points collection of this client and create
    a new media factory */
    GstRTSPMountPoints *mount_points = gst_rtsp_client_get_mount_points(client);
    GstRTSPMediaFactory *media_factory = gst_rtsp_media_factory_new();

    GstRTSPMediaFactoryClass *media_factory_class =
        GST_RTSP_MEDIA_FACTORY_GET_CLASS(media_factory);

    media_factory_class->create_element = on_ding;

    /* hook up the media-configure signal so we can inject our own
    pipeline */
    g_signal_connect(media_factory, "media-configure",
        G_CALLBACK(on_client_media_configure), client_data);

    /* set temporary launch line, until we can replace the media */
    /*gst_rtsp_media_factory_set_launch(media_factory, "videotestsrc ! x264enc ! \
        rtph264pay pt=96 name=pay0"); */

    /* link the path that the client is requesting to the media factory
    we just created, this will cause the the media-configure signal
    to be invoked, and gives us the possibility to configure the pipeline */
    gst_rtsp_mount_points_add_factory(mount_points,
        context->uri->abspath, media_factory);

    printf("[inf] client requesting %s\n", context->uri->abspath);
    g_object_unref(mount_points);
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

    /* creating a new index and fill it with chunks from
    a directory */
    GcsIndex *index = gcs_index_new();
    if(gcs_index_fill(index, "../recordings-3") <= 0) {
        fprintf(stderr, "[err] did not find any chunks\n");
        return 1;
    }

    /* create new iterator for the index */
    GcsIndexIterator *index_itr = gcs_index_iterator_new(index);

    /* create new chunk player with no decoder */
    server_data->player = gcs_player_new(index_itr, "rtph264pay", "pay0", FALSE);
    g_object_set(server_data->player->sink, "pt", 96, NULL);

    /* start server */
    gst_rtsp_server_attach(server_data->server, NULL);

    /* run tha loop, we'll stop it when we run into trouble */
    loop = g_main_loop_new(NULL, FALSE);
    g_main_loop_run(loop);

cleanup:
    free_server_data(server_data);
    return 0;
}
