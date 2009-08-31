/* desktop session recorder
 * Copyright (C) 2009 Benjamin Otte <otte@gnome.org>
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Library General Public
 * License as published by the Free Software Foundation; either
 * version 3 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Library General Public License for more details.
 *
 * You should have received a copy of the GNU Library General Public
 * License along with this library; if not, write to the
 * Free Software Foundation, Inc., 59 Temple Place - Suite 330,
 * Boston, MA 02111-1307, USA.
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "byzanzencodergstreamer.h"

#include <glib/gi18n.h>
#include <gst/app/gstappbuffer.h>
#include <gst/video/video.h>

G_DEFINE_TYPE (ByzanzEncoderGStreamer, byzanz_encoder_gstreamer, BYZANZ_TYPE_ENCODER)

#define PIPELINE_STRING "appsrc name=src ! ffmpegcolorspace ! videorate ! video/x-raw-yuv,framerate=25/1 ! theoraenc ! oggmux ! giostreamsink name=sink"

static gboolean
byzanz_encoder_gstreamer_setup (ByzanzEncoder * encoder,
                          GOutputStream * stream,
                          guint           width,
                          guint           height,
                          GCancellable *  cancellable,
                          GError **	  error)
{
  ByzanzEncoderGStreamer *gstreamer = BYZANZ_ENCODER_GSTREAMER (encoder);
  ByzanzEncoderGStreamerClass *klass = BYZANZ_ENCODER_GSTREAMER_GET_CLASS (encoder);
  GstElement *sink;

  g_assert (klass->pipeline_string);
  gstreamer->pipeline = gst_parse_launch (klass->pipeline_string, error);
  if (gstreamer->pipeline == NULL)
    return FALSE;

  g_assert (GST_IS_PIPELINE (gstreamer->pipeline));
  gstreamer->src = GST_APP_SRC (gst_bin_get_by_name (GST_BIN (gstreamer->pipeline), "src"));
  g_assert (GST_IS_APP_SRC (gstreamer->src));
  sink = gst_bin_get_by_name (GST_BIN (gstreamer->pipeline), "sink");
  g_assert (sink);
  g_object_set (sink, "stream", stream, NULL);
  g_object_unref (sink);

#if G_BYTE_ORDER == G_LITTLE_ENDIAN
  gstreamer->caps = gst_caps_from_string (GST_VIDEO_CAPS_BGRx);
#elif G_BYTE_ORDER == G_BIG_ENDIAN
  gstreamer->caps = gst_caps_new_from_string (GST_VIDEO_CAPS_xRGB);
#else
#error "Please add the Cairo caps format here"
#endif
  gst_caps_set_simple (gstreamer->caps,
      "width", G_TYPE_INT, width, 
      "height", G_TYPE_INT, height,
      "framerate", GST_TYPE_FRACTION, 0, 1, NULL);
  g_assert (gst_caps_is_fixed (gstreamer->caps));

  gst_app_src_set_caps (gstreamer->src, gstreamer->caps);
  gst_app_src_set_stream_type (gstreamer->src, GST_APP_STREAM_TYPE_STREAM);

  if (!gst_element_set_state (gstreamer->pipeline, GST_STATE_PLAYING)) {
    g_set_error_literal (error, G_IO_ERROR, G_IO_ERROR_FAILED, _("Failed to start GStreamer pipeline"));
    return FALSE;
  }

  return TRUE;
}

static gboolean
byzanz_encoder_gstreamer_got_error (ByzanzEncoderGStreamer *gstreamer, GError **error)
{
  GstBus *bus = gst_pipeline_get_bus (GST_PIPELINE (gstreamer->pipeline));
  GstMessage *message = gst_bus_pop_filtered (bus, GST_MESSAGE_ERROR | GST_MESSAGE_EOS);

  g_object_unref (bus);
  if (message != NULL) {
    if (GST_MESSAGE_TYPE (message) == GST_MESSAGE_ERROR) {
      gst_message_parse_error (message, error, NULL);
    } else {
      g_set_error_literal (error, G_IO_ERROR, G_IO_ERROR_FAILED, _("Premature end of GStreamer pipeline"));
    }
    gst_message_unref (message);
    return FALSE;
  }

  return TRUE;
}

static gboolean
byzanz_encoder_gstreamer_process (ByzanzEncoder *   encoder,
                            GOutputStream *   stream,
                            cairo_surface_t * surface,
                            const GdkRegion * region,
                            const GTimeVal *  total_elapsed,
                            GCancellable *    cancellable,
                            GError **	      error)
{
  ByzanzEncoderGStreamer *gstreamer = BYZANZ_ENCODER_GSTREAMER (encoder);
  GstBuffer *buffer;

  if (!byzanz_encoder_gstreamer_got_error (gstreamer, error))
    return FALSE;

  /* update the surface */
  if (gstreamer->surface == NULL) {
    /* just assume that the size is right and pray */
    gstreamer->surface = cairo_surface_reference (surface);
    gstreamer->start_time = *total_elapsed;
  } else {
    cairo_t *cr;

    if (cairo_surface_get_reference_count (gstreamer->surface) > 1) {
      cairo_surface_t *copy = cairo_image_surface_create (CAIRO_FORMAT_RGB24,
          cairo_image_surface_get_width (gstreamer->surface), cairo_image_surface_get_height (gstreamer->surface));
      
      cr = cairo_create (copy);
      cairo_set_source_surface (cr, gstreamer->surface, 0, 0);
      cairo_paint (cr);
      cairo_destroy (cr);

      cairo_surface_destroy (gstreamer->surface);
      gstreamer->surface = copy;
    }
    cr = cairo_create (gstreamer->surface);
    cairo_set_source_surface (cr, surface, 0, 0);
    gdk_cairo_region (cr, region);
    cairo_fill (cr);
    cairo_destroy (cr);
  }

  /* create a buffer and send it */
  /* FIXME: stride just works? */
  cairo_surface_reference (gstreamer->surface);
  buffer = gst_app_buffer_new (cairo_image_surface_get_data (gstreamer->surface),
      cairo_image_surface_get_stride (gstreamer->surface) * cairo_image_surface_get_height (gstreamer->surface),
      (GstAppBufferFinalizeFunc) cairo_surface_destroy, gstreamer->surface);
  GST_BUFFER_FLAG_SET (buffer, GST_BUFFER_FLAG_READONLY);
  GST_BUFFER_TIMESTAMP (buffer) = GST_TIMEVAL_TO_TIME (*total_elapsed) - GST_TIMEVAL_TO_TIME (gstreamer->start_time);
  gst_buffer_set_caps (buffer, gstreamer->caps);
  gst_app_src_push_buffer (gstreamer->src, buffer);

  return TRUE;
}

