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

#define MAX_CHUNKS 100
#define BASIC_PIPELINE_DESCRIPTION "concat name=concatter ! multiqueue ! xvimagesink"

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

static GstBusSyncReply
on_pipeline_bus_message(GstBus *bus, GstMessage *message, gpointer data)
{
    GstMessageType message_type = GST_MESSAGE_TYPE(message);
    switch(message_type) {
        case GST_MESSAGE_EOS: {
            /* will cause the main loop to stop and clean up, process will exit */
            if(loop) {
                g_main_loop_quit(loop);
            }
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
