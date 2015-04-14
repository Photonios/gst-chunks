#include <stdlib.h>
#include <string.h>
#include <dirent.h>
#include <stdio.h>
#include <inttypes.h>

#include <gcs/mem.h>
#include <gcs/index.h>
#include <gcs/chunk.h>
#include <gcs/time.h>

/* 1.1 seconds / 1100 milliseconds */
#define MAXIMUM_GAP_TIME 1100000000

static gint
compare_chunks_start_moment(gconstpointer a, gconstpointer b)
{
    const GcsChunk *chunk_a = (GcsChunk *) a;
    const GcsChunk *chunk_b = (GcsChunk *) b;

    if(chunk_a->start_moment < chunk_b->start_moment) {
        return -1;
    }

    if(chunk_a->start_moment == chunk_b->start_moment) {
        return 0;
    }

    return 1;
}

static void
detect_and_insert_gaps(GcsIndex *index)
{
    /* create a new array to store our newly build
    index in */
    GArray *new_index = g_array_new(0, 1, sizeof(GcsChunk));
    GcsIndexIterator *itr = gcs_index_iterator_new(index);

    /* get the start time of the index */
    uint64_t prev_chunk_stop_time = gcs_index_get_start_time(index);

    GcsChunk *chunk;
    while((chunk = gcs_index_iterator_next(itr)) != NULL) {
        /* calculate amount of nanoseconds between this chunk
        and the next one */
        uint64_t gap = chunk->start_moment - prev_chunk_stop_time;

        /* somehow, the compiler fucks when chunk->start_moment
        and prev_chunk_stop_time are equal (overflows the integer),
        little hack to prevent that */
        if(prev_chunk_stop_time >= chunk->start_moment) {
            gap = 0;
        }

        /* is it more than a second ? */
        if(gap > MAXIMUM_GAP_TIME) {
            /* insert a new gap into the index that is as long
            as the gap between the previous chunk and the next one */
            GcsChunk new_gap = gcs_chunk_new_gap(
                prev_chunk_stop_time, chunk->start_moment);

            g_array_append_val(new_index, new_gap);
        }

        /* insert the chunk into the new index */
        g_array_append_val(new_index, *chunk);
        prev_chunk_stop_time = chunk->stop_moment;
    }

    /* get the end time of this index (end of the day) */
    uint64_t next_day_time = gcs_index_get_end_time(index);

    /* determine whether we need to create a gap chunk between
    the last chunk and the end of the day */
    uint64_t gap = next_day_time - prev_chunk_stop_time;
    if(gap > MAXIMUM_GAP_TIME) {
        GcsChunk new_gap = gcs_chunk_new_gap(
            prev_chunk_stop_time, next_day_time);

        g_array_append_val(new_index, new_gap);
    }

    /* free old index and replace with new index, which also
    contains all the gaps */
    g_array_free(index->chunks, TRUE);
    index->chunks = new_index;
}

GcsIndex *
gcs_index_new()
{
    GcsIndex *index = ALLOC_NULL(GcsIndex *, sizeof(GcsIndex));
    index->chunks = g_array_new(FALSE, TRUE, sizeof(GcsChunk));

    return index;
}

int
gcs_index_fill(GcsIndex *index, char *directory)
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

        GcsChunk new_chunk = gcs_chunk_new(directory,
            directory_len, filename, filename_len);

        g_array_append_val(index->chunks, new_chunk);
    }

    closedir(d);

    /* sort chunks from older to newer */
    g_array_sort(index->chunks, compare_chunks_start_moment);

    /* detect and insert gaps to fill up missing chunks */
    detect_and_insert_gaps(index);

    int chunk_count = (int) index->chunks->len;
    return chunk_count;
}

int
gcs_index_count(GcsIndex *index)
{
    return index->chunks->len;
}

static struct tm
gcs_index_get_date(GcsIndex *index)
{
    /* grab the first chunk and its start moment, note that
    we have to convert it back to seconds (from nanoseconds) */
    GcsChunk *first_chunk = &g_array_index(index->chunks, GcsChunk, 0);
    time_t start_moment = (time_t) GCS_TIME_NANO_AS_SECONDS(first_chunk->start_moment);

    /* convert time_t to tm structure, UTC neutral */
    struct tm date_time = *localtime(&start_moment);

    /* zero out all the time related stuff, so we're left
    with the date */
    date_time.tm_sec = 0;
    date_time.tm_min = 0;
    date_time.tm_hour = 0;
    date_time.tm_wday = 0;
    date_time.tm_yday = 0;

    return date_time;
}

uint64_t
gcs_index_get_start_time(GcsIndex *index)
{
    if(!index || gcs_index_count(index) <= 0 ) {
        return 0;
    }

    /* get the date of the first structure, time will be
    zeroed out (00:00:00) */
    struct tm date_time = gcs_index_get_date(index);

    /* convert to time_t and then to nanoseconds */
    uint64_t date = (uint64_t) mktime(&date_time);
    date = GCS_TIME_SECONDS_AS_NANO(date);

    return date;
}

uint64_t
gcs_index_get_end_time(GcsIndex *index)
{
    if(!index || gcs_index_count(index) <= 0 ) {
        return 0;
    }

    /* get the date of the first structure, time will be
    zeroed out (00:00:00) */
    struct tm date_time = gcs_index_get_date(index);

    /* increase by one day (which gives us 00:00 at the next day)*/
    ++date_time.tm_mday;

    /* convert to time_t and then to nanoseconds */
    uint64_t date = (uint64_t) mktime(&date_time);
    date = GCS_TIME_SECONDS_AS_NANO(date);

    return date;
}

void
gcs_index_free(GcsIndex *index)
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

GcsIndexIterator *
gcs_index_iterator_new(GcsIndex *index)
{
    GcsIndexIterator *itr = ALLOC_NULL(GcsIndexIterator *,
        sizeof(GcsIndexIterator));

    itr->index = index;
    return itr;
}

GcsChunk *
gcs_index_iterator_next(GcsIndexIterator *itr)
{
    /* don't go out of bounds */
    if(itr->offset > (itr->index->chunks->len - 1)) {
        return NULL;
    }

    GcsChunk *next = &g_array_index(itr->index->chunks, GcsChunk,
        itr->offset);

    ++itr->offset;

    return next;
}

GcsChunk *
gcs_index_iterator_prev(GcsIndexIterator *itr)
{
    /* don't go out of bounds */
    if(itr->offset == 0 && itr->index->chunks->len > 0) {
        return NULL;
    }

    --itr->offset;

    GcsChunk *prev = &g_array_index(itr->index->chunks, GcsChunk,
        itr->offset);

    return prev;
}

GcsChunk *
gcs_index_iterator_peek(GcsIndexIterator *itr)
{
    GcsChunk *chunk = gcs_index_iterator_next(itr);
    if(chunk) {
        itr->offset--;
    }

    return chunk;
}

void
gcs_index_iterator_free(GcsIndexIterator *itr)
{
    if(!itr) {
        return;
    }

    free(itr);
    itr = NULL;
}
