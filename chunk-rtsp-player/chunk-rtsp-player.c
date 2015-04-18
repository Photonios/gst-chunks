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
    GstElement *source;
    GstElement *depay;
    GstElement *queue;
    GstElement *parser;
    GstElement *decoder;
    GstElement *sink;

    GstPadProbeInfo *probe;

    GstPad *eos_pad;
    GstPadProbeInfo *eos_probe;

    int resetting;
    int eos_received;
} GcsRtspPlayer;

#define GCS_RTSP_PLAYER(x) (GcsRtspPlayer *) x

static GMainLoop *loop;

static int gcs_rtsp_is_switch_message(GstRTSPMessage *message);

static void
on_sigint(int signo)
{
    /* will cause the main loop to stop and clean up, process will exit */
    if(loop != NULL)
    	 g_main_loop_quit(loop);
}

static gboolean
on_switch(gpointer user_data)
{
    printf("[dbg] resetting parser/demuxer 1\n");

    GcsRtspPlayer *player = GCS_RTSP_PLAYER(user_data);

    GSTREAMER_DUMP_GRAPH(player->pipeline, "before-unlink");

    /* unlink the parser and decoder from their container
    and from each other */
    gst_element_unlink(player->queue, player->parser);
    gst_element_unlink(player->parser, player->decoder);
    gst_element_unlink(player->decoder, player->sink);

    printf("[dbg] resetting parser/demuxer 2\n");

    GSTREAMER_DUMP_GRAPH(player->pipeline, "after-unlink");

    /* restore the the parser and decoder to their initial
    states */
    gst_element_set_state(player->parser, GST_STATE_NULL);
    gst_element_set_state(player->decoder, GST_STATE_NULL);

    printf("[dbg] resetting parser/demuxer 3\n");

    /* link them back again */
    gst_element_link(player->queue, player->parser);
    gst_element_link(player->parser, player->decoder);
    gst_element_link(player->decoder, player->sink);

    printf("[dbg] resetting parser/demuxer 4\n");

    GSTREAMER_DUMP_GRAPH(player->pipeline, "after-link");

    /* put them back into playing again */
    gst_element_set_state(player->parser, GST_STATE_PLAYING);
    gst_element_set_state(player->decoder, GST_STATE_PLAYING);

    printf("[dbg] resetting parser/demuxer 5\n");

    /* release the first probe, which is currently
    blocking the flow to the parser and decoder */
    GstPad *queue_sink_pad = gst_element_get_static_pad(player->queue,
        "sink");

    /*gst_pad_remove_probe(queue_sink_pad,
        GST_PAD_PROBE_INFO_ID(player->probe)); */

    printf("[dbg] removing eos pad probe\n");
    GstPad *sink_sink_pad = gst_element_get_static_pad(player->sink,
        "sink");

    gst_pad_remove_probe(player->eos_pad,
        GST_PAD_PROBE_INFO_ID(player->eos_probe));

    printf("[dbg] removing eos pad probe --done\n");

    g_object_unref(queue_sink_pad);

    return FALSE;
}

static GstPadProbeReturn
on_sink_pad_probe(GstPad *pad, GstPadProbeInfo *info, gpointer user_data)
{
    GcsRtspPlayer *player = GCS_RTSP_PLAYER(user_data);
    if(player->eos_received) {
        printf("[dbg] dropping shit in eos pad probe\n");
        return GST_PAD_PROBE_DROP;
    }

    /* wait until we actually receive EOS, everything
    else can safely be passed down stream */
    if(!GST_IS_EVENT(info->data)) {
        return GST_PAD_PROBE_PASS;
    }

    GstEvent *event = GST_EVENT(info->data);
    if(GST_EVENT_TYPE(event) != GST_EVENT_EOS) {
        return GST_PAD_PROBE_PASS;
    }

    printf("[dbg] parser/demuxer are now eos\n");

    /* yeey, we have EOS, remove this probe */
    //gst_pad_remove_probe(pad, GST_PAD_PROBE_INFO_ID(info));
    player->eos_probe = info;
    player->eos_pad = pad;

    /* perform the rest on the application thread, and not
    on the streaming thread (which is what we're on now), other
    wise the pipeline will dead lock */
    g_idle_add(on_switch, user_data);

    /* very important that we drop now, otherwise the image sink
    will handle EOS and our pipeline will come to a halt */
    player->eos_received = TRUE;
    return GST_PAD_PROBE_DROP;
}

