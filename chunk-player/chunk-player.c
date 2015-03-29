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

typedef struct {
    char *directory;
    GCS_INDEX *chunk_index;
    GCS_INDEX_ITERATOR *chunk_index_itr;

    GstElement *pipeline;
} PLAYER_DATA;

static char *
build_pipeline(GCS_CHUNK *first_chunk)
{
    char *pipeline_start = "concat name=c ! multiqueue ! h264parse ! avdec_h264 ! xvimagesink filesrc location=";
    char *pipeline_end = " ! matroskademux ! c.";

    int pipeline_start_len = strlen(pipeline_start);
    int pipeline_end_len = strlen(pipeline_end);
    int full_path_len = strlen(first_chunk->full_path);

    int total_len = pipeline_start_len + pipeline_end_len + full_path_len;

    char *pipeline = ALLOC_NULL(char *, total_len + 1); /* for null terminator */
    snprintf(pipeline, total_len + 1, "%s%s%s", pipeline_start, first_chunk->full_path, pipeline_end);

    pipeline[total_len] = '\0';
    return pipeline;
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
    gst_init(NULL, NULL);

    int result = 0;
    PLAYER_DATA *data = NULL;

    if(argc < 2) {
        fprintf(stderr, "Usage: chunk-player [directory]\n");
        result = 1;
        goto cleanup;
    }

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

    /* create chunk iterator that allows us to iterate over
    all indexed chunks in the right order */
    printf("Found %i chunks\n", gcs_index_count(data->chunk_index));
    data->chunk_index_itr = gcs_index_iterator_new(data->chunk_index);

    /* build basic pipeline */
    GError *error = NULL;
    data->pipeline = gst_parse_launch("concat name=concatter ! multiqueue ! xvimagesink", &error);
    GstElement *concatter = gst_bin_get_by_name(GST_BIN(data->pipeline), "concatter");

    /* add a filesrc and matroskademux element for each chunk to the pipeline */
    int counter = 0;
    GCS_CHUNK *chunk = gcs_index_iterator_next(data->chunk_index_itr);
    while((chunk = gcs_index_iterator_next(data->chunk_index_itr)) != NULL) {
        gcs_add_chunk_to_pipeline(data->pipeline, concatter, chunk);

        printf("Chunk loaded: %s\n", chunk->filename);

        counter++;
        if(counter == 10)
            break;
    }

    /* run that bitch */
    gst_element_set_state(data->pipeline, GST_STATE_PLAYING);

    /* run main loop */
    GMainLoop *loop = g_main_loop_new(NULL, FALSE);
    g_main_loop_run(loop);

cleanup:
    player_data_free(data);
    data = NULL;

    return result;
}
