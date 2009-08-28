/* desktop session recorder
 * Copyright (C) 2009 Benjamin Otte <otte@gnome.org
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

#include "byzanzencoderogv.h"

#include <glib/gi18n.h>
#include <gst/app/gstappbuffer.h>
#include <gst/video/video.h>

G_DEFINE_TYPE (ByzanzEncoderOgv, byzanz_encoder_ogv, BYZANZ_TYPE_ENCODER)

#define PIPELINE_STRING "appsrc name=src ! ffmpegcolorspace ! videorate ! video/x-raw-yuv,framerate=25/1 ! theoraenc ! oggmux ! giostreamsink name=sink"

static gboolean
byzanz_encoder_ogv_setup (ByzanzEncoder * encoder,
                          GOutputStream * stream,
                          guint           width,
                          guint           height,
                          GCancellable *  cancellable,
                          GError **	  error)
{
  ByzanzEncoderOgv *ogv = BYZANZ_ENCODER_OGG (encoder);
  GstElement *sink;

  ogv->pipeline = gst_parse_launch (PIPELINE_STRING, error);
  if (ogv->pipeline == NULL)
    return FALSE;

  g_assert (GST_IS_PIPELINE (ogv->pipeline));
  ogv->src = GST_APP_SRC (gst_bin_get_by_name (GST_BIN (ogv->pipeline), "src"));
  g_assert (GST_IS_APP_SRC (ogv->src));
  sink = gst_bin_get_by_name (GST_BIN (ogv->pipeline), "sink");
  g_assert (sink);
  g_object_set (sink, "stream", stream, NULL);
  g_object_unref (sink);

#if G_BYTE_ORDER == G_LITTLE_ENDIAN
  ogv->caps = gst_caps_from_string (GST_VIDEO_CAPS_BGRx);
#elif G_BYTE_ORDER == G_BIG_ENDIAN
  ogv->caps = gst_caps_new_from_string (GST_VIDEO_CAPS_xRGB);
#else
#error "Please add the Cairo caps format here"
#endif
  gst_caps_set_simple (ogv->caps,
      "width", G_TYPE_INT, width, 
      "height", G_TYPE_INT, height,
      "framerate", GST_TYPE_FRACTION, 0, 1, NULL);
  g_assert (gst_caps_is_fixed (ogv->caps));

  gst_app_src_set_caps (ogv->src, ogv->caps);
  gst_app_src_set_stream_type (ogv->src, GST_APP_STREAM_TYPE_STREAM);

  if (!gst_element_set_state (ogv->pipeline, GST_STATE_PLAYING)) {
    g_set_error_literal (error, G_IO_ERROR, G_IO_ERROR_FAILED, _("Failed to start GStreamer pipeline"));
    return FALSE;
  }

  return TRUE;
}

static gboolean
byzanz_encoder_ogv_got_error (ByzanzEncoderOgv *ogv, GError **error)
{
  GstBus *bus = gst_pipeline_get_bus (GST_PIPELINE (ogv->pipeline));
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
byzanz_encoder_ogv_process (ByzanzEncoder *   encoder,
                            GOutputStream *   stream,
                            cairo_surface_t * surface,
                            const GdkRegion * region,
                            const GTimeVal *  total_elapsed,
                            GCancellable *    cancellable,
                            GError **	      error)
{
  ByzanzEncoderOgv *ogv = BYZANZ_ENCODER_OGG (encoder);
  GstBuffer *buffer;

  if (!byzanz_encoder_ogv_got_error (ogv, error))
    return FALSE;

  /* update the surface */
  if (ogv->surface == NULL) {
    /* just assume that the size is right and pray */
    ogv->surface = cairo_surface_reference (surface);
    ogv->start_time = *total_elapsed;
  } else {
    cairo_t *cr;

    if (cairo_surface_get_reference_count (ogv->surface) > 1) {
      cairo_surface_t *copy = cairo_image_surface_create (CAIRO_FORMAT_RGB24,
          cairo_image_surface_get_width (ogv->surface), cairo_image_surface_get_height (ogv->surface));
      
      cr = cairo_create (copy);
      cairo_set_source_surface (cr, ogv->surface, 0, 0);
      cairo_paint (cr);
      cairo_destroy (cr);

      cairo_surface_destroy (ogv->surface);
      ogv->surface = copy;
    }
    cr = cairo_create (ogv->surface);
    cairo_set_source_surface (cr, surface, 0, 0);
    gdk_cairo_region (cr, region);
    cairo_fill (cr);
    cairo_destroy (cr);
  }

  /* create a buffer and send it */
  /* FIXME: stride just works? */
  cairo_surface_reference (ogv->surface);
  buffer = gst_app_buffer_new (cairo_image_surface_get_data (ogv->surface),
      cairo_image_surface_get_stride (ogv->surface) * cairo_image_surface_get_height (ogv->surface),
      (GstAppBufferFinalizeFunc) cairo_surface_destroy, ogv->surface);
  GST_BUFFER_FLAG_SET (buffer, GST_BUFFER_FLAG_READONLY);
  GST_BUFFER_TIMESTAMP (buffer) = GST_TIMEVAL_TO_TIME (*total_elapsed) - GST_TIMEVAL_TO_TIME (ogv->start_time);
  gst_buffer_set_caps (buffer, ogv->caps);
  gst_app_src_push_buffer (ogv->src, buffer);

  return TRUE;
}

