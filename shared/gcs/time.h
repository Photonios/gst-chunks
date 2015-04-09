#ifndef __gst_chunks_shared_time_h
#define __gst_chunks_shared_time_h

#define GCS_TIME_NANO_AS_SECONDS(time) \
    (time / 1000000000)

#define GCS_TIME_SECONDS_AS_NANO(time) \
    (time * 1000000000)

#endif /* __gst_chunks_shared_time_h */
