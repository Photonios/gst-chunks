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

typedef struct {
    GstRTSPServer *server;
    GPtrArray *clients;
    GcsIndex *index;
    GcsIndexIterator *index_itr;
} GcsChunkServer;

typedef struct {
    GcsChunkServer *server;
    GstRTSPContext *context;
    GcsPlayer *player;
} GcsChunkServerClient;

#define GCS_CHUNK_SERVER_CLIENT(x) (GcsChunkServerClient *) x

static GMainLoop *loop;

static void gcs_chunk_server_client_send_switch_message(
    GcsChunkServerClient *client);

static void
on_sigint(int signo)
{
    /* will cause the main loop to stop and clean up, process will exit */
    if(loop != NULL)
        g_main_loop_quit(loop);
}

static void
on_switch(GstElement *elem, GstPad *old_pad, GstPad *new_pad, gpointer user_data)
{
    /* we're switching chunks, signal the client of this event
    by sending a custom RTSP message */

    GcsChunkServerClient *client = GCS_CHUNK_SERVER_CLIENT(user_data);
    gcs_chunk_server_client_send_switch_message(client);
}

static void
gcs_chunk_server_client_send_switch_message(GcsChunkServerClient *client)
{
    /* little hack here, we abuse the RTSP OPTIONS message (see
    RFC-2326 section 10.1) to signal the client that we've switch from
    one chunk to the next one. this allows the client to re-initialize
    the decoder in case the video format is changing between chunks.

    we chose the OPTIONS message because it's most harmless, all the
    client will do is reply with the available options. the hack here
    is that we set the `uri` property of the request to 'switch', our
    custom client will check the `uri` property of each incoming message. */

    GstRTSPMessage *message;
    gst_rtsp_message_new_request(&message, GST_RTSP_OPTIONS, "switch");

    gst_rtsp_client_send_message(client->context->client,
        client->context->session, message);

    gst_rtsp_message_free(message);
}

static void
gcs_chunk_server_free(GcsChunkServer *server)
{
    if(!server) {
        return;
    }

    if(server->index_itr) {
        gcs_index_iterator_free(server->index_itr);
    }

    if(server->index) {
        gcs_index_free(server->index);
    }

    GSTREAMER_FREE(server->server);
    free(server);
}

static void
gcs_chunk_server_client_free(GcsChunkServerClient *client)
{
    if(!client) {
        return;
    }

    GSTREAMER_FREE(client->context);
    free(client);
}

static GcsChunkServer *
gcs_chunk_server_new(const char *server_port)
{
    GcsChunkServer *server = ALLOC_NULL(GcsChunkServer *, sizeof(GcsChunkServer));
    server->server = gst_rtsp_server_new();
    server->clients = g_ptr_array_new();

    g_object_set(server->server, "service", server_port, NULL);
    return server;
}

static GcsChunkServerClient *
gcs_chunk_server_new_client(GcsChunkServer *server)
{
    /* allocate new client and have the client keep a
    reference to the server ... fuck we're creating a circular
    dependency here.... well, screw that for now */

    GcsChunkServerClient *client = ALLOC_NULL(
        GcsChunkServerClient *, sizeof(GcsChunkServerClient));

    client->server = server;

    /* make sure that the client has a reference to the client */
    g_ptr_array_add(server->clients, server);
    return client;
}

static void
gcs_chunk_server_client_init_player(GcsChunkServerClient *client)
{
    /* create the new player, we use the `rtph264pay` element as the
    sink element.. this will put the h264 data into a rtp packet..
    it must be named `pay0` so the gst-rtsp-server will link with
    that element */
    client->player = gcs_player_new(client->server->index_itr, "rtph264pay",
        "pay0", FALSE);

    /* pt == payload type, which is 96.. which is the first payload type
    that is dynamic.. meaning that any kind of data will work...
    http://www.iana.org/assignments/rtp-parameters/rtp-parameters.xhtml */
    g_object_set(client->player->sink, "pt", 96, NULL);

    /* hook up a signal so we get notified when switching happens
    between chunks */
    gcs_player_connect_signal(client->player, G_CALLBACK(on_switch), client);
}

