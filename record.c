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
	GstElement *muxer;
	GstElement *parser;
	GstPad *parser_pad;
	GstPad *muxer_pad;
	GstPad *destination_pad;
	int is_switching;
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
	char *pipeline_end = " latency=100 ! rtph264depay ! h264parse name=parser ! matroskamux name=muxer ! queue ! filesink name=destination";
	
	int pipeline_start_len = strlen(pipeline_start);
	int pipeline_end_len = strlen(pipeline_end);
	int url_len = strlen(url);

	int total_len = pipeline_start_len + pipeline_end_len + url_len;

	char *pipeline = ALLOC_NULL(char *, total_len + 1); /* for null terminator */
	strcpy(pipeline, pipeline_start);
	strcpy(pipeline + pipeline_start_len, url);
	strcpy(pipeline + pipeline_start_len + url_len, pipeline_end);
	printf("LEN: %i\n", total_len);

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

	/* set flag to false again, so that we can switch once again */
	data->is_switching = FALSE;

	/* very important that we drop, otherwise the whole pipeline
	will halt */
	return GST_PAD_PROBE_DROP;
}

static GstPadProbeReturn
on_block_probe(GstPad *pad, GstPadProbeInfo *info, gpointer user_data)
{
	if(user_data == NULL) {
		fprintf(stderr, "In block probe callback the user data was NULL, fatal!\n");
		
		/* quit loop to exit application */
		if(loop != NULL) {
			g_main_loop_quit(loop);
		}

		return GST_PAD_PROBE_REMOVE;
	}

	PIPELINE_DATA *data = (PIPELINE_DATA *) user_data;
	gst_pad_remove_probe(pad, GST_PAD_PROBE_INFO_ID(info));

	/* add an event probe (that is blocking) on the sink pad of the muxer
	so when we sent EOS through the muxer, we'll know we receieved it */
	gst_pad_add_probe(data->muxer_pad, GST_PAD_PROBE_TYPE_BLOCK | 
		GST_PAD_PROBE_TYPE_EVENT_DOWNSTREAM, on_muxer_eos, data, NULL);

	/* send EOS through the muxer, causing it to correctly write everything
	to the file */
	gst_pad_send_event(data->muxer_pad, gst_event_new_eos());
	return GST_PAD_PROBE_OK;
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
		/* no key frame, continue waiting for one */
		return GST_PAD_PROBE_OK;	
	}

	/* remove the probe, prevent prbing again */
	gst_pad_remove_probe(pad, GST_PAD_PROBE_INFO_ID(info));

	/* add a blocking probe to block the data flow in the pipeline */
	gst_pad_add_probe(data->parser_pad, GST_PAD_PROBE_TYPE_BLOCK,
		(GstPadProbeCallback) on_block_probe, data, NULL);			

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
	if(data->is_switching)
		return G_SOURCE_CONTINUE;

	/* in case of short timeouts, it can happen that the timeout is called
	before we're done switching, set flag to prevent that */
	data->is_switching = TRUE;

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

	g_object_ref(pipeline); /* casting to bin, increment ref count */		

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
	data->destination_pad = gst_element_get_static_pad(data->destination, "sink");
	data->muxer_pad = gst_element_get_static_pad(data->muxer, "video_0");

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

	g_object_unref(data->destination);
	g_object_unref(data->source);
	g_object_unref(data->parser);
	g_object_unref(data->muxer);
	g_object_unref(data->parser_pad);
	g_object_unref(data->muxer_pad);
	g_object_unref(data->destination_pad);
	g_object_unref(data->pipeline);
	g_object_unref(data->bin);
	g_object_unref(loop);
	free(data);

	printf("Exiting\n");
	return 0;
}	
