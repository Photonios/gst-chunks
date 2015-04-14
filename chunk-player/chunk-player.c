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

static void
on_sigint(int signo)
{
	/* will cause the main loop to stop and clean up, process will exit */
	if(loop != NULL)
		g_main_loop_quit(loop);
}

int
main(int argc, char **argv)
{
    /* intercept SIGINT so we can can cleanly exit */
	signal(SIGINT, on_sigint);

    /* make sure we have enough arguments */
    if(argc < 2) {
		fprintf(stderr, "Usage: chunk-player [directory]\n");
		return 1;
	}

    /* initialize gstreamer */
    gst_init(&argc, &argv);

    /* start indexing, sorting etc of the chunks */
    GcsIndex *index = gcs_index_new();
    if(gcs_index_fill(index, argv[1]) <= 0) {
        fprintf(stderr, "[err] did not find any chunks\n");
        return 1;
    }

    /* create a new iterator for our chunk index */
    GcsIndexIterator *index_itr = gcs_index_iterator_new(index);

    /* create a new player with xvmagesink as the output, and start
    playing */
    GcsPlayer *player = gcs_player_new(index_itr, "xvimagesink", NULL, TRUE);
    gcs_player_play(player);

    /* run main loop so we don't exit until streaming stops */
    loop = g_main_loop_new(NULL, FALSE);
    g_main_loop_run(loop);

    /* if we get here, we're stopping */
    gcs_player_stop(player);
    gcs_player_free(player);
    gcs_index_iterator_free(index_itr);
    gcs_index_free(index);

    return 0;
}
