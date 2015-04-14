#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <dirent.h>

#include <gst/gst.h>

#include <gcs/dir.h>
#include <gcs/chunk.h>
#include <gcs/index.h>
#include <gcs/mem.h>
#include <gcs/player.h>

static GMainLoop *loop;

int
main(int argc, char **argv)
{
    gst_init(&argc, &argv);

    GcsIndex *index = gcs_index_new();
    if(gcs_index_fill(index, "../recordings-3") <= 0) {
        fprintf(stderr, "[err] did not find any chunks\n");
        return 1;
    }

    GcsIndexIterator *index_itr = gcs_index_iterator_new(index);
    GcsPlayer *player = gcs_player_new(index_itr, "xvimagesink", NULL, TRUE);

    gcs_player_play(player);

    loop = g_main_loop_new(NULL, FALSE);
    g_main_loop_run(loop);
    return 0;
}
