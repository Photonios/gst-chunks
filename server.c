#include <stdio.h>
#include <stdlib.h>

#include <gst/gst.h>
#include <gst/rtsp-server/rtsp-server.h>

#define ALLOC_NULL(type, num) (type) calloc(1, num)

typedef struct {
	GstRTSPServer *server;
	GstRTSPMountPoints *mounts;
	GstRTSPMediaFactory *factory;
} SERVER_DATA;

static GMainLoop *loop;
static char *port = "8554";

int
main(int argc, char **argv)
{
	gst_init(&argc, &argv);

	loop = g_main_loop_new(NULL, FALSE);

	SERVER_DATA *data = ALLOC_NULL(SERVER_DATA *, sizeof(SERVER_DATA));
	
	data->server = gst_rtsp_server_new();
	data->mounts = gst_rtsp_server_get_mount_points(data->server);
	data->factory = gst_rtsp_media_factory_new();

	g_object_set(data->server, "service", port, NULL);

	gst_rtsp_media_factory_set_launch(data->factory,
		"filesrc location=test.mkv ! matroskademux ! rtph264pay name=pay0 pt=96");

	gst_rtsp_mount_points_add_factory(data->mounts, "/test", data->factory); 	

	gst_rtsp_server_attach(data->server, NULL);

	g_main_loop_run(loop);

cleanup:
	g_object_unref(loop);	
	return 0;
}

