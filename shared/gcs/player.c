#include <stdio.h>
#include <stdint.h>

#include <gst/gst.h>

#include <gcs/mem.h>
#include <gcs/index.h>
#include <gcs/player.h>
#include <gcs/gst.h>

/* prototype declarations */
static int gcs_player_prepare_next_bin(GcsPlayer *player, int play);
static int gcs_player_get_next_bin_index(GcsPlayer *player);

static gboolean
on_switch_finish(gpointer user_data)
{
    GcsPlayer *player = GCS_PLAYER(user_data);
    gcs_player_prepare_next_bin(player, TRUE);

    return FALSE;
}

static void
on_switch(GstElement *element, GstPad *old_pad, GstPad *new_pad,
    gpointer user_data)
{
    /* perform switching of bins on the application thread
    and not on the streaming thread, which is where signals
    are emitted on. g_idle_add will execute the specified function
    from the main thread */

    g_idle_add(on_switch_finish, user_data);
}

static void
gcs_player_bin_change_elements(GcsPlayerBin *player_bin, const char *source_name,
    const char *demuxer_name)
{
    /* create the new elements that we're going to inject
    into the pipeline */
    GstElement *new_source = gst_element_factory_make(source_name, NULL);
    GstElement *new_demuxer = gst_element_factory_make(demuxer_name, NULL);

    /* add the new elements to the bin */
    gst_bin_add(GST_BIN(player_bin->bin), new_source);
    gst_bin_add(GST_BIN(player_bin->bin), new_demuxer);

    /* unlink the demuxer from the rest of the pipeline, and
    unlink the source from the demuxer */
    gst_element_unlink(player_bin->demuxer, player_bin->queue);
    gst_element_unlink(player_bin->source, player_bin->demuxer);

    /* remove the source and demuxer from the bin */
    gst_bin_remove(GST_BIN(player_bin->bin), player_bin->source);
    gst_bin_remove(GST_BIN(player_bin->bin), player_bin->demuxer);

    /* update references in our structure */
    player_bin->source = new_source;
    player_bin->demuxer = new_demuxer;

    /* link the new elements together */
    gst_element_link_many(player_bin->source, player_bin->demuxer,
        player_bin->queue, NULL);
}

static void
gcs_player_bin_make_chunk_bin(GcsPlayerBin *player_bin)
{
    gcs_player_bin_change_elements(player_bin, "filesrc",
        "matroskademux");

    player_bin->type = GCS_PLAYER_BIN_TYPE_CHUNK;
}

static void
gcs_player_bin_make_gap_bin(GcsPlayerBin *player_bin)
{
    gcs_player_bin_change_elements(player_bin, "videotestsrc",
        "x264enc");

    player_bin->type = GCS_PLAYER_BIN_TYPE_GAP;
}

static void
gcs_player_bin_stop(GcsPlayer *player, GcsPlayerBin *player_bin)
{
    /* our bins (branches) are linked to dynamic pads on the concat element,
    by grabbing the src pad of our bin, we can get the dynamic pad that was
    created on the concat element*/
    GstPad *bin_src_pad = gst_element_get_static_pad(player_bin->bin, "src");
    GstPad *concat_sink_pad = gst_pad_get_peer(bin_src_pad);

    /* unlink the bin from the concat element */
    if(player_bin->linked) {
        gst_element_unlink(player_bin->bin, player->concat);

        /* request a release of the dynamic pad that we were linked with,
        this causes the pad to be removed so that when we link again,
        we get a new pad */
        gst_element_release_request_pad(player->concat, concat_sink_pad);
    }

    /* we don't need the pads any more */
    GSTREAMER_FREE(bin_src_pad);
    GSTREAMER_FREE(concat_sink_pad);

    /* set the entire bin to NULL so we can perform
    operations on it */
    gst_element_set_state(player_bin->bin, GST_STATE_NULL);
}

