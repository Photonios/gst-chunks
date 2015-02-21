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
	char *pipeline_end = " latency=100 ! rtph264depay ! h264parse ! matroskamux ! queue ! filesink name=destination";
	
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

void
set_file_destination(PIPELINE_DATA *data)
{
	char *filename = build_filename();
	g_object_set(data->destination, "location", filename, NULL);

	printf("Writing to: %s\n", filename);

	free(filename);
}

int
main(int argc, char **argv)
{
	signal(SIGINT, on_sigint);

	if(argc < 2) {
		fprintf(stderr, "Usage: record [rtsp url]\n");
		return 1;
	}

	gst_init(&argc, &argv);

	char *pipeline_description = build_pipeline(argv[1]);

	GError *error = NULL;
	GstElement *pipeline = gst_parse_launch(pipeline_description, &error);
	if(pipeline == NULL) {
		fprintf(stderr, "Failed to parse pipeline due to: %s\n%s\n", error->message, pipeline_description);
		return 1;
	}

	printf("Using pipeline: %s\n", pipeline_description);

	PIPELINE_DATA *data = ALLOC_NULL(PIPELINE_DATA *, sizeof(PIPELINE_DATA));
	data->pipeline = pipeline;
	data->bin = GST_BIN(pipeline);

	if(data->bin == NULL) {
		fprintf(stderr, "Cast to GstBin* failed\n");
		return 1;
	}

	g_object_ref(pipeline); /* casting to bin, increment ref coutn */		

	data->source = gst_bin_get_by_name(data->bin, "source");
	data->destination = gst_bin_get_by_name(data->bin, "destination");

	if(data->source == NULL || data->destination == NULL) {
		fprintf(stderr, "Could not find `source` or `destination` elements in the pipeline\n");
		return 1;
	}

	set_file_destination(data);

	gst_element_set_state(data->pipeline, GST_STATE_PLAYING);

	loop = g_main_loop_new(NULL, FALSE);
	g_main_loop_run(loop);

	printf("Closing stream and file...\n");

	gst_element_set_state(data->pipeline, GST_STATE_NULL);
	gst_element_get_state(data->pipeline, NULL, NULL, GST_CLOCK_TIME_NONE);

	g_object_unref(data->destination);
	g_object_unref(data->source);
	g_object_unref(data->pipeline);
	g_object_unref(data->bin);
	g_object_unref(loop);
	free(data);

	printf("Exiting\n");
	return 0;
}
