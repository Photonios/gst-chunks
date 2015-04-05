#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <gcs/adder.h>

void
gcs_add_chunk_to_pipeline(GstElement *pipeline,
    GstElement *concat_elem, GCS_CHUNK *chunk)
{
    GstElement *bin = gst_bin_new(NULL);

    GstElement *source = gst_element_factory_make("filesrc",
        NULL);

    GstElement *demuxer = gst_element_factory_make("matroskademux",
        NULL);

    GstElement *queue = gst_element_factory_make("queue",
        NULL);

    GstElement *parser = gst_element_factory_make("h264parse",
        NULL);

    GstElement *decoder = gst_element_factory_make("avdec_h264",
        NULL);

    /* add all elements to the bin */
    gst_bin_add_many(GST_BIN(bin), source, demuxer, queue, parser,
        decoder, NULL);

    g_object_set(source, "location", chunk->full_path, NULL);

    /* add our bin to the pipeline */
    gst_bin_add_many(GST_BIN(pipeline), bin, NULL);

    /* link all elements together, and in the end to the concat element */
    gst_element_link_many(source, demuxer, queue, parser, decoder,
        concat_elem, NULL);
}
