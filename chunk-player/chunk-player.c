#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <dirent.h>

#include <gst/gst.h>

#include <gcs/dir.h>
#include <gcs/chunk.h>
#include <gcs/index.h>
#include <gcs/mem.h>

#define GENERATE_DEBUG_GRAPHS 0

#define GSTREAMER_FREE(ptr)  \
    if(ptr != NULL) {        \
        g_object_unref(ptr); \
        ptr = NULL;          \
    }

typedef struct {
    GstElement *bin;
    GstElement *source;
    GstElement *demuxer;
    GstElement *queue;
    GstElement *parser;
    GstElement *decoder;
} GcsPipelineDataBin;

typedef struct {
    char *directory;
    GcsIndex *chunk_index;
    GcsIndexIterator *chunk_index_itr;

    GstElement *pipeline;
    GstElement *concat;
    GstElement *multiqueue;
    GstElement *sink;

	GstClock *clock;

    GcsPipelineDataBin bin1;
    GcsPipelineDataBin bin2;

    int active_bin;
} GcsPipelineData;

static GMainLoop *loop;

static void
on_sigint(int signo)
{
    /* will cause the main loop to stop and clean up, process will exit */
    if(loop != NULL)
        g_main_loop_quit(loop);
}

static GcsPipelineData *
create_pipeline_data()
{
    GcsPipelineData *data = ALLOC_NULL(GcsPipelineData *, sizeof(GcsPipelineData *));
    return data;
}

static GstElement *
create_pipeline(GcsPipelineData *data)
{
    /* create the pipeline and the elements, don't give them
    names, gstreamer will assign unique names to them */
    data->pipeline = gst_pipeline_new(NULL);
    data->concat = gst_element_factory_make("concat", NULL);
    data->multiqueue = gst_element_factory_make("multiqueue", NULL);
    data->sink = gst_element_factory_make("xvimagesink", NULL);

    /* add all elements to the pipeline (which is a bin) */
    gst_bin_add_many(GST_BIN(data->pipeline), data->concat, data->multiqueue,
        data->sink, NULL);

    /* link them all together */
    gst_element_link_many(data->concat, data->multiqueue, data->sink, NULL);

	/* get the pipeline's clock */
	data->clock = gst_pipeline_get_clock(GST_PIPELINE(data->pipeline));

    printf("[inf] created pipeline\n");
    return data->pipeline;
}

static GcsPipelineDataBin *
create_pipeline_bin(GcsPipelineDataBin *data_bin, int index)
{
    /* use names we know so we can easily track elements in
    the pipeline */

    char source_name[9];
    char demuxer_name[10];
    char queue_name[8];
    char parser_name[9];
    char decoder_name[10];

    sprintf(source_name, "source_%i", index);
    sprintf(demuxer_name, "demuxer_%i", index);
    sprintf(queue_name, "queue_%i", index);
    sprintf(parser_name, "parser_%i", index);
    sprintf(decoder_name, "decoder_%i", index);

    /* create bin and elements for in the bin */
    data_bin->bin = gst_bin_new(NULL);
    data_bin->source = gst_element_factory_make("filesrc", source_name);
    data_bin->demuxer = gst_element_factory_make("matroskademux", demuxer_name);
    data_bin->queue = gst_element_factory_make("queue", queue_name);
    data_bin->parser = gst_element_factory_make("h264parse", parser_name);
    data_bin->decoder = gst_element_factory_make("avdec_h264", decoder_name);

    /* add all elements to the bin */
    gst_bin_add_many(GST_BIN(data_bin->bin), data_bin->source,
        data_bin->demuxer, data_bin->queue, data_bin->parser,
        data_bin->decoder, NULL);

    /* link them all together */
    gst_element_link_many(data_bin->source, data_bin->demuxer,
        data_bin->queue, data_bin->parser, data_bin->decoder, NULL);

    /* get the src pad of the decoder (last element in the bin), so
    we can create a ghost pad for it on the bin */
    GstPad *decoder_src_pad = gst_element_get_static_pad(data_bin->decoder,
        "src");

    if(!decoder_src_pad) {
        fprintf(stderr, "[err] unable to get `src` pad from decoder\n");
        return NULL;
    }

    /* add a ghost pad to the bin that is linked to the src pad
    of the decoder, this allows us to link the bin to other elements */
    gst_element_add_pad(data_bin->bin, gst_ghost_pad_new("src",
        decoder_src_pad));

    /* we don't need that references anymore */
    g_object_unref(decoder_src_pad);

    printf("[inf] created bin %i\n", index);
    return data_bin;
}

