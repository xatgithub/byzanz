/* desktop session recorder
 * Copyright (C) 2005,2009 Benjamin Otte <otte@gnome.org>
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
#  include "config.h"
#endif

#include "byzanzsession.h"

#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <cairo.h>
#include <glib/gstdio.h>
#include <glib/gi18n.h>
#include <gtk/gtk.h>
#include <gdk/gdkx.h>
#include <X11/extensions/Xdamage.h>
#include <X11/extensions/Xfixes.h>

#include "byzanzrecorder.h"
#include "gifenc.h"

typedef enum {
  SESSION_JOB_QUIT,
  SESSION_JOB_ENCODE,
} SessionJobType;

typedef struct {
  SessionJobType	type;		/* type of job */
  GTimeVal		tv;		/* time this job was enqueued */
  cairo_surface_t *	image;		/* image to process */
  GdkRegion *		region;		/* relevant region of image */
} SessionJob;

/*** JOB FUNCTIONS ***/

static void
session_job_free (SessionJob *job)
{
  if (job->image)
    cairo_surface_destroy (job->image);
  if (job->region)
    gdk_region_destroy (job->region);

  g_slice_free (SessionJob, job);
}

/* UGH: This function takes ownership of region, but only if a job could be created */
static SessionJob *
session_job_new (ByzanzSession *rec, SessionJobType type, cairo_surface_t *surface,
    const GTimeVal *tv, const GdkRegion *region)
{
  SessionJob *job;

  job = g_slice_new0 (SessionJob);
  
  if (tv)
    job->tv = *tv;
  job->type = type;
  if (region)
    job->region = gdk_region_copy (region);
  if (surface)
    job->image = cairo_surface_reference (surface);

  return job;
}

/*** THREAD FUNCTIONS ***/

static gboolean
byzanz_session_dither_region (ByzanzSession *rec, GdkRegion *region, cairo_surface_t *surface)
{
  GdkRectangle *rects;
  GdkRegion *rev;
  int i, line, nrects, xoffset, yoffset;
  guint8 transparent;
  guint width, stride;
  guint8 *mem;
  GdkRectangle area;
  double xod, yod;
  
  width = gifenc_get_width (rec->gifenc);
  transparent = gifenc_palette_get_alpha_index (rec->gifenc->palette);
  gdk_region_get_clipbox (region, &rec->relevant_data);
  /* dither changed pixels */
  gdk_region_get_rectangles (region, &rects, &nrects);
  rev = gdk_region_new ();
  stride = cairo_image_surface_get_stride (surface);
  cairo_surface_get_device_offset (surface, &xod, &yod);
  xoffset = xod;
  yoffset = yod;
  for (i = 0; i < nrects; i++) {
    mem = cairo_image_surface_get_data (surface) + (rects[i].x + xoffset) * 4
      + (rects[i].y + yoffset) * stride;
    if (gifenc_dither_rgb_with_full_image (
	rec->data + width * rects[i].y + rects[i].x, width, 
	rec->data_full + width * rects[i].y + rects[i].x, width, 
	rec->gifenc->palette, mem, rects[i].width, rects[i].height, stride, &area)) {
      area.x += rects[i].x;
      area.y += rects[i].y;
      gdk_region_union_with_rect (rev, &area);
    }
  }
  g_free (rects);
  gdk_region_get_clipbox (rev, &rec->relevant_data);
  gdk_region_destroy (rev);
  if (rec->relevant_data.width <= 0 && rec->relevant_data.height <= 0)
    return TRUE;
  
  /* make non-relevant pixels transparent */
  rev = gdk_region_rectangle (&rec->relevant_data);
  gdk_region_subtract (rev, region);
  gdk_region_get_rectangles (rev, &rects, &nrects);
  for (i = 0; i < nrects; i++) {
    for (line = 0; line < rects[i].height; line++) {
      memset (rec->data + rects[i].x + width * (rects[i].y + line), 
	  transparent, rects[i].width);
    }
  }
  g_free (rects);
  gdk_region_destroy (rev);
  return TRUE;
}

