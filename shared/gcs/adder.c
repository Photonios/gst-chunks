#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <gcs/adder.h>

char *
build_element_name(GCS_CHUNK *chunk, char *post_fix)
{
    char *name = malloc(100);
    snprintf(name, 100, "%d-%s", chunk->start_moment, post_fix);

    return name;
}

void
on_pad_added(GstElement *element, GstPad *pad, GstElement *concat_element)
{
    gst_element_link_many(element, concat_element);
}

void
gcs_add_chunk_to_pipeline(GstElement *pipeline,
    GstElement *concat_elem, GCS_CHUNK *chunk)
{
    char *source_name = build_element_name(chunk, "filesrc");
    char *demuxer_name = build_element_name(chunk, "matroskademux");

    GstElement *source = gst_element_factory_make("filesrc",
        NULL);

    GstElement *demuxer = gst_element_factory_make("matroskademux",
        NULL);

    GstElement *queue = gst_element_factory_make("queue",
        NULL);

    gst_bin_add_many(GST_BIN(pipeline), source, demuxer, queue,  NULL);
    gst_element_link_many(source, demuxer, queue, NULL);

    g_object_set(source, "location", chunk->full_path, NULL);
    g_signal_connect(demuxer, "pad-added", G_CALLBACK(on_pad_added),
        concat_elem);

    free(source_name);
    free(demuxer_name);
}
