#ifndef __gst_chunks_shared_adder_h
#define __gst_chunks_shared_adder_h

#include <gcs/chunk.h>
#include <gcs/index.h>

void gcs_add_chunk_to_pipeline(GstElement *pipeline,
    GstElement *concat_elem, GCS_CHUNK *chunk);

#endif /* __gst_chunks_shared_adder_h */