static void
gcs_player_bin_start(GcsPlayer *player, GcsPlayerBin *player_bin, int play)
{
    /* link it back into the pipeline and then put it
    into the playing state again */
    gst_element_link(player_bin->bin, player->concat);

    /* when the pipeline is initializing, we don't want the bin
    to be moving to the PLAYING state on it's own, this flag can
    be used to control this behaviour */
    if(play) {
        gst_element_set_state(player_bin->bin, GST_STATE_PLAYING);
    }

    /* make sure it's unlinked if it stops */
    player_bin->linked = TRUE;
}

static void
gcs_player_bin_set_filename(GcsPlayerBin *player_bin, const char *filename)
{
    g_object_set(player_bin->source, "location", filename, NULL);
}

static int
gcs_player_get_next_bin_index(GcsPlayer *player)
{
    int next_index = 0;
    int last_index = player->bins->len - 1;

    /* if we're at the end, move the the first one again,
    otherwise, just take the next one in the list */
    if(player->next_bin_index == last_index) {
        next_index = 0;
    } else {
        next_index = player->next_bin_index + 1;
    }

    return next_index;
}

static GcsChunk *
gcs_player_get_next_chunk(GcsPlayer *player)
{
    GcsChunk *chunk = gcs_index_iterator_next(player->index_itr);
    return chunk;
}

static int
gcs_player_prepare_next_bin(GcsPlayer *player, int play)
{
    /* get a reference to the bin we're switching to, but
    don't actually make the switch yet */
    GcsPlayerBin *player_bin = g_ptr_array_index(player->bins,
        player->next_bin_index);

    /* get the next chunk to switch to */
    GcsChunk *chunk = gcs_player_get_next_chunk(player);

    /* make sure the bin is stopped before we're making any
    changes to it */
    gcs_player_bin_stop(player, player_bin);

    /* set the type of the bin (depending on the type of chunk) */
    if(gcs_chunk_is_gap(chunk)) {
        gcs_player_bin_make_gap_bin(player_bin);

        /* temp hack to avoid waiting */
        if(chunk->duration > 12000000000) {
            chunk->duration = 12000000000;
            printf("[wrn] applying temp hack, reducing gap to 12 seconds\n");
        }

        /* videotestsrc will EOS when the max-duration was reached,
        pattern == GST_VIDEO_TEST_SRC_BLACK */
        g_object_set(player_bin->source, "max-duration", chunk->duration, NULL);
        g_object_set(player_bin->source, "pattern", 2, NULL);

    } else {
        gcs_player_bin_make_chunk_bin(player_bin);
        gcs_player_bin_set_filename(player_bin, chunk->full_path);
    }

    /* when the pipeline is initializing, we don't want the
    bins to go into the play state right away */
    gcs_player_bin_start(player, player_bin, play);

    if(!gcs_chunk_is_gap(chunk)) {
        printf("[inf] prepared chunk '%s'\n", chunk->filename);
    } else {
        printf("[inf] prepared gap\n");
    }

    /* update with the new bin index */
    player->next_bin_index = gcs_player_get_next_bin_index(player);
    return 1;
}

static int
gcs_player_create_pipeline(GcsPlayer *player, const char *sink_type,
    const char *sink_name, int enable_decoder)
{
    player->pipeline = gst_pipeline_new(NULL);
    player->concat = gst_element_factory_make("concat", NULL);
    player->multiqueue = gst_element_factory_make("multiqueue", NULL);

    gst_bin_add_many(GST_BIN(player->pipeline), player->concat,
        player->multiqueue, NULL);

    gst_element_link_many(player->concat, player->multiqueue, NULL);

    /* a sink is optional, for for example, RTSP servers */
    if(sink_type) {
        player->sink = gst_element_factory_make(sink_type, sink_name);

        if(!player->sink) {
            fprintf(stderr, "[err] could not create sink element of type `%s`",
                sink_type);
            return FALSE;
        }

        gst_bin_add(GST_BIN(player->pipeline), player->sink);
        gst_element_link(player->multiqueue, player->sink);
    }

    /* add bins for context switching */
    int i;
    for(i = 0; i < GCS_PLAYER_DEFAULT_BIN_COUNT; ++i) {
        GcsPlayerBin *new_bin = gcs_player_bin_new(enable_decoder);

        /* add to the pipeline bin, but don't link them yet */
        gst_bin_add(GST_BIN(player->pipeline), new_bin->bin);
        g_ptr_array_add(player->bins, new_bin);
    }

    /* hook up signals */
    g_signal_connect(player->concat, "pad-switch", G_CALLBACK(on_switch),
        player);

    return TRUE;
}

