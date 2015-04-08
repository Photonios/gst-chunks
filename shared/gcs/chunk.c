#include <string.h>
#include <stdio.h>
#include <unistd.h>

#include <gcs/chunk.h>
#include <gcs/mem.h>

static void
update_full_path(GCS_CHUNK *chunk, int directory_len, int filename_len)
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
update_start_moment(GCS_CHUNK *chunk)
{
    struct tm time_info;

    sscanf(chunk->filename, "%d-%d-%d_%d;%d;%d",
        &time_info.tm_mday,
        &time_info.tm_mon,
        &time_info.tm_year,
        &time_info.tm_hour,
        &time_info.tm_min,
        &time_info.tm_sec);

    time_info.tm_year -= 1900;
    time_info.tm_mon -= 1;

    chunk->start_moment = mktime(&time_info);
}

GCS_CHUNK *
gcs_chunk_new(char *directory, int directory_len, char *filename,
    int filename_len)
{
    GCS_CHUNK *new_chunk = ALLOC_NULL(GCS_CHUNK *, sizeof(GCS_CHUNK));

    memcpy(new_chunk->directory, directory, directory_len);
    new_chunk->directory[directory_len] = '\0';

    memcpy(new_chunk->filename, filename, filename_len);
    new_chunk->filename[filename_len] = '\0';

    update_full_path(new_chunk, directory_len, filename_len);
    update_start_moment(new_chunk);

    return new_chunk;
}
