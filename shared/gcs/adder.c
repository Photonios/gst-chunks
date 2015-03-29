#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <gcs/adder.h>

void
on_pad_added(GstElement *demuxer, GstPad *pad, GstElement *queue)
{
    gst_element_link_many(demuxer, queue, NULL);
}

void
gcs_add_chunk_to_pipeline(GstElement *pipeline,
    GstElement *concat_elem, GCS_CHUNK *chunk)
{
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

    /* add all elements to the pipeline */
    gst_bin_add_many(GST_BIN(pipeline), source, demuxer, queue, parser,
        decoder, NULL);

    g_object_set(source, "location", chunk->full_path, NULL);

    /* we cannot link the demuxer to another element yet since the demuxer
    uses dynamic pads.. we'll do that later when the pads are created */
    g_signal_connect(demuxer, "pad-added", G_CALLBACK(on_pad_added), queue);

    /* we do have to link filesrc and matroskademux, otherwise no
    pads will be created */
    gst_element_link_many(source, demuxer, NULL);

    /* and we also link all other elements, except the demuxer, it is
    important we link at least one element to the concat element, because
    the order in which elements are linked to the concat element matters,
    if not added in the right other all chunks will be messed up */
    gst_element_link_many(queue, parser, decoder, concat_elem, NULL);
}