GcsPlayer *
gcs_player_new(GcsIndexIterator *index_itr, const char *sink_type,
    const char *sink_name,
    int enable_decoder)
{
    GcsPlayer *player = ALLOC_NULL(GcsPlayer *, sizeof(GcsPlayer));
    player->index_itr = index_itr;
    player->bins = g_ptr_array_new();

    gcs_player_create_pipeline(player, sink_type, sink_name, enable_decoder);

    return player;
}

void
gcs_player_prepare(GcsPlayer *player)
{
    /* initialize the first two bins with the first
    two chunks */
    gcs_player_prepare_next_bin(player, FALSE);
    gcs_player_prepare_next_bin(player, FALSE);

    /* little hack to make everything works */
    player->next_bin_index = 0;
}

void
gcs_player_play(GcsPlayer *player)
{
    gcs_player_prepare(player);
    gst_element_set_state(player->pipeline, GST_STATE_PLAYING);
}

void
gcs_player_stop(GcsPlayer *player)
{
    gst_element_set_state(player->pipeline, GST_STATE_NULL);
}

void
gcs_player_free(GcsPlayer *player)
{
    if(!player) {
        return;
    }

    if(player->bins) {
        /* free entire array, and the elements */
        g_ptr_array_free(player->bins, TRUE);
    }

    free(player);
}

GcsPlayerBin *
gcs_player_bin_new(int enable_decoder)
{
    GcsPlayerBin *player_bin = ALLOC_NULL(GcsPlayerBin *, sizeof(GcsPlayerBin));

    player_bin->bin = gst_bin_new(NULL);
    player_bin->source = gst_element_factory_make("filesrc", NULL);
    player_bin->demuxer = gst_element_factory_make("matroskademux", NULL);
    player_bin->queue = gst_element_factory_make("queue", NULL);
    player_bin->parser = gst_element_factory_make("h264parse", NULL);
    player_bin->capsfilter = gst_element_factory_make("capsfilter", NULL);

    /* it could be that one does not want the video to be decoded,
    in that case we simply turn it into a queue element */
    if(enable_decoder) {
        player_bin->decoder = gst_element_factory_make("avdec_h264", NULL);
    } else {
        player_bin->decoder = gst_element_factory_make("queue", NULL);
    }

    gst_bin_add_many(GST_BIN(player_bin->bin), player_bin->source,
        player_bin->demuxer, player_bin->queue, player_bin->parser,
        player_bin->capsfilter, player_bin->decoder, NULL);

    gst_element_link_many(player_bin->source, player_bin->demuxer,
        player_bin->queue, player_bin->parser, player_bin->capsfilter,
        player_bin->decoder, NULL);

    /* get the src pad of the decoder (last element in the bin), so
    we can create a ghost pad for it on the bin */
    GstPad *decoder_src_pad = gst_element_get_static_pad(player_bin->decoder,
        "src");

    /* add a ghost pad to the bin that is linked to the src pad
    of the decoder, this allows us to link the bin to other elements */
    gst_element_add_pad(player_bin->bin, gst_ghost_pad_new("src",
        decoder_src_pad));

    g_object_unref(decoder_src_pad);
    return player_bin;
}
