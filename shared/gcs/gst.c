#include <stdio.h>
#include <stdint.h>

#include <gst/gst.h>

#include <gcs/mem.h>
#include <gcs/gst.h>

int
gcs_gst_replace_element(GstElement *bin, GstElement *left_element,
    GstElement *right_element, GstElement *old_element,
    GstElement *new_element)
{
    if(GST_IS_ELEMENT(left_element)) {
        gst_element_unlink(left_element, old_element);
    }

    if(GST_IS_ELEMENT(right_element)) {
        gst_element_unlink(old_element, right_element);
    }

    gst_bin_remove(GST_BIN(bin), old_element);
    gst_bin_add(GST_BIN(bin), new_element);

    if(GST_IS_ELEMENT(left_element)) {
        gst_element_link(left_element, new_element);
    }

    if(GST_IS_ELEMENT(right_element)) {
        gst_element_link(new_element, right_element);
    }

    return 1;
}