static void
byzanz_session_add_image (ByzanzSession *rec, const GTimeVal *tv)
{
  glong msecs;
  guint width;

  width = gifenc_get_width (rec->gifenc);
  if (rec->data == NULL) {
    guint count = width * gifenc_get_height (rec->gifenc);
    rec->data = g_malloc (count);
    rec->data_full = g_malloc (count);
    memset (rec->data_full, 
	gifenc_palette_get_alpha_index (rec->gifenc->palette), 
	count);
    rec->current = *tv;
    return;
  }
  msecs = (tv->tv_sec - rec->current.tv_sec) * 1000 + 
	  (tv->tv_usec - rec->current.tv_usec) / 1000 + 5;
  if (msecs < 10)
    g_printerr ("<10 msecs for a frame, can this be?\n");
  msecs = MAX (msecs, 10);
  if (rec->relevant_data.width > 0 && rec->relevant_data.height > 0) {
    gifenc_add_image (rec->gifenc, rec->relevant_data.x, rec->relevant_data.y, 
	rec->relevant_data.width, rec->relevant_data.height, msecs,
	rec->data + width * rec->relevant_data.y + rec->relevant_data.x,
	width, NULL);
    rec->current = *tv;
  }
}

static void
byzanz_session_quantize (ByzanzSession *rec, cairo_surface_t *image)
{
  GifencPalette *palette;

  palette = gifenc_quantize_image (cairo_image_surface_get_data (image),
      cairo_image_surface_get_width (image), cairo_image_surface_get_height (image),
      cairo_image_surface_get_stride (image), TRUE, 255);
  
  gifenc_initialize (rec->gifenc, palette, rec->loop, NULL);
}

static void
byzanz_session_encode (ByzanzSession *rec, cairo_surface_t *image, GdkRegion *region)
{
  g_assert (!gdk_region_empty (region));
  
  byzanz_session_dither_region (rec, region, image);
}

static gboolean
encoding_finished (gpointer data)
{
  ByzanzSession *session = data;

  if (g_thread_join (session->encoder) != session)
    g_assert_not_reached ();
  session->encoder = NULL;
  g_object_unref (session);

  g_object_notify (data, "encoding");

  return FALSE;
}

static gpointer
byzanz_session_run_encoder (gpointer data)
{
  ByzanzSession *rec = data;
  SessionJob *job;
  GTimeVal quit_tv;
  gboolean quit = FALSE;
  gboolean has_quantized = FALSE;

  while (!quit) {
    job = g_async_queue_pop (rec->jobs);

    switch (job->type) {
      case SESSION_JOB_ENCODE:
        if (!has_quantized) {
	  byzanz_session_quantize (rec, job->image);
          has_quantized = TRUE;
        }
        byzanz_session_add_image (rec, &job->tv);
        byzanz_session_encode (rec, job->image, job->region);
	break;
      case SESSION_JOB_QUIT:
	quit_tv = job->tv;
	quit = TRUE;
	break;
      default:
	g_assert_not_reached ();
	return rec;
    }
    session_job_free (job);
  }
  
  if (has_quantized) {
    byzanz_session_add_image (rec, &quit_tv);
    gifenc_close (rec->gifenc, NULL);
  }

  g_free (rec->data);
  rec->data = NULL;
  g_free (rec->data_full);
  rec->data_full = NULL;

  g_idle_add (encoding_finished, rec);
  return rec;
}

/*** MAIN FUNCTIONS ***/

enum {
  PROP_0,
  PROP_RECORDING,
  PROP_ENCODING,
  PROP_ERROR
};

G_DEFINE_TYPE (ByzanzSession, byzanz_session, G_TYPE_OBJECT)

static void
byzanz_session_get_property (GObject *object, guint param_id, GValue *value, 
    GParamSpec * pspec)
{
  ByzanzSession *session = BYZANZ_SESSION (object);

  switch (param_id) {
    case PROP_ERROR:
      g_value_set_pointer (value, session->error);
      break;
    case PROP_RECORDING:
      g_value_set_boolean (value, byzanz_session_is_recording (session));
      break;
    case PROP_ENCODING:
      g_value_set_boolean (value, byzanz_session_is_encoding (session));
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, param_id, pspec);
      break;
  }
}