static gboolean
byzanz_encoder_ogv_close (ByzanzEncoder *  encoder,
                          GOutputStream *  stream,
                          const GTimeVal * total_elapsed,
                          GCancellable *   cancellable,
                          GError **	   error)
{
  ByzanzEncoderOgv *ogv = BYZANZ_ENCODER_OGG (encoder);
  GstBus *bus;
  GstMessage *message;

  gst_app_src_end_of_stream (ogv->src);

  bus = gst_pipeline_get_bus (GST_PIPELINE (ogv->pipeline));
  message = gst_bus_timed_pop_filtered (bus, GST_CLOCK_TIME_NONE, GST_MESSAGE_ERROR | GST_MESSAGE_EOS);

  if (GST_MESSAGE_TYPE (message) == GST_MESSAGE_ERROR) {
    gst_message_parse_error (message, error, NULL);
    gst_message_unref (message);
    return FALSE;
  }
  gst_message_unref (message);
  g_object_unref (bus);
  gst_element_set_state (ogv->pipeline, GST_STATE_NULL);

  return TRUE;
}

static void
byzanz_encoder_ogv_finalize (GObject *object)
{
  ByzanzEncoderOgv *ogv = BYZANZ_ENCODER_OGG (object);

  if (ogv->pipeline) {
    gst_element_set_state (ogv->pipeline, GST_STATE_NULL);
    g_object_unref (ogv->pipeline);
  }
  if (ogv->src)
    g_object_unref (ogv->src);
  if (ogv->caps)
    gst_caps_unref (ogv->caps);

  G_OBJECT_CLASS (byzanz_encoder_ogv_parent_class)->finalize (object);
}

static void
byzanz_encoder_ogv_class_init (ByzanzEncoderOgvClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);
  ByzanzEncoderClass *encoder_class = BYZANZ_ENCODER_CLASS (klass);

  gst_init (NULL, NULL);

  object_class->finalize = byzanz_encoder_ogv_finalize;

  encoder_class->setup = byzanz_encoder_ogv_setup;
  encoder_class->process = byzanz_encoder_ogv_process;
  encoder_class->close = byzanz_encoder_ogv_close;

  encoder_class->filter = gtk_file_filter_new ();
  g_object_ref_sink (encoder_class->filter);
  gtk_file_filter_set_name (encoder_class->filter, _("Theora videos"));
  gtk_file_filter_add_mime_type (encoder_class->filter, "video/ogg");
  gtk_file_filter_add_pattern (encoder_class->filter, "*.ogv");
  gtk_file_filter_add_pattern (encoder_class->filter, "*.ogg");
}

static void
byzanz_encoder_ogv_init (ByzanzEncoderOgv *encoder_ogv)
{
}

