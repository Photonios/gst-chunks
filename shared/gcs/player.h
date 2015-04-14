#ifndef __gst_chunks_shared_player_h
#define __gst_chunks_shared_player_h

#include <stdint.h>

#include <gst/gst.h>

#include <gcs/index.h>

#define GCS_PLAYER_DEFAULT_BIN_COUNT 2

typedef struct {
    GstElement *bin;
    GstElement *source;
    GstElement *demuxer;
    GstElement *queue;
    GstElement *parser;
    GstElement *decoder;

    int linked;
} GcsPlayerBin;

typedef struct {
    GcsIndexIterator *index_itr;

    GstElement *pipeline;
    GstElement *concat;
    GstElement *multiqueue;
    GstElement *sink;

    GPtrArray *bins;
    int next_bin_index;

} GcsPlayer;

#define GCS_PLAYER(x) ((GcsPlayer *)x);

GcsPlayer *     gcs_player_new(GcsIndexIterator *index_itr,
                    const char *sink_type, const char *sink_name, int enable_decoder);

void            gcs_player_prepare(GcsPlayer *player);
void            gcs_player_play(GcsPlayer *player);
void            gcs_player_stop(GcsPlayer *player);
void            gcs_player_free(GcsPlayer *player);
GcsPlayerBin *  gcs_player_bin_new();

#endif /* __gst_chunks_shared_player_h */
