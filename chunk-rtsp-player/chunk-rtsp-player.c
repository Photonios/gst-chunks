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
    GstElement *pipeline;
    GstElement *container;
    GstElement *source;
    GstElement *depay;
    GstElement *parser;
    GstElement *decoder;
    GstElement *sink;
} GcsRtspPlayer;

static GMainLoop *loop;

static void
on_sigint(int signo)
{
	/* will cause the main loop to stop and clean up, process will exit */
	if(loop != NULL)
		g_main_loop_quit(loop);
}

static int
gcs_rtsp_is_switch_message(GstRTSPMessage *message)
{
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

    if(method != GST_RTSP_OPTIONS) {
        return 0;
    }

    if(strcmp(uri, "switch") != 0) {
        return 0;
    }

    return 1;
}

static void
on_rtsp_message_received(GstRTSPSrc *src, GstRTSPMessage *msg, gpointer user_data)
{
    if(!gcs_rtsp_is_switch_message(msg)) {
        return;
    }
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

static void
on_rtspsrc_pad_added(GstElement *rtspsrc, GstPad *pad, gpointer user_data)
{
    GcsRtspPlayer *player = (GcsRtspPlayer *) user_data;

    gst_element_link_many(player->source, player->depay, player->container,
        player->sink, NULL);

    GSTREAMER_DUMP_GRAPH(player->pipeline, "mygraph");
}

GcsRtspPlayer *
gcs_rtsp_player_new(const char *rtsp_url)
{
    GcsRtspPlayer *player = ALLOC_NULL(GcsRtspPlayer *, sizeof(GcsRtspPlayer));

    /*
        rtspsrc ! rtph264depay ! bin(h254parse ! avdec_h264) ! xvimagesink
     */

    player->pipeline = gst_pipeline_new(NULL);
    player->container = gst_bin_new(NULL);

    player->source = gst_element_factory_make("rtspsrc", "source");
    player->depay = gst_element_factory_make("rtph264depay", "depay");
    player->parser = gst_element_factory_make("h264parse", "parser");
    player->decoder = gst_element_factory_make("avdec_h264", "decoder");
    player->sink = gst_element_factory_make("xvimagesink", "sink");

    /* build up container bin for the parser and decoder */

    gst_bin_add_many(GST_BIN(player->container), player->parser,
        player->decoder, NULL);

    gst_element_link(player->parser, player->decoder);

    GstPad *parser_sink_pad = gst_element_get_static_pad(player->parser,
        "sink");

    GstPad *decoder_src_pad = gst_element_get_static_pad(player->decoder,
        "src");

    gst_element_add_pad(player->container, gst_ghost_pad_new("sink",
        parser_sink_pad));

    gst_element_add_pad(player->container, gst_ghost_pad_new("src",
        decoder_src_pad));

    g_object_unref(parser_sink_pad);
    g_object_unref(decoder_src_pad);

    /* add all elements to the pipeline */

    gst_bin_add_many(GST_BIN(player->pipeline), player->source,
        player->depay, player->container, player->sink, NULL);

    /* rtspsrc has dynamic pads, wait for the pads to be created
    and then perform all the linking of pads */
    g_signal_connect(player->source, "pad-added",
        G_CALLBACK(on_rtspsrc_pad_added), player);

    /* set options and hook up signals */

    g_object_set(player->source, "location", rtsp_url, NULL);

    g_signal_connect(player->source, "message-received",
        G_CALLBACK(on_rtsp_message_received), player);

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
