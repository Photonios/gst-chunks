#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <dirent.h>

#include <gst/gst.h>

#include <gcs/dir.h>
#include <gcs/chunk.h>
#include <gcs/index.h>
#include <gcs/mem.h>
#include <gcs/player.h>
#include <gcs/gst.h>

#include </home/swen/Documents/gstreamer/gst-plugins-good/gst/rtsp/gstrtspsrc.h>

typedef struct {
    GstElement *bin;
    GstElement *parser;
    GstElement *decoder;
} GcsRtspPlayerBin;

typedef struct {
    GstElement *pipeline;
    GstElement *source;
    GstElement *depay;
    GcsRtspPlayerBin *bin;
    GstElement *sink;
} GcsRtspPlayer;

/* cast macro functions */
#define GCS_RTSP_PLAYER(x) (GcsRtspPlayer *) x

/* prototypes */
static int gcs_rtsp_is_switch_message(GstRTSPMessage *message);

/* globals */
static GMainLoop *loop;

static void
on_sigint(int signo)
{
	/* will cause the main loop to stop and clean up, process will exit */
	if(loop != NULL)
		g_main_loop_quit(loop);
}

static void
on_rtsp_message_received(GstRTSPSrc *src, GstRTSPMessage *msg, gpointer user_data)
{
    if(!gcs_rtsp_is_switch_message(msg)) {
        return;
    }

    printf("[inf] switching chunks\n");
}

static void
on_rtsp_src_pad_added(GstElement *element, GstPad *pad, gpointer user_data)
{
    GcsRtspPlayer *player = GCS_RTSP_PLAYER(user_data);

    gst_element_link_many(player->source, player->depay, player->bin->bin,
        player->sink, NULL);
}

void
gcs_rtsp_player_free(GcsRtspPlayer *player)
{
    if(!player) {
        return;
    }

    free(player);
}

void
gcs_rtsp_player_stop(GcsRtspPlayer *player)
{
    gst_element_set_state(player->pipeline, GST_STATE_NULL);
}

void
gcs_rtsp_player_play(GcsRtspPlayer *player)
{
    gst_element_set_state(player->pipeline, GST_STATE_PLAYING);
}

static int
gcs_rtsp_is_switch_message(GstRTSPMessage *message)
{
    /* as explained in chunk-server, we abuse the RTSP OPTIONS
    message to signal the client that there's a switch taking
    place between chunks */

    if(!message) {
        return 0;
    }

    if(gst_rtsp_message_get_type(message) != GST_RTSP_MESSAGE_REQUEST) {
        return 0;
    }

    GstRTSPMethod method;
    GstRTSPVersion version;
    const gchar *uri;

    if(gst_rtsp_message_parse_request(message, &method, &uri,
        &version) != GST_RTSP_OK) {
        return 0;
    }

    /* the way we identify our custom message is by setting
    the `uri` part of an RTSP OPTIONS message to 'switch' */

    if(method != GST_RTSP_OPTIONS || strcmp(uri, "switch") != 0) {
        return 0;
    }

    return 1;
}

GcsRtspPlayerBin *
gcs_rtsp_player_bin_new()
{
    /* we'll contain this in a separate structure so we can easily
    replace it in the mainn structure */
    GcsRtspPlayerBin *bin = ALLOC_NULL(GcsRtspPlayerBin *,
        sizeof(GcsRtspPlayerBin));

    /* create the new bin and the parser and decoder element */
    bin->bin = gst_bin_new(NULL);
    bin->parser = gst_element_factory_make("h264parse", "parser");
    bin->decoder = gst_element_factory_make("avdec_h264", "decoder");

    /* add the parser and decoder element to the bin,
    with and link them together */

    gst_bin_add_many(GST_BIN(bin->bin), bin->parser, bin->decoder,
        NULL);

    gst_element_link(bin->parser, bin->decoder);

    /* grab the sink and src pads of the parser and decoder
    (sink==left,src==right) so we can create ghost pads for them
    on the bin */

    GstPad *parser_sink_pad = gst_element_get_static_pad(bin->parser, "sink");
    GstPad *decoder_src_pad = gst_element_get_static_pad(bin->decoder, "src");

    /* create the bin's ghost pads, this allows the bin
    to be linked to other elements */

    gst_element_add_pad(bin->bin, gst_ghost_pad_new("sink",
        parser_sink_pad));

    gst_element_add_pad(bin->bin, gst_ghost_pad_new("src",
        decoder_src_pad));

    g_object_unref(parser_sink_pad);
    g_object_unref(decoder_src_pad);

    return bin;
}

GcsRtspPlayer *
gcs_rtsp_player_new(const char *rtsp_url)
{
    GcsRtspPlayer *player = ALLOC_NULL(GcsRtspPlayer *, sizeof(GcsRtspPlayer));

    /* create the pipeline and the elements to it */
    player->pipeline = gst_pipeline_new(NULL);
    player->source = gst_element_factory_make("rtspsrc", NULL);
    player->depay = gst_element_factory_make("rtph264depay", NULL);
    player->sink = gst_element_factory_make("xvimagesink", NULL);

    /* create the bin for the decoder and parser, this is in a bin
    so we can easily swap it in and out during playback */
    player->bin = gcs_rtsp_player_bin_new();

    /* add all the elements and the container bin to the
    pipeline */
    gst_bin_add_many(GST_BIN(player->pipeline), player->source, player->depay,
        player->bin->bin, player->sink, NULL);

    /* set up the properties */
    g_object_set(player->source, "location", rtsp_url, NULL);

    /* connect the `message-received` signal, so we can intercept
    each RTSP message we receive */
    g_signal_connect(player->source, "message-received",
        G_CALLBACK(on_rtsp_message_received), player);

    /* connect the `pad-added` signal to the rtspsrc element,
    this is because the rtspsrc element uses dynamic pads, so
    we have to wait for them to be created before we can link
    all elements together */
    g_signal_connect(player->source, "pad-added",
        G_CALLBACK(on_rtsp_src_pad_added), player);

    return player;
}

int
main(int argc, char **argv)
{
    /* intercept SIGINT so we can can cleanly exit */
	  signal(SIGINT, on_sigint);

    /* make sure we have enough arguments */
    if(argc < 2) {
    		fprintf(stderr, "Usage: chunk-rtsp-player [rtsp url]\n");
    		return 1;
	  }

    /* initialize gstreamer */
    putenv("GST_DEBUG_DUMP_DOT_DIR=.");
    gst_init(&argc, &argv);

    /* create the player and start playing */
    GcsRtspPlayer *player = gcs_rtsp_player_new(argv[1]);
    gcs_rtsp_player_play(player);

    /* run main loop so we don't exit until streaming stops */
    loop = g_main_loop_new(NULL, FALSE);
    g_main_loop_run(loop);

cleanup:
    gcs_rtsp_player_stop(player);
    gcs_rtsp_player_free(player);

    printf("[inf] stopped\n");
    return 0;
}
