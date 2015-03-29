#include <stdlib.h>
#include <string.h>
#include <dirent.h>

#include <gcs/mem.h>
#include <gcs/index.h>
#include <gcs/chunk.h>

static gint
compare_chunks_start_moment(gconstpointer a, gconstpointer b)
{
    const GCS_CHUNK *chunk_a = (GCS_CHUNK *) a;
    const GCS_CHUNK *chunk_b = (GCS_CHUNK *) b;

    if(chunk_a->start_moment < chunk_b->start_moment) {
        return -1;
    }

    if(chunk_a->start_moment == chunk_b->start_moment) {
        return 0;
    }

    return 1;
}

GCS_INDEX *
gcs_index_new()
{
    GCS_INDEX *index = ALLOC_NULL(GCS_INDEX *, sizeof(GCS_INDEX));
    index->chunks = g_array_new(0, 1, sizeof(GCS_CHUNK));

    return index;
}

int
gcs_index_fill(GCS_INDEX *index, char *directory)
{
    int directory_len = strlen(directory);

    DIR *d = opendir(directory);
    if(!d) {
        return -1;
    }

    struct dirent *dir = NULL;
    while((dir = readdir(d)) != NULL) {
        /* skip non-files */
        if(dir->d_type != DT_REG) {
            continue;
        }

        char *filename = &dir->d_name[0];
        int filename_len = strlen(filename);

        GCS_CHUNK new_chunk = gcs_chunk_new(directory,
            directory_len, filename, filename_len);

        g_array_append_val(index->chunks, new_chunk);
    }

    closedir(d);

    /* sort chunks from older to newer */
    g_array_sort(index->chunks, compare_chunks_start_moment);

    int chunk_count = (int) index->chunks->len;
    return chunk_count;
}

int
gcs_index_count(GCS_INDEX *index)
{
    return index->chunks->len;
}

void
gcs_index_free(GCS_INDEX *index)
{
    /* fail silently if NULL is passed */
    if(!index) {
        return;
    }

    if(index->chunks) {
        /* last parameter indicates freeing of elements as well */
        g_array_free(index->chunks, 1);
    }

    index = NULL;
}

GCS_INDEX_ITERATOR *
gcs_index_iterator_new(GCS_INDEX *index)
{
    GCS_INDEX_ITERATOR *itr = ALLOC_NULL(GCS_INDEX_ITERATOR *,
        sizeof(GCS_INDEX_ITERATOR));

    itr->index = index;
    return itr;
}

GCS_CHUNK *
gcs_index_iterator_next(GCS_INDEX_ITERATOR *itr)
{
    /* don't go out of bounds */
    if(itr->offset > (itr->index->chunks->len - 1)) {
        return NULL;
    }

    GCS_CHUNK *next = &g_array_index(itr->index->chunks, GCS_CHUNK,
        itr->offset);

    ++itr->offset;

    return next;
}

void
gcs_index_iterator_free(GCS_INDEX_ITERATOR *itr)
{
    if(!itr) {
        return;
    }

    free(itr);
    itr = NULL;
}
