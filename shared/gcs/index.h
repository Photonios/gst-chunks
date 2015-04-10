#ifndef __gst_chunks_shared_index_h
#define __gst_chunks_shared_index_h

#include <gst/gst.h>

#include <gcs/chunk.h>

/* explictly made a struct instead of typedef so
new members can easily be added */
typedef struct {
    GArray *chunks;
} GcsIndex;

typedef struct {
    GcsIndex *index;
    int offset;
} GcsIndexIterator;

GcsIndex *      gcs_index_new();
int             gcs_index_fill(GcsIndex *index, char *directory);
int             gcs_index_count(GcsIndex *index);
uint64_t        gcs_index_get_start_time(GcsIndex *index);
uint64_t        gcs_index_get_end_time(GcsIndex *index);
void            gcs_index_free(GcsIndex *index);

GcsIndexIterator * gcs_index_iterator_new(GcsIndex *index);
GcsChunk *         gcs_index_iterator_next(GcsIndexIterator *itr);
GcsChunk *         gcs_index_iterator_prev(GcsIndexIterator *itr);
GcsChunk *         gcs_index_iterator_peek(GcsIndexIterator *itr);
void               gcs_index_iterator_free(GcsIndexIterator *itr);

#endif /* __gst_chunks_shared_index_h */