static void
on_client_media_configure(GstRTSPMediaFactory *factory, GstRTSPMedia *media,
    GcsChunkServerClient *client)
{
    /* prepare the player, which means that pipeline is created etc */
    gcs_player_prepare(client->player);
    printf("[inf] configuring media for client\n");
}

GcsChunkServerClient *client_ding;

GstElement *
on_client_create_pipeline(GstRTSPMediaFactory *factory, const GstRTSPUrl *url)
{
    return client_ding->player->pipeline;
}

static void
on_client_options_request(GstRTSPClient *rtsp_client, GstRTSPContext *context,
    GcsChunkServerClient *client)
{
    client->context = context;
    client_ding = client;

    /* ignore */
    int path_len = strlen(context->uri->abspath);
    if(path_len <= 0) {
        return;
    }

    /* create new chunk player with no decoder */
    gcs_chunk_server_client_init_player(client);

    /* get the mount points collection of this client and create
    a new media factory */
    GstRTSPMountPoints *mount_points = gst_rtsp_client_get_mount_points(rtsp_client);
    GstRTSPMediaFactory *media_factory = gst_rtsp_media_factory_new();

    /* override the media fatory's create_element method so we
    control how the pipeline is created */

    GstRTSPMediaFactoryClass *media_factory_class =
        GST_RTSP_MEDIA_FACTORY_GET_CLASS(media_factory);

    media_factory_class->create_element = on_client_create_pipeline;

    /* hook up the media-configure signal so we can inject our own
    pipeline */
    g_signal_connect(media_factory, "media-configure",
        G_CALLBACK(on_client_media_configure), client);

    /* link the path that the client is requesting to the media factory
    we just created, this will cause the the media-configure signal
    to be invoked, and gives us the possibility to configure the pipeline */
    gst_rtsp_mount_points_add_factory(mount_points,
        context->uri->abspath, media_factory);

    printf("[inf] client requesting %s\n", context->uri->abspath);
    g_object_unref(mount_points);
}

static void
on_client_connected(GstRTSPServer *rtsp_server, GstRTSPClient *rtsp_client,
    GcsChunkServer *server)
{
    printf("[inf] new client connected\n");

    /* create a structure that represents this client, this way
    we can keep references to important gstreamer structures */
    GcsChunkServerClient *client = gcs_chunk_server_new_client(
        server);

    /* wait for the options request from the client, which is the
    first RTSP command that is send by the client */
    g_signal_connect(rtsp_client, "options-request",
        G_CALLBACK(on_client_options_request), client);
}

int
main(int argc, char **argv)
{
    /* intercept SIGINT so we can can cleanly exit */
    signal(SIGINT, on_sigint);

    /* make sure we have enough arguments */
    if(argc < 2) {
        fprintf(stderr, "Usage: chunk-server [directory]\n");
        return 1;
    }

    /* initialize gstreamer and set the directory that the graphs
    need to be written to */
    putenv("GST_DEBUG_DUMP_DOT_DIR=.");
    gst_init(&argc, &argv);

    /* initialze collection structure, will hold
    all data related to the server */
    GcsChunkServer *server = gcs_chunk_server_new(SERVER_PORT);

    /* connect signals */
    g_signal_connect(server->server, "client-connected",
        G_CALLBACK(on_client_connected), server);

    /* start indexing, sorting etc of the chunks */
    printf("[inf] indexing chunks in %s\n", argv[1]);
    server->index = gcs_index_new();
    if(gcs_index_fill(server->index, argv[1]) <= 0) {
        fprintf(stderr, "[err] did not find any chunks\n");
        return 1;
    }
    printf("[inf] indexed %i chunks\n", gcs_index_count(server->index));

    /* create new iterator for the index */
    server->index_itr = gcs_index_iterator_new(server->index);

    /* start server */
    gst_rtsp_server_attach(server->server, NULL);

    /* run tha loop, we'll stop it when we run into trouble */
    loop = g_main_loop_new(NULL, FALSE);
    g_main_loop_run(loop);

cleanup:
    gcs_chunk_server_free(server);
    printf("[inf] stopped\n");
    return 0;
}
