#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <time.h>

#include <signal.h>
#include <gst/gst.h>

#if !defined(_WIN32)
#	include <unistd.h>
#   include <sys/types.h>
#   include <sys/stat.h>
#endif

/* screw you windows */
#if defined(_WIN32)
#	define snprintf _snprintf
#endif

#define ALLOC_NULL(type, num) (type) calloc(1, num)

#define GSTREAMER_FREE(ptr)  \
	if(ptr != NULL) {		 \
		g_object_unref(ptr); \
		ptr = NULL;			 \
	}

typedef struct {
	GstElement *pipeline;
	GstBin *bin;
	GstElement *source;
	GstElement *destination;
	GstElement *muxer;
	GstElement *parser;
	GstPad *parser_pad;
	GstPad *muxer_pad;
	GstPadProbeInfo *buffer_probe;

    int is_switching;

    char *directory;
    int directory_len;
} PIPELINE_DATA;

static GMainLoop *loop;

static void
free_pipeline_data(PIPELINE_DATA *data)
{
	GSTREAMER_FREE(data->pipeline);
	GSTREAMER_FREE(data->bin);
	GSTREAMER_FREE(data->source);
	GSTREAMER_FREE(data->destination);
	GSTREAMER_FREE(data->muxer);
	GSTREAMER_FREE(data->parser);
	GSTREAMER_FREE(data->parser_pad);
	GSTREAMER_FREE(data->muxer_pad);

	data->buffer_probe = NULL;
	data->is_switching = FALSE;

	free(data);
	data = NULL;
}

static void
on_sigint(int signo)
{
	/* will cause the main loop to stop and clean up, process will exit */
	if(loop != NULL)
		g_main_loop_quit(loop);
}

static char *
build_pipeline(const char *url)
{
	char *pipeline_start = "rtspsrc name=source location=";
	char *pipeline_end = " latency=100 ! rtph264depay ! h264parse name=parser ! " \
		"matroskamux name=muxer ! queue ! filesink name=destination";

	int pipeline_start_len = strlen(pipeline_start);
	int pipeline_end_len = strlen(pipeline_end);
	int url_len = strlen(url);

	int total_len = pipeline_start_len + pipeline_end_len + url_len;

	char *pipeline = ALLOC_NULL(char *, total_len + 1); /* for null terminator */
	snprintf(pipeline, total_len + 1, "%s%s%s", pipeline_start, url, pipeline_end);

	pipeline[total_len] = '\0';
	return pipeline;
}

static char *
build_filename(char *directory, int directory_len)
{
	time_t t = time(NULL);
	struct tm current = *localtime(&t);

	/* 5 times 2 for the month, day, hour, minutes and seconds,
	plus 4 for the year, plus 5 for separators and 4 for .mkv*/
	int filename_len = 24 + directory_len;

	char *filename = ALLOC_NULL(char *, filename_len + 1);

	snprintf(filename, filename_len + 1, "%s/%02d-%02d-%04d_%02d;%02d;%02d.mkv",
		directory, current.tm_mday, current.tm_mon + 1, current.tm_year + 1900,
		current.tm_hour, current.tm_min, current.tm_sec);

	filename[filename_len] = '\0';
	return filename;
}

static void
set_file_destination(PIPELINE_DATA *data)
{
	char *filename = build_filename(data->directory, data->directory_len);
	g_object_set(data->destination, "location", filename, NULL);

	printf("Writing to: %s\n", filename);

	free(filename);
}

