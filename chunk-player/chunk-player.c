#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <dirent.h>

#include <gst/gst.h>

#include <gcs/dir.h>
#include <gcs/chunk.h>
#include <gcs/index.h>
#include <gcs/mem.h>
#include <gcs/adder.h>

#define MAX_CHUNKS 2
#define BASIC_PIPELINE_DESCRIPTION "concat name=concatter ! multiqueue ! xvimagesink"

//#define SHOW_STATE_CHANGES

typedef struct {
    char *directory;
    GCS_INDEX *chunk_index;
    GCS_INDEX_ITERATOR *chunk_index_itr;

    GstElement *pipeline;
} PLAYER_DATA;

static GMainLoop *loop;

static void
on_sigint(int signo)
{
    /* will cause the main loop to stop and clean up, process will exit */
	if(loop) {
		g_main_loop_quit(loop);
    }
}

static char *
gst_state_name(GstState state)
{
    switch(state) {
        case GST_STATE_VOID_PENDING:
            return "GST_STATE_VOID_PENDING";
        case GST_STATE_NULL:
            return "GST_STATE_NULL";
        case GST_STATE_READY:
            return "GST_STATE_READY";
        case GST_STATE_PAUSED:
            return "GST_STATE_PAUSED";
        case GST_STATE_PLAYING:
            return "GST_STATE_PLAYING";
        default:
            return "GST_STATE_UNKNOWN";
    }
}

static GstBusSyncReply
on_pipeline_bus_message(GstBus *bus, GstMessage *message, gpointer data)
{
    GstMessageType message_type = GST_MESSAGE_TYPE(message);
    const char *message_source_name = GST_MESSAGE_SRC_NAME(message);

    switch(message_type) {
        case GST_MESSAGE_ERROR:
        case GST_MESSAGE_EOS: {
            /* will cause the main loop to stop and clean up, process will exit */
            if(loop) {
                g_main_loop_quit(loop);
            }
        } break;

        case GST_MESSAGE_STATE_CHANGED: {
            GstState old_state, new_state, pending_state;
            gst_message_parse_state_changed(message, &old_state, &new_state, &pending_state);

#if defined(SHOW_STATE_CHANGES)
            printf("State changing on '%s' from %s to %s\n",
                message_source_name, gst_state_name(old_state), gst_state_name(new_state));
#endif
        } break;
    }
    return GST_BUS_PASS;
}

static void
player_data_free(PLAYER_DATA *data)
{
    if(!data) {
        return;
    }

    if(data->chunk_index_itr) {
        gcs_index_iterator_free(data->chunk_index_itr);
        data->chunk_index_itr = NULL;
    }

    if(data->chunk_index) {
        gcs_index_free(data->chunk_index);
        data->chunk_index = NULL;
    }

    free(data);
    data = NULL;
}

gboolean
cleanup_chunk_pipeline_bin(gpointer data)
{
    if(!data) {
        return;
    }

    GstElement *bin = GST_ELEMENT(data);
    gst_element_set_state(bin, GST_STATE_NULL);

    printf("NULLLLLLL\n");

    return FALSE;
}

void
on_pad_switch(GstElement *element, GstPad *old_pad, GstPad *new_pad)
{
    GstElement *old_bin = gst_pad_get_parent_element(old_pad);
    const char *old_bin_name = GST_ELEMENT_NAME(old_bin);

    if(new_pad != NULL) {
        GstElement *new_bin = gst_pad_get_parent_element(new_pad);
        const char *new_bin_name = GST_ELEMENT_NAME(new_bin);
        printf("Switching (%s - %s)\n", old_bin_name, new_bin_name);
    } else {
        printf("Switching (%s)\n", old_bin_name);
    }

    g_idle_add(cleanup_chunk_pipeline_bin, old_bin);
}

int
main(int argc, char **argv)
{
    /* intercept SIGINT so we can can cleanly exit */
	signal(SIGINT, on_sigint);

    /* initialize gstreamer, causes all plugins to be loaded */
    gst_init(&argc, &argv);

    int result = 0;
    PLAYER_DATA *data;
    GstBus *bus;

    if(argc < 2) {
        fprintf(stderr, "Usage: chunk-player [directory]\n");
        result = 1;
        goto cleanup;
    }

    /* initialize data structure that is used as a container
    for all relevant data */
    data = ALLOC_NULL(PLAYER_DATA *, sizeof(PLAYER_DATA));
    data->directory = argv[1];

    /* do not continue without a valid directory */
    if(!gcs_dir_exists(data->directory)) {
        fprintf(stderr, "%s is not a valid directory\n", data->directory);
        result = 1;
        goto cleanup;
    }

    /* create a new chunk index and fill it with chunks from the
    specified directory */
    data->chunk_index = gcs_index_new();
    if(gcs_index_fill(data->chunk_index, data->directory) <= 0) {
        fprintf(stderr, "Did not found any chunks\n");
        result = 1;
        goto cleanup;
    }

    /* build basic pipeline that we will add branches to */
    GError *error = NULL;
    data->pipeline = gst_parse_launch(BASIC_PIPELINE_DESCRIPTION, &error);
    GstElement *concatter = gst_bin_get_by_name(GST_BIN(data->pipeline), "concatter");

    g_signal_connect(concatter, "pad-switch", G_CALLBACK(on_pad_switch), data);

    /* create a new branch in the pipeline for each chunk, that will link
    to the concat element */

    int chunk_count = 0;
    GCS_CHUNK *chunk;

    data->chunk_index_itr = gcs_index_iterator_new(data->chunk_index);
    while((chunk = gcs_index_iterator_next(data->chunk_index_itr)) != NULL) {
        gcs_add_chunk_to_pipeline(data->pipeline, concatter, chunk);

        ++chunk_count;
        if(chunk_count == MAX_CHUNKS) {
            break;
        }
    }

    printf("Loaded %i/%i chunks\n", chunk_count,
        gcs_index_count(data->chunk_index));

    /* before we start, set up a handler on the pipeline's bus
    so we can detect that we're done or ran into an error */
    bus = gst_element_get_bus(data->pipeline);
    gst_bus_set_sync_handler(bus, on_pipeline_bus_message, data, NULL);

    /* bring the pipeline into the playing state, thus starting playback */
    gst_element_set_state(data->pipeline, GST_STATE_PLAYING);

    /* using get_state we wait for the state change to complete */
	if(gst_element_get_state(data->pipeline, NULL, NULL, GST_CLOCK_TIME_NONE)
		== GST_STATE_CHANGE_FAILURE) {
		fprintf(stderr, "Failed to get the pipeline into the PLAYING state\n");
		goto cleanup;
	}

    printf("Pipeline is playing now\n");

    /* run the main loop, we will quit the main loop when EOS happens
    on the pipeline or when SIGINT is received */
    loop = g_main_loop_new(NULL, FALSE);
    g_main_loop_run(loop);

    /* if we got here, the main loop has stopped */
    printf("Closing files and cleaning up\n");

    /* we do get_state here to wait for the state change to complete */
cleanup:
    gst_element_set_state(data->pipeline, GST_STATE_NULL);
    if(gst_element_get_state(data->pipeline, NULL, NULL, GST_CLOCK_TIME_NONE)
        == GST_STATE_CHANGE_FAILURE) {
        fprintf(stderr, "Failed to get the pipline into the NULL state\n");
    }

    if(data) {
        player_data_free(data);
        data = NULL;
    }

    printf("Exiting with exit code %i\n", result);
    return result;
}
