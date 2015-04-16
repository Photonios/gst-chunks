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

typedef struct {
    GstElement *pipeline;
    GstElement *source;
    GstElement *depay;
    GstElement *parser;
    GstElement *decoder;
    GstElement *sink;
} GcsRtspPlayer;

void
on_rtsp_message(gpointer *rtspsrc, gpointer request, gpointer response, gpointer user_data)
{
    printf("[dbg] rtsp message received\n");
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

GcsRtspPlayer *
gcs_rtsp_player_new(const char *rtsp_url)
{
    GcsRtspPlayer *player = ALLOC_NULL(GcsRtspPlayer *, sizeof(GcsRtspPlayer));

    player->pipeline = gst_parse_launch(
        "rtspsrc name=source ! rtph264depay name=depay ! h264parse name=parser ! avdec_h264 name=decoder ! xvimagesink name=sink",
        NULL);

    if(!player->pipeline) {
        fprintf(stderr, "[err] could not parse pipeline description\n");
    }

    player->source = gst_bin_get_by_name(GST_BIN(player->pipeline), "source");
    player->depay = gst_bin_get_by_name(GST_BIN(player->pipeline), "depay");
    player->parser = gst_bin_get_by_name(GST_BIN(player->pipeline), "parser");
    player->decoder = gst_bin_get_by_name(GST_BIN(player->pipeline), "decoder");
    player->sink = gst_bin_get_by_name(GST_BIN(player->pipeline), "sink");

    g_object_set(player->source, "location", rtsp_url, NULL);

    g_signal_connect(player->source, "handle-request", G_CALLBACK(on_rtsp_message), player);

    return player;
}

static GMainLoop *loop;

static void
on_sigint(int signo)
{
	/* will cause the main loop to stop and clean up, process will exit */
	if(loop != NULL)
		g_main_loop_quit(loop);
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

    GSTREAMER_DUMP_GRAPH(player->pipeline, "ggg");

    /* run main loop so we don't exit until streaming stops */
    loop = g_main_loop_new(NULL, FALSE);
    g_main_loop_run(loop);

    return 0;
}