static GstPadProbeReturn
on_muxer_eos(GstPad *pad, GstPadProbeInfo *info, gpointer user_data)
{
	if(user_data == NULL) {
		fprintf(stderr, "In event (EOS) probe callback the user data was NULL, fatal!\n");

		/* quit loop to exit application */
		if(loop != NULL) {
			g_main_loop_quit(loop);
		}

		return GST_PAD_PROBE_REMOVE;
	}

	PIPELINE_DATA *data = (PIPELINE_DATA *) user_data;

	/* set the state of the muxer and filesink element
	to NULL, causing everything to be reset to the initial
	state (and handles closed etc) */
	gst_element_set_state(data->destination, GST_STATE_NULL);
	gst_element_set_state(data->muxer, GST_STATE_NULL);

	/* generate a new filename with the current date/time and
	apply it on the file sink */
	set_file_destination(data);

	/* remove the probe, preventing us from intercepting
	more events */
	gst_pad_remove_probe(pad, GST_PAD_PROBE_INFO_ID(info));

	/* sync state of the muxer and filesink with the parent,
	which is PLAYING, causing everything to resume again */
	gst_element_set_state(data->muxer, GST_STATE_PLAYING);
	gst_element_set_state(data->destination, GST_STATE_PLAYING);

	/* remove the blocking buffer probe so frames can once
	again pass through the pipeline */
	gst_pad_remove_probe(data->parser_pad, GST_PAD_PROBE_INFO_ID(data->buffer_probe));
	data->buffer_probe = NULL;

	/* set flag to false again, so that we can switch once again */
	data->is_switching = FALSE;

	/* very important that we drop, otherwise the whole pipeline
	will halt */
	return GST_PAD_PROBE_DROP;
}

static GstPadProbeReturn
on_buffer_probe(GstPad *pad, GstPadProbeInfo *info, gpointer user_data)
{
	if(user_data == NULL) {
		fprintf(stderr, "In buffer probe callback the user data was NULL, fatal!\n");

		/* quit loop to exit application */
		if(loop != NULL) {
			g_main_loop_quit(loop);
		}

		return GST_PAD_PROBE_REMOVE;
	}

	PIPELINE_DATA *data = (PIPELINE_DATA *) user_data;
	GstBuffer *buffer = GST_PAD_PROBE_INFO_BUFFER(info);

	/* is this frame a key frame? */
	if(GST_BUFFER_FLAG_IS_SET(buffer, GST_BUFFER_FLAG_DELTA_UNIT)) {
		/* not a key frame, in case we have not set buffer_probe,
		we are still waiting for a key frame, let the curent frame
		pass. if not, block that damn thing */
		if(data->buffer_probe == NULL) {
			return GST_PAD_PROBE_PASS;
		} else {
			return GST_PAD_PROBE_OK;
		}
	}

	/* setting so this probe can be removed when the
	switch is done */
	data->buffer_probe = info;

	/* add an event probe (that is blocking) on the sink pad of the muxer
	so when we sent eos through the muxer, we'll know we receieved it */
	gst_pad_add_probe(data->muxer_pad,  GST_PAD_PROBE_TYPE_BLOCK |
		GST_PAD_PROBE_TYPE_EVENT_DOWNSTREAM, on_muxer_eos, data, NULL);

	/* send eos through the muxer, causing it to correctly write everything
	to the file */
	gst_pad_send_event(data->muxer_pad, gst_event_new_eos());

	/* continue blocking */
	return GST_PAD_PROBE_OK;
}

static gboolean
on_timeout(gpointer user_data)
{
	if(user_data == NULL) {
		fprintf(stderr, "In timeout callback the user data was NULL, fatal!\n");

		/* quit loop to exit application */
		if(loop != NULL) {
			g_main_loop_quit(loop);
		}

		return G_SOURCE_REMOVE;
	}

	PIPELINE_DATA *data = (PIPELINE_DATA *) user_data;

	/* do not start another switch if still switching */
	if(data->is_switching) {
		return G_SOURCE_CONTINUE;
	}

	/* in case of short timeouts, it can happen that the timeout is called
	before we're done switching, set flag to prevent that */
	data->is_switching = TRUE;

	/* add a blocking probe to wait for a key frame, when we
	encounter a key frame, we'll do the switch, this way the
	first frame in the next file is a key frame */
	gst_pad_add_probe(data->parser_pad, GST_PAD_PROBE_TYPE_BUFFER | GST_PAD_PROBE_TYPE_BLOCK,
		on_buffer_probe, data, NULL);

	return G_SOURCE_CONTINUE;
}

