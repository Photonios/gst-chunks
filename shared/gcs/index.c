#include <stdlib.h>
#include <string.h>
#include <dirent.h>
#include <stdio.h>
#include <inttypes.h>

#include <gcs/mem.h>
#include <gcs/index.h>
#include <gcs/chunk.h>
#include <gcs/time.h>

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

static void
detect_and_insert_gaps(GCS_INDEX *index)
{
    /* create a new array to store our newly build
    index in */
    GArray *new_index = g_array_new(0, 1, sizeof(GCS_CHUNK));
    GCS_INDEX_ITERATOR *itr = gcs_index_iterator_new(index);

    /* grab the date of the first chunk, giving us
    a timestamp that is at 00:00:00 */
    uint64_t last_chunk_time = gcs_index_get_date(index);

    GCS_CHUNK *chunk;
    while((chunk = gcs_index_iterator_next(itr)) != NULL) {
        uint64_t gap = chunk->start_moment - last_chunk_time;
        if(last_chunk_time >= chunk->start_moment) {
            gap = 0;
        }

        /* is it more than a second ? */
        if(gap > 1100000000) {
            /* insert a new gap into the index that is as long
            as the gap between the previous chunk and the next one */
            GCS_CHUNK new_gap = gcs_chunk_new_gap(
                last_chunk_time, chunk->start_moment);

            g_array_append_val(new_index, new_gap);
        }

        /* insert the chunk into the new index */
        g_array_append_val(new_index, *chunk);

        /* update last chunk time with this chunk's stop
        time */
        last_chunk_time = chunk->stop_moment;
    }

    /* create timestamp that is on the next day 00:00 */
    time_t chunk_date = (time_t) GCS_TIME_NANO_AS_SECONDS(gcs_index_get_date(index));
    struct tm next_day = *localtime(&chunk_date);
    next_day.tm_mday += 1;
    uint64_t next_day_time = GCS_TIME_SECONDS_AS_NANO((uint64_t)mktime(&next_day));

    /* determine whether we need to create a gap chunk between
    the last chunk and the end of the day */
    uint64_t gap = next_day_time - last_chunk_time;
    if(gap > 1100000000) {
        GCS_CHUNK new_gap = gcs_chunk_new_gap(
            last_chunk_time, next_day_time);

        g_array_append_val(new_index, new_gap);
    }

    /* free old index and replace with new index, which also
    contains all the gaps */
    g_array_free(index->chunks, 1);
    index->chunks = new_index;
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

    /* detect and insert gaps to fill up missing chunks */
    detect_and_insert_gaps(index);

    int chunk_count = (int) index->chunks->len;
    return chunk_count;
}

int
gcs_index_count(GCS_INDEX *index)
{
    return index->chunks->len;
}

uint64_t
gcs_index_get_date(GCS_INDEX *index)
{
    if(!index) {
        return 0;
    }

    if(gcs_index_count(index) <= 0) {
        return 0;
    }

    /* grab the first chunk and its start moment, note that
    we have to convert it back to seconds (from nanoseconds) */
    GCS_CHUNK *first_chunk = &g_array_index(index->chunks, GCS_CHUNK, 0);
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

    /* convert to time_t and then to nanoseconds */
    uint64_t date = (uint64_t) mktime(&date_time);
    date = GCS_TIME_SECONDS_AS_NANO(date);

    return date;
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

GCS_CHUNK *
gcs_index_iterator_peek(GCS_INDEX_ITERATOR *itr)
{
    GCS_CHUNK *chunk = gcs_index_iterator_next(itr);
    if(chunk) {
        itr->offset--;
    }

    return chunk;
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
