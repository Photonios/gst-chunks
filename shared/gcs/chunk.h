#ifndef __gst_chunks_shared_chunk_h
#define __gst_chunks_shared_chunk_h

#include <stdint.h>
#include <dirent.h>
#include <time.h>

/* explictly made all strings array so the whole
structure can be freed easily */
typedef struct {
    char filename[PATH_MAX];
    char directory[PATH_MAX];
    char full_path[PATH_MAX];

    /* start and stop moments are UNIX EPOCH timestamps
    in nanoseconds, so the amount of nanoseconds from
    1st of January 1970 */
    uint64_t start_moment;
    uint64_t stop_moment;

    /* nano seconds (stop_moment = start_moment + duration) */
    uint64_t duration;
} GCS_CHUNK;

GCS_CHUNK  gcs_chunk_new(char *directory, int directory_len, char *filename,
    int filename_len);

GCS_CHUNK gcs_chunk_new_gap(uint64_t start, uint64_t stop);

int gcs_chunk_is_gap(GCS_CHUNK *chunk);

void gcs_chunk_print(GCS_CHUNK *chunk);

#endif /* __gst_chunks_shared_chunk_h */