static void
byzanz_session_set_property (GObject *object, guint param_id, const GValue *value, 
    GParamSpec * pspec)
{
  //ByzanzSession *session = BYZANZ_SESSION (object);

  switch (param_id) {
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, param_id, pspec);
      break;
  }
}

static void
byzanz_session_dispose (GObject *object)
{
  ByzanzSession *session = BYZANZ_SESSION (object);

  if (byzanz_recorder_get_recording (session->recorder)) {
    byzanz_session_stop (session);
    return;
  }

  G_OBJECT_CLASS (byzanz_session_parent_class)->dispose (object);
}

static void
byzanz_session_finalize (GObject *object)
{
  ByzanzSession *session = BYZANZ_SESSION (object);

  g_assert (session->encoder == NULL);

  gifenc_free (session->gifenc);
  g_object_unref (session->recorder);
  g_object_unref (session->stream);

  g_assert (g_async_queue_length (session->jobs) == 0);
  g_async_queue_unref (session->jobs);

  if (session->error)
    g_error_free (session->error);

  G_OBJECT_CLASS (byzanz_session_parent_class)->finalize (object);
}

#if 0
static void
byzanz_session_set_error (ByzanzSession *session, const GError *error)
{
  GObject *object = G_OBJECT (session);

  if (session->error != NULL)
    return;

  session->error = g_error_copy (error);
  g_object_freeze_notify (object);
  g_object_notify (object, "error");
  if (session->encoder != NULL)
    g_object_notify (object, "encoding");
  if (byzanz_recorder_get_recording (session->recorder))
    byzanz_session_stop (session);
  g_object_thaw_notify (object);
}
#endif

static void
byzanz_session_constructed (GObject *object)
{
  //ByzanzSession *session = BYZANZ_SESSION (object);

  if (G_OBJECT_CLASS (byzanz_session_parent_class)->constructed)
    G_OBJECT_CLASS (byzanz_session_parent_class)->constructed (object);
}

static void
byzanz_session_class_init (ByzanzSessionClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);

  object_class->get_property = byzanz_session_get_property;
  object_class->set_property = byzanz_session_set_property;
  object_class->dispose = byzanz_session_dispose;
  object_class->finalize = byzanz_session_finalize;
  object_class->constructed = byzanz_session_constructed;

  g_object_class_install_property (object_class, PROP_ERROR,
      g_param_spec_pointer ("error", "error", "error that happened on the thread",
	  G_PARAM_READABLE));
  g_object_class_install_property (object_class, PROP_RECORDING,
      g_param_spec_boolean ("recording", "recording", "TRUE while the recorder is running",
	  FALSE, G_PARAM_READABLE));
  g_object_class_install_property (object_class, PROP_ENCODING,
      g_param_spec_boolean ("encoding", "encoding", "TRUE while the encoder is running",
	  TRUE, G_PARAM_READABLE));
}

static void
byzanz_session_init (ByzanzSession *session)
{
  session->jobs = g_async_queue_new ();
}

static void
byzanz_session_recorder_image_cb (ByzanzRecorder *  recorder,
                                  cairo_surface_t * surface,
                                  const GdkRegion * region,
                                  const GTimeVal *  tv,
                                  ByzanzSession *   session)
{
  SessionJob *job = session_job_new (session, SESSION_JOB_ENCODE, surface, tv, region);
  g_async_queue_push (session->jobs, job);
}

static gboolean
session_gifenc_write (gpointer closure, const guchar *data, gsize len, GError **error)
{
  ByzanzSession *session = closure;

  return g_output_stream_write_all (session->stream, data, len, NULL, session->cancellable, error);
}

