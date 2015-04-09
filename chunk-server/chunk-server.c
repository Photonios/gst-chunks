#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <dirent.h>

#include <gst/gst.h>

#include <gcs/dir.h>
#include <gcs/chunk.h>
#include <gcs/index.h>
#include <gcs/mem.h>

int
main(int argc, char **argv)
{
    /* initialize gstreamer and set the directory that the graphs
    need to be written to */
    putenv("GST_DEBUG_DUMP_DOT_DIR=.");
    gst_init(&argc, &argv);

    

    return 0;
}
