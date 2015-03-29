#ifndef __gst_chunks_shared_index_h
#define __gst_chunks_shared_index_h

#include <gst/gst.h>

#include <gcs/chunk.h>

/* explictly made a struct instead of typedef so
new members can easily be added */
typedef struct {
    GArray *chunks;
} GCS_INDEX;

typedef struct {
    GCS_INDEX *index;
    int offset;
} GCS_INDEX_ITERATOR;

GCS_INDEX * gcs_index_new();
int         gcs_index_fill(GCS_INDEX *index, char *directory);
void        gcs_index_free(GCS_INDEX *index);

GCS_INDEX_ITERATOR * gcs_index_iterator_new(GCS_INDEX *index);
GCS_CHUNK *          gcs_index_iterator_next(GCS_INDEX_ITERATOR *itr);

#endif /* __gst_chunks_shared_index_h */