/**
 * byzanz_session_new:
 * @filename: filename to record to
 * @window: window to record
 * @area: area of window that should be recorded
 * @loop: if the resulting animation should loop
 * @record_cursor: if the cursor image should be recorded
 *
 * Creates a new #ByzanzSession and initializes all basic variables. 
 * gtk_init() and g_thread_init() must have been called before.
 *
 * Returns: a new #ByzanzSession or NULL if an error occured. Most likely
 *          the XDamage extension is not available on the current X server
 *          then. Another reason would be a thread creation failure.
 **/
ByzanzSession *
byzanz_session_new (GFile *destination, GdkWindow *window, GdkRectangle *area,
    gboolean loop, gboolean record_cursor)
{
  ByzanzSession *session;
  GdkRectangle root_rect;

  g_return_val_if_fail (G_IS_FILE (destination), NULL);
  g_return_val_if_fail (GDK_IS_WINDOW (window), NULL);
  g_return_val_if_fail (area != NULL, NULL);
  g_return_val_if_fail (area->x >= 0, NULL);
  g_return_val_if_fail (area->y >= 0, NULL);
  g_return_val_if_fail (area->width > 0, NULL);
  g_return_val_if_fail (area->height > 0, NULL);
  
  session = g_object_new (BYZANZ_TYPE_SESSION, NULL);

  /* set user properties */
  session->loop = loop;
  
  /* open file for writing */
  session->stream = G_OUTPUT_STREAM (g_file_replace (destination, NULL, 
        FALSE, G_FILE_CREATE_REPLACE_DESTINATION, session->cancellable, &session->error));
  if (session->stream == NULL)
    return session;

  /* prepare thread first, so we can easily error out on failure */
  root_rect.x = root_rect.y = 0;
  gdk_drawable_get_size (window,
      &root_rect.width, &root_rect.height);
  gdk_rectangle_intersect (area, &root_rect, &root_rect);
  session->gifenc = gifenc_new (root_rect.width, root_rect.height, 
      session_gifenc_write, session, NULL);

  session->encoder = g_thread_create (byzanz_session_run_encoder, session, 
      TRUE, &session->error);
  if (!session->encoder)
    return session;

  session->recorder = byzanz_recorder_new (window, &root_rect);
  g_signal_connect (session->recorder, "image", 
      G_CALLBACK (byzanz_session_recorder_image_cb), session);

  return session;
}

void
byzanz_session_start (ByzanzSession *session)
{
  g_return_if_fail (BYZANZ_IS_SESSION (session));

  byzanz_recorder_set_recording (session->recorder, TRUE);
  g_object_notify (G_OBJECT (session), "recording");
}

void
byzanz_session_stop (ByzanzSession *session)
{
  GTimeVal tv;
  SessionJob *job;

  g_return_if_fail (BYZANZ_IS_SESSION (session));

  g_object_ref (session);

  /* byzanz_session_queue_image (session); - useless because last image would have a 0 time */
  g_get_current_time (&tv);
  job = session_job_new (session, SESSION_JOB_QUIT, NULL, &tv, NULL);
  g_async_queue_push (session->jobs, job);
  
  byzanz_recorder_set_recording (session->recorder, FALSE);
  g_object_notify (G_OBJECT (session), "recording");
}

void
byzanz_session_abort (ByzanzSession *session)
{
  g_return_if_fail (BYZANZ_IS_SESSION (session));

  g_cancellable_cancel (session->cancellable);
}

gboolean
byzanz_session_is_recording (ByzanzSession *session)
{
  g_return_val_if_fail (BYZANZ_IS_SESSION (session), FALSE);

  return session->error == NULL &&
    byzanz_recorder_get_recording (session->recorder);
}

gboolean
byzanz_session_is_encoding (ByzanzSession *session)
{
  g_return_val_if_fail (BYZANZ_IS_SESSION (session), FALSE);

  return session->error == NULL &&
    session->encoder != NULL;
}

const GError *
byzanz_session_get_error (ByzanzSession *session)
{
  g_return_val_if_fail (BYZANZ_IS_SESSION (session), NULL);
  
  return session->error;
}

