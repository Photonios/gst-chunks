#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <gcs/adder.h>

typedef struct {
    GstElement *source;
    GstElement *demuxer;
    GstElement *queue;
    GstElement *parser;
    GstElement *decoder;
    GstElement *concatter;
} CHUNK_PIPELINE_BRANCH;

void
on_pad_added(GstElement *element, GstPad *pad, CHUNK_PIPELINE_BRANCH *branch)
{
    gst_element_link_many(
        branch->demuxer,
        branch->queue,
        branch->parser,
        branch->decoder,
        branch->concatter,
        NULL
    );
}

void
gcs_add_chunk_to_pipeline(GstElement *pipeline,
    GstElement *concat_elem, GCS_CHUNK *chunk)
{
    CHUNK_PIPELINE_BRANCH *branch = malloc(sizeof(CHUNK_PIPELINE_BRANCH));

    branch->source = gst_element_factory_make("filesrc",
        NULL);

    branch->demuxer = gst_element_factory_make("matroskademux",
        NULL);

    branch->queue = gst_element_factory_make("queue",
        NULL);

    branch->parser = gst_element_factory_make("h264parse",
        NULL);

    branch->decoder = gst_element_factory_make("avdec_h264",
        NULL);

    branch->concatter = concat_elem;

    gst_bin_add_many(GST_BIN(pipeline),
        branch->source,
        branch->demuxer,
        branch->queue,
        branch->parser,
        branch->decoder,
        NULL
    );

    g_object_set(branch->source, "location", chunk->full_path, NULL);

    /* we cannot link all elements yet since filesrc/matroskademux use
    dynamic pads, we'll do the linking when the pads have been created */
    g_signal_connect(branch->demuxer, "pad-added", G_CALLBACK(on_pad_added),
        branch);

    /* we do have to link filesrc and matroskademux, otherwise no
    pads will be created */
    gst_element_link_many(branch->source, branch->demuxer, NULL);
}
