#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <time.h>

#include <signal.h>
#include <unistd.h>

#include <gst/gst.h>
 
#define ALLOC_NULL(type, num) (type) calloc(1, num)

typedef struct {
	GstElement *pipeline;
	GstBin *bin;
	GstElement *source;
	GstElement *destination;
	GstElement *parser;
	GstPad *parser_pad;
} PIPELINE_DATA;

static GMainLoop *loop;

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
	char *pipeline_end = " latency=100 ! rtph264depay ! h264parse name=parser ! matroskamux ! queue ! filesink name=destination";
	
	int pipeline_start_len = strlen(pipeline_start);
	int pipeline_end_len = strlen(pipeline_end);
	int url_len = strlen(url);

	int total_len = pipeline_start_len + pipeline_end_len + url_len;

	char *pipeline = ALLOC_NULL(char *, total_len + 1); /* for null terminator */
	strcpy(pipeline, pipeline_start);
	strcpy(pipeline + pipeline_start_len, url);
	strcpy(pipeline + pipeline_start_len + url_len, pipeline_end);

	pipeline[total_len] = '\0';
	return pipeline;
}

static char *
build_filename()
{
	time_t t = time(NULL);
	struct tm current = *localtime(&t);
	
	int filename_len = 26; /* lol */

	char *filename = ALLOC_NULL(char *, filename_len + 1);
	snprintf(filename, filename_len, "%02d-%02d-%04d_%02d;%02d;%02d.mkv", 
		current.tm_mday, current.tm_mon + 1, current.tm_year + 1900,
		current.tm_hour, current.tm_min, current.tm_sec);

	filename[filename_len] = '\0';
	return filename;
}

static void
set_file_destination(PIPELINE_DATA *data)
{
	char *filename = build_filename();
	g_object_set(data->destination, "location", filename, NULL);

	printf("Writing to: %s\n", filename);

	free(filename);
}

static GstPadProbeReturn
on_buffer_probe(GstPad *pad, GstPadProbeInfo *info, gpointer user_data)
{
	GstBuffer *buffer = GST_PAD_PROBE_INFO_BUFFER(info);
	
	if(!GST_BUFFER_FLAG_IS_SET(buffer, GST_BUFFER_FLAG_DELTA_UNIT)) {
		printf("Keyframe!\n");
		return GST_PAD_PROBE_REMOVE;	
	}

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

	/* add non-blocking probe to inspect buffer contents */	
	gst_pad_add_probe(data->parser_pad, GST_PAD_PROBE_TYPE_BUFFER,
		(GstPadProbeCallback) on_buffer_probe, data, NULL);		

	return G_SOURCE_CONTINUE;
}

int
main(int argc, char **argv)
{
	/* intercept SIGINT so we can can cleanly exit */
	signal(SIGINT, on_sigint);

	if(argc < 2) {
		fprintf(stderr, "Usage: record [rtsp url]\n");
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

	/* we no longer need access to the pipeline description this */
	free(pipeline_description);
	pipeline_description = NULL;

	/* construct data structure, containing references to elements we need a lot */

	PIPELINE_DATA *data = ALLOC_NULL(PIPELINE_DATA *, sizeof(PIPELINE_DATA));
	data->pipeline = pipeline;
	data->bin = GST_BIN(pipeline);

	if(data->bin == NULL) {
		fprintf(stderr, "Cast to GstBin* failed\n");
		goto cleanup;
	}

	g_object_ref(pipeline); /* casting to bin, increment ref coutn */		

	data->source = gst_bin_get_by_name(data->bin, "source");
	data->destination = gst_bin_get_by_name(data->bin, "destination");
	data->parser = gst_bin_get_by_name(data->bin, "parser");

	if(data->source == NULL || data->destination == NULL || data->parser == NULL) {
		fprintf(stderr, "Could not find `source`, `destination` or `parser` elements in the pipeline\n");
		goto cleanup;
	}

	/* the initial file to record to */
	set_file_destination(data);

	/* create main loop and add timeout to be called every 10 seconds */
	loop = g_main_loop_new(NULL, FALSE);
	g_timeout_add(10000, on_timeout, data);

	/* get src pad of the h264parse element, later, we'll add a probe
	to this pad to block the data flow when switching files */
	data->parser_pad = gst_element_get_static_pad(data->parser, "src");

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
	printf("Closing stream and file...\n");

	/* we do get_state here to wait for the state change to complete */
cleanup:
	gst_element_set_state(data->pipeline, GST_STATE_NULL);
	if(gst_element_get_state(data->pipeline, NULL, NULL, GST_CLOCK_TIME_NONE)
		== GST_STATE_CHANGE_FAILURE) {
		fprintf(stderr, "Failed to get the pipline into the NULL state\n");
	}

	g_object_unref(data->destination);
	g_object_unref(data->source);
	g_object_unref(data->pipeline);
	g_object_unref(data->bin);
	g_object_unref(loop);
	free(data);

	printf("Exiting\n");
	return 0;
}	
