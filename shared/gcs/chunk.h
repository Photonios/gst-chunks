#ifndef __gst_chunks_shared_chunk_h
#define __gst_chunks_shared_chunk_h

#include <dirent.h>
#include <time.h>

/* explictly made all strings array so the whole
structure can be freed easily */
typedef struct {
    char filename[PATH_MAX];
    char directory[PATH_MAX];
    char full_path[PATH_MAX];
    time_t start_moment;
} GCS_CHUNK;

GCS_CHUNK gcs_chunk_new(char *directory, int directory_len, char *filename,
    int filename_len);

#endif /* __gst_chunks_shared_chunk_h */
