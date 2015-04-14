#ifndef __gst_chunks_shared_gst_h
#define __gst_chunks_shared_gst_h

#define GSTREAMER_FREE(ptr)  \
    if(ptr != NULL) {        \
        g_object_unref(ptr); \
        ptr = NULL;          \
    }

int gcs_gst_replace_element(GstElement *bin, GstElement *left_element,
    GstElement *right_element, GstElement *old_element,
    GstElement *new_element);

#endif /* __gst_chunks_shared_gst_h */
