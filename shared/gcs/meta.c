#include <string.h>
#include <stdio.h>

#include <gst/gst.h>
#include <gst/pbutils/pbutils.h>

#include <gcs/meta.h>
#include <gcs/mem.h>

uint64_t
gcs_meta_get_mkv_duration(const char *filename)
{
    uint64_t duration = 0;
    GstDiscoverer *magic;
    GstDiscovererInfo *info;

    if(!filename) {
        goto cleanup;
    }

    char uri[PATH_MAX];
    sprintf(uri, "file://%s", filename);

    magic = gst_discoverer_new(GST_SECOND, NULL);

    info = gst_discoverer_discover_uri(magic, uri,
        NULL);

    if(!info) {
        goto cleanup;
    }

    duration = (uint64_t) gst_discoverer_info_get_duration(info);

cleanup:
    if(magic) {
        g_object_unref(magic);
    }

    if(info) {
        g_object_unref(info);
    }

    return duration;
}
