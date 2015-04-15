#ifndef __gst_chunks_shared_gst_h
#define __gst_chunks_shared_gst_h

#define GSTREAMER_FREE(ptr)  \
    if(ptr != NULL) {        \
        g_object_unref(ptr); \
        ptr = NULL;          \
    }

#define GSTREAMER_DUMP_GRAPH(element, name) \
    GST_DEBUG_BIN_TO_DOT_FILE(GST_BIN(element), GST_DEBUG_GRAPH_SHOW_ALL, name)

int gcs_gst_replace_element(GstElement *bin, GstElement *left_element,
    GstElement *right_element, GstElement *old_element,
    GstElement *new_element);

#endif /* __gst_chunks_shared_gst_h */