static void
link_bin(GcsPipelineData *data, GcsPipelineDataBin *data_bin)
{
    gst_bin_add(GST_BIN(data->pipeline), data_bin->bin);
    gst_element_link(data_bin->bin, data->concat);
}

static void
generate_debug_graphs(GcsPipelineData *data, const char *postfix)
{
#if GENERATE_DEBUG_GRAPHS
    char pipeline_graph_name[100];
    char bin1_graph_name[100];
    char bin2_graph_name[100];

    sprintf(pipeline_graph_name, "pipeline-%s", postfix);
    sprintf(bin1_graph_name, "bin1-%s", postfix);
    sprintf(bin2_graph_name, "bin2-%s", postfix);

    GST_DEBUG_BIN_TO_DOT_FILE(GST_BIN(data->pipeline),
        GST_DEBUG_GRAPH_SHOW_ALL, pipeline_graph_name);

    GST_DEBUG_BIN_TO_DOT_FILE(GST_BIN(data->bin1.bin),
        GST_DEBUG_GRAPH_SHOW_ALL, bin1_graph_name);

    GST_DEBUG_BIN_TO_DOT_FILE(GST_BIN(data->bin2.bin),
        GST_DEBUG_GRAPH_SHOW_ALL, bin2_graph_name);
#endif
}

static gboolean
on_switch_finish(gpointer user_data)
{
    GcsPipelineData *data = (GcsPipelineData *) user_data;

    /* determine currently active bin (the bin we're switching away from) */
    GcsPipelineDataBin *old_bin;
    if(!data->active_bin) {
        old_bin = &data->bin1;
        printf("[inf] switching from bin1 -> bin2\n");
    } else {
        old_bin = &data->bin2;
        printf("[inf] switching from bin2 -> bin1\n");
    }

    /* swap flag, we're switching aren't we ? */
    data->active_bin = !data->active_bin;

    /* our bins (branches) are linked to dynamic pads on the concat element,
    by grabbing the src pad of our bin, we can get the dynamic pad that was
    created on the concat element*/
    GstPad *bin_src_pad = gst_element_get_static_pad(old_bin->bin, "src");
    GstPad *concat_sink_pad = gst_pad_get_peer(bin_src_pad);

    /* unlink the bin from the concat element */
    gst_element_unlink(old_bin->bin, data->concat);

    /* request a release of the dynamic pad that we were linked with,
    this causes the pad to be removed so that when we link again,
    we get a new pad */
    gst_element_release_request_pad(data->concat, concat_sink_pad);

    /* we don't need the pads any more */
    GSTREAMER_FREE(bin_src_pad);
    GSTREAMER_FREE(concat_sink_pad);

    /* set the entire bin to NULL so we can perform
    operations on it */
    gst_element_set_state(old_bin->bin, GST_STATE_NULL);

    /* get the next chunk and set the location of the filesrc
    element to the location of the chunk */
    GcsChunk *chunk = gcs_index_iterator_next(data->chunk_index_itr);
    if(chunk == NULL) {
        printf("[inf] nearing end, not preparing next chunk\n");
        return;
    }

	if(gcs_chunk_is_gap(chunk)) {
		chunk = gcs_index_iterator_next(data->chunk_index_itr);
	}

    g_object_set(old_bin->source, "location", chunk->full_path, NULL);
    printf("[dbg] prepared chunk '%s'\n", chunk->full_path);

    /* link it back into the pipeline and then put it
    into the playing state again */
    gst_element_link(old_bin->bin, data->concat);
    gst_element_set_state(old_bin->bin, GST_STATE_PLAYING);

    /* if TRUE is returned, we'll execute again, don't want
    that to happen */
    return FALSE;
}

static void
on_switch(GstElement *element, GstPad *old_pad, GstPad *new_pad, gpointer user_data)
{
    /* perform actions on the application thread, and not on the
    streaming thread (which is where signals are emitted from */
    g_idle_add(on_switch_finish, user_data);
}