static gboolean
byzanz_encoder_gstreamer_close (ByzanzEncoder *  encoder,
                          GOutputStream *  stream,
                          const GTimeVal * total_elapsed,
                          GCancellable *   cancellable,
                          GError **	   error)
{
  ByzanzEncoderGStreamer *gstreamer = BYZANZ_ENCODER_GSTREAMER (encoder);
  GstBus *bus;
  GstMessage *message;

  gst_app_src_end_of_stream (gstreamer->src);

  bus = gst_pipeline_get_bus (GST_PIPELINE (gstreamer->pipeline));
  message = gst_bus_timed_pop_filtered (bus, GST_CLOCK_TIME_NONE, GST_MESSAGE_ERROR | GST_MESSAGE_EOS);

  if (GST_MESSAGE_TYPE (message) == GST_MESSAGE_ERROR) {
    gst_message_parse_error (message, error, NULL);
    gst_message_unref (message);
    return FALSE;
  }
  gst_message_unref (message);
  g_object_unref (bus);
  gst_element_set_state (gstreamer->pipeline, GST_STATE_NULL);

  return TRUE;
}

static void
byzanz_encoder_gstreamer_finalize (GObject *object)
{
  ByzanzEncoderGStreamer *gstreamer = BYZANZ_ENCODER_GSTREAMER (object);

  if (gstreamer->pipeline) {
    gst_element_set_state (gstreamer->pipeline, GST_STATE_NULL);
    g_object_unref (gstreamer->pipeline);
  }
  if (gstreamer->src)
    g_object_unref (gstreamer->src);
  if (gstreamer->caps)
    gst_caps_unref (gstreamer->caps);

  G_OBJECT_CLASS (byzanz_encoder_gstreamer_parent_class)->finalize (object);
}

static void
byzanz_encoder_gstreamer_class_init (ByzanzEncoderGStreamerClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);
  ByzanzEncoderClass *encoder_class = BYZANZ_ENCODER_CLASS (klass);

  gst_init (NULL, NULL);

  object_class->finalize = byzanz_encoder_gstreamer_finalize;

  encoder_class->setup = byzanz_encoder_gstreamer_setup;
  encoder_class->process = byzanz_encoder_gstreamer_process;
  encoder_class->close = byzanz_encoder_gstreamer_close;
}

static void
byzanz_encoder_gstreamer_init (ByzanzEncoderGStreamer *encoder_gstreamer)
{
}