static void
set_directory(PIPELINE_DATA *data, char *directory)
{
    data->directory = directory;
    data->directory_len = strlen(directory);

    /* if the directory does not exists, create it,
    otherwise filesink fails */

    DIR *dir = opendir(directory);

    if(!dir) {
        printf("Creating '%s' because it does not exists yet\n", directory);
        /* r/w/s permissions for owner r/s for others */
        mkdir(directory, S_IRWXU | S_IRWXG | S_IROTH | S_IXOTH);
    }

    closedir(dir);
}

int
main(int argc, char **argv)
{
	/* intercept SIGINT so we can can cleanly exit */
	signal(SIGINT, on_sigint);

	if(argc < 3) {
		fprintf(stderr, "Usage: chunk-recorder [rtsp url] [directory]\n");
		return 1;
	}

	/* initialize gstreamer, causes all plugins to be loaded */
	gst_init(&argc, &argv);

	/* construct pipeline by parsing the pipeline description */

	char *pipeline_description = build_pipeline(argv[1]);

	GError *error = NULL;
	GstElement *pipeline = gst_parse_launch(pipeline_description, &error);
	if(pipeline == NULL) {
		fprintf(stderr, "Failed to parse pipeline due to: %s\n%s\n", error->message, pipeline_description);

		/* avoid goto cleanup by cleaning up now */
		free(pipeline_description);
		pipeline_description = NULL;

		return 1;
	}

	printf("Using pipeline: %s\n", pipeline_description);

	/* we no longer need access to the pipeline description */
	free(pipeline_description);
	pipeline_description = NULL;

	PIPELINE_DATA *data = ALLOC_NULL(PIPELINE_DATA *, sizeof(PIPELINE_DATA));
	data->pipeline = pipeline;
	data->bin = GST_BIN(pipeline);

    set_directory(data, argv[2]);   /* this also assures that the directory exists */
	g_object_ref(pipeline);         /* casting to bin, increment ref count */

	data->source = gst_bin_get_by_name(data->bin, "source");
	data->destination = gst_bin_get_by_name(data->bin, "destination");
	data->parser = gst_bin_get_by_name(data->bin, "parser");
	data->muxer = gst_bin_get_by_name(data->bin, "muxer");

	if(!data->source || !data->destination || !data->parser || !data->muxer) {
		fprintf(stderr, "Could not find `source`, `destination`, `muxer` or `parser` elements in the pipeline\n");
		goto cleanup;
	}

	/* the initial file to record to */
	set_file_destination(data);

	/* create main loop and add timeout to be called every 10 seconds */
	loop = g_main_loop_new(NULL, FALSE);
	g_timeout_add(10000, on_timeout, data);

	/* get pads of some elements that we're going to use during
	the switching of files */
	data->parser_pad = gst_element_get_static_pad(data->parser, "src");
	data->muxer_pad = gst_element_get_static_pad(data->muxer, "video_0");

	if(!data->parser_pad || !data->muxer_pad) {
		fprintf(stderr, "Could not get `src` and `video_0` pads from the parser/muxer");
		goto cleanup;
	}

	/* start playing the pipeline, causes recording to start */
	gst_element_set_state(data->pipeline, GST_STATE_PLAYING);

	/* using get_state we wait for the state change to complete */
	if(gst_element_get_state(data->pipeline, NULL, NULL, GST_CLOCK_TIME_NONE)
		== GST_STATE_CHANGE_FAILURE) {
		fprintf(stderr, "Failed to get the pipeline into the PLAYING state\n");
		goto cleanup;
	}

	/* run main loop, is blocking until SIGINT is received */
	g_main_loop_run(loop);

	/* terminating, set pipeline to NULL and clean up */
	printf("Closing stream and file\n");

	/* we do get_state here to wait for the state change to complete */
cleanup:
	gst_element_set_state(data->pipeline, GST_STATE_NULL);
	if(gst_element_get_state(data->pipeline, NULL, NULL, GST_CLOCK_TIME_NONE)
		== GST_STATE_CHANGE_FAILURE) {
		fprintf(stderr, "Failed to get the pipline into the NULL state\n");
	}

	free_pipeline_data(data);

	printf("Exiting\n");
	return 0;
}
