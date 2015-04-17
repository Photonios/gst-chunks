#include <string.h>
#include <stdio.h>
#include <unistd.h>

#include <gcs/chunk.h>
#include <gcs/meta.h>
#include <gcs/mem.h>
#include <gcs/time.h>

static void
update_full_path(GcsChunk *chunk, int directory_len, int filename_len)
{
    /* todo: add proper path joining, if the directory ends with a /
    or the filename starts with a / you'll have // */

    char *cwd = getcwd (NULL, 0);

    char temp[PATH_MAX];
    snprintf(temp, PATH_MAX, "%s/%s/%s",
        cwd, chunk->directory, chunk->filename);

    /* resolve symlinks and ./ and ../ */
    realpath(temp, chunk->full_path);
}

static void
update_start_moment(GcsChunk *chunk)
{
    /* when I started developing this, I used a different
    filename format, detect that and fall back to the old one */
    char *format = "%d-%d-%d_%d-%d-%d";
    if(strstr(chunk->filename, ";") != NULL) {
        printf("[wrn] falling back to old filename format\n");
        format = "%d-%d-%d_%d;%d;%d";
    }

    struct tm time_info;

    sscanf(chunk->filename, format,
        &time_info.tm_mday,
        &time_info.tm_mon,
        &time_info.tm_year,
        &time_info.tm_hour,
        &time_info.tm_min,
        &time_info.tm_sec);

    /* year is since 1900 (2015 == 1015) and month
    is zero-based */
    time_info.tm_year -= 1900;
    time_info.tm_mon -= 1;

    /* convert seconds to nano seconds */
    chunk->start_moment = (uint64_t) mktime(&time_info);
    chunk->start_moment = GCS_TIME_SECONDS_AS_NANO(chunk->start_moment);
}

static void
update_stop_moment(GcsChunk *chunk)
{
    chunk->duration = gcs_meta_get_mkv_duration(chunk->full_path);
    chunk->stop_moment = chunk->start_moment + chunk->duration;
}

GcsChunk
gcs_chunk_new(char *directory, int directory_len, char *filename,
    int filename_len)
{
    GcsChunk new_chunk;

    memcpy(new_chunk.directory, directory, directory_len);
    new_chunk.directory[directory_len] = '\0';

    memcpy(new_chunk.filename, filename, filename_len);
    new_chunk.filename[filename_len] = '\0';

    update_full_path(&new_chunk, directory_len, filename_len);
    update_start_moment(&new_chunk);
    update_stop_moment(&new_chunk);

    return new_chunk;
}

GcsChunk
gcs_chunk_new_gap(uint64_t start, uint64_t stop)
{
    GcsChunk new_chunk;

    new_chunk.start_moment = start;
    new_chunk.stop_moment = stop;
    new_chunk.duration = (stop - start);

    /* although we allocate using calloc, just be sure
    this is recognized as a gap */
    new_chunk.filename[0] = 0;

    return new_chunk;
}

int
gcs_chunk_is_gap(GcsChunk *chunk)
{
    if(!chunk) {
        return 0;
    }

    int is_gap = (chunk->filename[0] == 0);
    return is_gap;
}

void
gcs_chunk_print(GcsChunk *chunk)
{
    if(!chunk) {
        return;
    }

    /* convert back to seconds */
    time_t start = (time_t) GCS_TIME_NANO_AS_SECONDS(chunk->start_moment);
    time_t stop = (time_t) GCS_TIME_NANO_AS_SECONDS(chunk->stop_moment);
    time_t duration = (time_t) GCS_TIME_NANO_AS_SECONDS(chunk->duration);

    /* convert start and stop time into tm structures */
    struct tm start_info = *localtime(&start);
    struct tm stop_info = *localtime(&stop);

    printf("%s - %i:%i:%i - %i:%i:%i\n", chunk->filename, start_info.tm_hour,
        start_info.tm_min, start_info.tm_sec, stop_info.tm_hour,
        stop_info.tm_min, stop_info.tm_sec);
}