static void
send_gap_event(GcsPipelineData *data, GcsPipelineDataBin *data_bin, GcsChunk *gap_chunk)
{
	printf("[dbg] sending gap event\n");

	GstClockTime timestamp = gst_clock_get_time(data->clock);
	GstClockTime duration = (GstClockTime) gap_chunk->duration;

	GstPad *source_src_pad = gst_element_get_static_pad(data_bin->source, "src");

	GstEvent *event = gst_event_new_latency(duration);
	gst_pad_send_event(source_src_pad, event);

	g_object_unref(source_src_pad);
}

int
main(int argc, char **argv)
{
    /* intercept SIGINT so we can can cleanly exit */
    signal(SIGINT, on_sigint);

    if(argc < 2) {
        fprintf(stderr, "Usage: chunk-player [directory]\n");
        return 1;
    }

    /* initialize gstreamer, set destination directory for
    generated pipeline graphs before gstreamer initialization */
    putenv("GST_DEBUG_DUMP_DOT_DIR=.");
    gst_init(&argc, &argv);

    /* create structure and create basic pipeline */
    GcsPipelineData data;
    create_pipeline(&data);
    data.directory = argv[1];

    /* create bins that we'll switch between */
    create_pipeline_bin(&data.bin1, 1);
    create_pipeline_bin(&data.bin2, 2);

    /* add the bins to the pipeline and link them */
    link_bin(&data, &data.bin1);
    link_bin(&data, &data.bin2);

    /* create a new chunk index and fill it with chunks from the
    specified directory */
    data.chunk_index = gcs_index_new();
    if(gcs_index_fill(data.chunk_index, data.directory) <= 0) {
        fprintf(stderr, "[err] did not find any chunks\n");
        goto cleanup;
    }

    printf("[inf] loaded %i chunks\n", gcs_index_count(data.chunk_index));

    /* create a new chunk index iterator and get the first two
    chunks */
    data.chunk_index_itr = gcs_index_iterator_new(data.chunk_index);
    GcsChunk *chunk1 = gcs_index_iterator_next(data.chunk_index_itr);

	if(gcs_chunk_is_gap(chunk1)) {
		printf("[dbg] first chunk is a gap, sending gap\n");
		send_gap_event(&data, &data.bin1, chunk1);
		chunk1 = gcs_index_iterator_next(data.chunk_index_itr);
	}

	GcsChunk *chunk2 = gcs_index_iterator_next(data.chunk_index_itr);

	if(gcs_chunk_is_gap(chunk2)) {
		printf("[dbg] second chunk is a gap, sending gap\n");
		send_gap_event(&data, &data.bin2, chunk2);
		chunk2 = gcs_index_iterator_next(data.chunk_index_itr);
	}

    /* set the initial locations to the first two chunks */
    g_object_set(data.bin1.source, "location", chunk1->full_path, NULL);
    g_object_set(data.bin2.source, "location", chunk1->full_path, NULL);

    /* connect signals */
    g_signal_connect(data.concat, "pad-switch", G_CALLBACK(on_switch), &data);

    /* attempt to put the pipeline into the playing state, and wait
    for the state change to complete */
    gst_element_set_state(data.pipeline, GST_STATE_PLAYING);
    if(gst_element_get_state(data.pipeline, NULL, NULL, GST_CLOCK_TIME_NONE)
        == GST_STATE_CHANGE_FAILURE) {
        fprintf(stderr, "[err] failed to get the pipeline into the PLAYING state\n");
        goto cleanup;
    }

    /* generate some debug graphs so we can see how our
    pipeline is formed */
    generate_debug_graphs(&data, "init");

    /* run main loop and wait until streaming ended
    or we are stopped */
    loop = g_main_loop_new(NULL, FALSE);
    g_main_loop_run(loop);

cleanup:
    /* we have stopped the main loop, doesn't mean the pipeline
    stopped */
    gst_element_set_state(data.pipeline, GST_STATE_NULL);
    if(gst_element_get_state(data.pipeline, NULL, NULL, GST_CLOCK_TIME_NONE)
        == GST_STATE_CHANGE_FAILURE) {
        fprintf(stderr, "[err] failed to get the pipeline into the NULL state\n");
    }

    /* free index and index iterator */
    gcs_index_iterator_free(data.chunk_index_itr);
    gcs_index_free(data.chunk_index);

    /* free rest of the data structure */
    GSTREAMER_FREE(data.pipeline);
    return 0;
}