static GstPadProbeReturn
on_data_flow_blocked(GstPad *pad, GstPadProbeInfo *info, gpointer user_data)
{
    GcsRtspPlayer *player = GCS_RTSP_PLAYER(user_data);

    if(player->resetting) {
        return GST_PAD_PROBE_OK;
    }

    printf("[dbg] data flow to parser/decoder blocked\n");

    /* store a reference to this probe so we can remove it
    when we're done */
    player->probe = info;

    /* get the sink pad of the parser, so we can send EOS on it */
    GstPad *parser_sink_pad = gst_element_get_static_pad(player->parser,
        "sink");

    /* set up a pad probe so we can catch EOS at the end of
    the container bin (after the decoder) */
    GstPad *sink_sink_pad = gst_element_get_static_pad(player->sink,
        "sink");

    gst_pad_add_probe(sink_sink_pad, GST_PAD_PROBE_TYPE_ALL_BOTH,
        on_sink_pad_probe, player, NULL);

    /* send EOS onto the parser's sink pad, we'll get it coming
    out of the decoder, that way we can be sure both the parser
    and the decoder processed EOS and flushed all of their data */
    gst_pad_send_event(parser_sink_pad, gst_event_new_eos());

    /* continue blocking, we've stored a refernce to the
    pad probe info, we'll remove the probe later when we're done */
    g_object_unref(parser_sink_pad);
    g_object_unref(sink_sink_pad);
    player->resetting = TRUE;
    return GST_PAD_PROBE_OK;
}

static void
on_rtsp_message_received(GstRTSPSrc *src, GstRTSPMessage *msg, gpointer user_data)
{
    GcsRtspPlayer *player = GCS_RTSP_PLAYER(user_data);

    /* wait for the 'switch' message, which is our custom message
    from the server that there's a switch between chunks */
    if(!gcs_rtsp_is_switch_message(msg)) {
        return;
    }

    printf("[inf] received 'switch' message\n");

    /* start by putting up a probe in front of the parser
    and decoder, so no data is passed to them */
    GstPad *queue_sink_pad = gst_element_get_static_pad(player->queue,
        "sink");

    gst_pad_add_probe(queue_sink_pad,
        GST_PAD_PROBE_TYPE_ALL_BOTH, on_data_flow_blocked, player, NULL);

    g_object_unref(queue_sink_pad);
}

static void
on_rtspsrc_pad_added(GstElement *rtspsrc, GstPad *pad, gpointer user_data)
{
    GcsRtspPlayer *player = GCS_RTSP_PLAYER(user_data);

    gst_element_link_many(player->source, player->depay, player->queue,
        player->parser, player->decoder, player->sink, NULL);
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

    if(method != GST_RTSP_OPTIONS || strcmp(uri, "switch") != 0) {
        return 0;
    }

    return 1;
}

GcsRtspPlayer *
gcs_rtsp_player_new(const char *rtsp_url)
{
    GcsRtspPlayer *player = ALLOC_NULL(GcsRtspPlayer *, sizeof(GcsRtspPlayer));

    /*
        rtspsrc ! rtph264depay ! bin(h254parse ! avdec_h264) ! xvimagesink
     */

    player->pipeline = gst_pipeline_new(NULL);
    player->source = gst_element_factory_make("rtspsrc", "source");
    player->depay = gst_element_factory_make("rtph264depay", "depay");
    player->queue = gst_element_factory_make("queue", "queue");
    player->parser = gst_element_factory_make("h264parse", "parser");
    player->decoder = gst_element_factory_make("avdec_h264", "decoder");
    player->sink = gst_element_factory_make("xvimagesink", "sink");

    /* add all elements to the pipeline */

    gst_bin_add_many(GST_BIN(player->pipeline), player->source,
        player->depay, player->queue, player->parser, player->decoder,
        player->sink, NULL);

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
