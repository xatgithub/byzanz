/* desktop session recorder
 * Copyright (C) 2005 Benjamin Otte <otte@gnome.org
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Library General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
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

#include "byzanzrecorder.h"
#include <string.h>
#include <gtk/gtk.h>
#include <gdk/gdkx.h>
#include <X11/extensions/Xdamage.h>
#include "gifenc.h"
#include "i18n.h"

typedef enum {
  RECORDER_STATE_ERROR,
  RECORDER_STATE_CREATED,
  RECORDER_STATE_PREPARED,
  RECORDER_STATE_RECORDING,
  RECORDER_STATE_STOPPED
} RecorderState;

typedef enum {
  RECORDER_JOB_QUIT,
  RECORDER_JOB_QUANTIZE,
  RECORDER_JOB_ENCODE,
} RecorderJobType;

typedef struct {
  RecorderJobType	type;		/* type of job */
  GTimeVal		tv;		/* time this job was enqueued */
  GdkImage *		image;		/* image to process */
  GdkRegion *		region;		/* relevant region of image */
} RecorderJob;

struct _ByzanzRecorder {
  /*< private >*/
  /* set by user - accessed ALSO by thread */
  GdkRectangle		area;		/* area of the screen we record */
  gboolean		loop;		/* wether the resulting gif should loop */
  guint			frame_duration;	/* minimum frame duration in msecs */
  /* state */
  RecorderState		state;		/* state the recorder is in */
  guint			timeout;	/* signal id for timeout */
  GdkWindow *		window;		/* root window we record */
  Damage		damage;		/* the Damage object */
  XserverRegion		damaged;	/* the damaged region */
  XserverRegion		tmp_region;   	/* temporary variable for event handling */
  GdkRegion *		region;		/* the region we need to record next time */
  GThread *		encoder;	/* encoding thread */
  /* accessed ALSO by thread */
  GAsyncQueue *		jobs;		/* jobs the encoding thread has to do */
  GAsyncQueue *		finished;	/* store for leftover images */
  /* accessed ONLY by thread */
  Gifenc *		gifenc;		/* encoder used to encode the image */
  guint8 *		data;		/* data used to hold palettized data */
  GdkRectangle		relevant_data;	/* relevant area to encode */
};

/* XDamageQueryExtension returns these */
static int dmg_event_base = 0;
static int dmg_error_base = 0;
    

/*** JOB FUNCTIONS ***/

/* UGH: This function takes ownership of region, but only if a job could be created */
static RecorderJob *
recorder_job_new (ByzanzRecorder *rec, RecorderJobType type, 
    const GTimeVal *tv, GdkRegion *region)
{
  RecorderJob *job;

  job = g_async_queue_try_pop (rec->finished);
  if (!job) 
    job = g_new0 (RecorderJob, 1);
  
  g_assert (job->region == NULL);
  
  job->tv = *tv;
  job->type = type;
  job->region = region;
  if (region != NULL) {
    GdkRectangle *rects;
    gint nrects, i;
    if (!job->image) {
      job->image = gdk_image_new (GDK_IMAGE_FASTEST,
	  gdk_drawable_get_visual (rec->window),
	  rec->area.width, rec->area.height);
      if (!job->image) {
	g_free (job);
	return NULL;
      }
    } 
    gdk_region_get_rectangles (region, &rects, &nrects);
    for (i = 0; i < nrects; i++) {
      gdk_drawable_copy_to_image (rec->window, job->image, 
	  rects[i].x, rects[i].y, 
	  rects[i].x - rec->area.x, rects[i].y - rec->area.y, 
	  rects[i].width, rects[i].height);
    }
    gdk_region_offset (region, -rec->area.x, -rec->area.y);
  }
  return job;
}

static void
recorder_job_free (RecorderJob *job)
{
  if (job->image)
    g_object_unref (job->image);
  if (job->region)
    gdk_region_destroy (job->region);

  g_free (job);
}

/*** THREAD FUNCTIONS ***/

static void
byzanz_recorder_quantize (ByzanzRecorder *rec, GdkImage *image)
{
  GifencPalette *palette;

  palette = gifenc_quantize_image (
      image->mem + (image->bpp == 4 && image->byte_order == GDK_MSB_FIRST ? 1 : 0),
      rec->area.width, rec->area.height, image->bpp, image->bpl, TRUE,
      (image->byte_order == GDK_MSB_FIRST) ? G_BIG_ENDIAN : G_LITTLE_ENDIAN, 
      255);
  
  gifenc_set_palette (rec->gifenc, palette);
  if (rec->loop)
    gifenc_set_looping (rec->gifenc);
}

static void
byzanz_recorder_encode (ByzanzRecorder *rec, GdkImage *image, GdkRegion *region)
{
  GdkRectangle *rects;
  GdkRegion *rev;
  int i, line, nrects;
  guint8 transparent;

  g_assert (!gdk_region_empty (region));
  g_return_if_fail (image->bpp == 3 || image->bpp == 4);

  transparent = gifenc_palette_get_alpha_index (rec->gifenc->palette);
  gdk_region_get_clipbox (region, &rec->relevant_data);
  /* dither changed pixels */
  gdk_region_get_rectangles (region, &rects, &nrects);
  for (i = 0; i < nrects; i++) {
    gifenc_dither_rgb_into (rec->data + rec->area.width * rects[i].y + rects[i].x, 
	rec->area.width, rec->gifenc->palette,
	image->mem + rects[i].y * image->bpl + rects[i].x * image->bpp + 
	    (image->bpp == 4 && image->byte_order == GDK_MSB_FIRST ? 1 : 0),
	rects[i].width, rects[i].height, image->bpp, image->bpl);
  }
  g_free (rects);
  /* make non-relevant pixels transparent */
  rev = gdk_region_rectangle (&rec->relevant_data);
  gdk_region_subtract (rev, region);
  gdk_region_get_rectangles (rev, &rects, &nrects);
  for (i = 0; i < nrects; i++) {
    for (line = 0; line < rects[i].height; line++) {
      memset (rec->data + rects[i].x + rec->area.width * (rects[i].y + line), 
	  transparent, rects[i].width);
    }
  }
  g_free (rects);
  gdk_region_destroy (rev);
}

static void
byzanz_recorder_add_image (ByzanzRecorder *rec, const GTimeVal *now, 
    const GTimeVal *last)
{
  if (rec->data == NULL) {
    rec->data = g_malloc (rec->area.width * rec->area.height);
    return;
  }
  gifenc_add_image (rec->gifenc, rec->relevant_data.x, rec->relevant_data.y, 
      rec->relevant_data.width, rec->relevant_data.height, 
      (now->tv_sec - last->tv_sec) * 1000 + (now->tv_usec - last->tv_usec) / 1000 + 5, 
      rec->data + rec->area.width * rec->relevant_data.y + rec->relevant_data.x,
      rec->area.width);
}

static gpointer
byzanz_recorder_run_encoder (gpointer data)
{
  ByzanzRecorder *rec = data;
  RecorderJob *job;
  GTimeVal current;

  while (TRUE) {
    job = g_async_queue_pop (rec->jobs);
    switch (job->type) {
      case RECORDER_JOB_QUANTIZE:
	byzanz_recorder_quantize (rec, job->image);
	break;
      case RECORDER_JOB_ENCODE:
	byzanz_recorder_add_image (rec, &job->tv, &current);
	byzanz_recorder_encode (rec, job->image, job->region);
	current = job->tv;
	break;
      case RECORDER_JOB_QUIT:
	byzanz_recorder_add_image (rec, &job->tv, &current);
	recorder_job_free (job);
	while ((job = g_async_queue_try_pop (rec->finished)) != NULL)
	  recorder_job_free (job);
	g_free (rec->data);
	rec->data = NULL;
	return rec;
      default:
	g_assert_not_reached ();
	return rec;
    }
    if (job->region) {
      gdk_region_destroy (job->region);
      job->region = NULL;
    }
    g_async_queue_push (rec->finished, job);
  }
  
  g_assert_not_reached ();
  return rec;
}

/*** MAIN FUNCTIONS ***/

static gboolean byzanz_recorder_timeout_cb (gpointer recorder);
static void
byzanz_recorder_queue_image (ByzanzRecorder *rec)
{
  RecorderJob *job;
  GdkDisplay *display;
  Display *dpy;
  GTimeVal tv;
  
  g_assert (!gdk_region_empty (rec->region));

  g_get_current_time (&tv);
  job = recorder_job_new (rec, RECORDER_JOB_ENCODE, &tv, rec->region);
  g_async_queue_push (rec->jobs, job);
  rec->region = gdk_region_new ();
  display = gdk_display_get_default ();
  dpy = gdk_x11_display_get_xdisplay (display);
  XDamageSubtract (dpy, rec->damage, rec->damaged, None);
  XFixesSetRegion (dpy, rec->damaged, 0, 0);
  gdk_display_flush (display);
  if (rec->timeout == 0)
    rec->timeout = g_timeout_add (rec->frame_duration, 
	byzanz_recorder_timeout_cb, rec);
}

static gboolean
byzanz_recorder_timeout_cb (gpointer recorder)
{
  ByzanzRecorder *rec = recorder;

  if (gdk_region_empty (rec->region)) {
    rec->timeout = 0;
    return FALSE;
  }
  byzanz_recorder_queue_image (rec);
  return TRUE;
}

static GdkFilterReturn
byzanz_recorder_filter_damage_event (GdkXEvent *xevent, GdkEvent *event, gpointer data)
{
  ByzanzRecorder *rec = data;
  XDamageNotifyEvent *dev;
  GdkRectangle rect;
  Display *dpy;

  dev = (XDamageNotifyEvent *) xevent;

  if (dev->type != dmg_event_base + XDamageNotify)
    return GDK_FILTER_CONTINUE;
  if (dev->damage != rec->damage)
    return GDK_FILTER_CONTINUE;

  dpy = gdk_x11_display_get_xdisplay (gdk_display_get_default ());
  rect.x = dev->area.x;
  rect.y = dev->area.y;
  rect.width = dev->area.width;
  rect.height = dev->area.height;
  XFixesSetRegion (dpy, rec->tmp_region, &dev->area, 1);
  XFixesUnionRegion (dpy, rec->damaged, rec->damaged, rec->tmp_region);
  if (gdk_rectangle_intersect (&rect, &rec->area, &rect))
    gdk_region_union_with_rect (rec->region, &rect);

  if (rec->timeout == 0 && !dev->more)
    byzanz_recorder_queue_image (rec);
  return GDK_FILTER_REMOVE;
}

static void
byzanz_recorder_state_advance (ByzanzRecorder *recorder)
{
  switch (recorder->state) {
    case RECORDER_STATE_CREATED:
      byzanz_recorder_prepare (recorder);
      break;
    case RECORDER_STATE_PREPARED:
      byzanz_recorder_start (recorder);
      break;
    case RECORDER_STATE_RECORDING:
      byzanz_recorder_stop (recorder);
      break;
    case RECORDER_STATE_STOPPED:
    case RECORDER_STATE_ERROR:
    default:
      break;
  }
}

/**
 * byzanz_recorder_new:
 * @x: x coordinate on default root window
 * @y: y coordinate on default root window
 * @width: width of recording rectangle
 * @height: height of recoirding rectangle
 *
 * Creates a new #ByzanzRecorder and initializes all basic variables. 
 * gtk_init() and g_thread_init() must have been called before.
 *
 * Returns: a new #ByzanzRecorder or NULL if an error occured. Most likely
 *          the XDamage extension is not available on the current X server
 *          then. Another reason would be a thread creation failure.
 **/
ByzanzRecorder *
byzanz_recorder_new (const gchar *filename, gint x, gint y, 
    gint width, gint height, gboolean loop)
{
  ByzanzRecorder *recorder;
  Display *dpy;
  GdkRectangle root_rect;

  g_return_val_if_fail (filename != NULL, NULL);
  g_return_val_if_fail (x >= 0, NULL);
  g_return_val_if_fail (y >= 0, NULL);
  g_return_val_if_fail (width > 0, NULL);
  g_return_val_if_fail (height > 0, NULL);
  
  dpy = gdk_x11_display_get_xdisplay (gdk_display_get_default ());
  if (dmg_event_base == 0) {
    if (!XDamageQueryExtension (dpy, &dmg_event_base, &dmg_error_base))
      return NULL;
  }
  
  recorder = g_new (ByzanzRecorder, 1);

  /* set user properties */
  recorder->area.x = x;
  recorder->area.y = y;
  recorder->area.width = width;
  recorder->area.height = height;
  recorder->loop = loop;
  recorder->frame_duration = 1000 / 25;
  
  /* prepare thread first, so we can easily error out on failure */
  recorder->window = gdk_get_default_root_window ();
  root_rect.x = root_rect.y = 0;
  gdk_drawable_get_size (recorder->window,
      &root_rect.width, &root_rect.height);
  gdk_rectangle_intersect (&recorder->area, &root_rect, &recorder->area);
  recorder->gifenc = gifenc_open (recorder->area.width, recorder->area.height, filename);
  if (!recorder->gifenc) {
    g_free (recorder);
    return NULL;
  }
  recorder->jobs = g_async_queue_new ();
  recorder->finished = g_async_queue_new ();
  recorder->encoder = g_thread_create (byzanz_recorder_run_encoder, recorder, 
      TRUE, NULL);
  if (!recorder->encoder) {
    gifenc_close (recorder->gifenc);
    g_async_queue_unref (recorder->jobs);
    g_free (recorder);
    return NULL;
  }

  /* do setup work */
  recorder->damage = XDamageCreate (dpy, GDK_DRAWABLE_XID (recorder->window), 
      XDamageReportDeltaRectangles);
  recorder->region = gdk_region_new ();
  recorder->damaged = XFixesCreateRegion (dpy, 0, 0);
  recorder->tmp_region = XFixesCreateRegion (dpy, 0, 0);
  recorder->timeout = 0;
  recorder->region = NULL;
  
  recorder->data = NULL;

  recorder->state = RECORDER_STATE_CREATED;
  return recorder;
}

void
byzanz_recorder_prepare (ByzanzRecorder *rec)
{
  RecorderJob *job;
  GdkRegion *region;
  GTimeVal tv;
  
  g_return_if_fail (BYZANZ_IS_RECORDER (rec));
  g_return_if_fail (rec->state == RECORDER_STATE_CREATED);

  region = gdk_region_rectangle (&rec->area);
  g_get_current_time (&tv);
  job = recorder_job_new (rec, RECORDER_JOB_QUANTIZE, &tv, region);
  g_async_queue_push (rec->jobs, job);
  rec->state = RECORDER_STATE_PREPARED;
}

void
byzanz_recorder_start (ByzanzRecorder *rec)
{
  g_return_if_fail (BYZANZ_IS_RECORDER (rec));
  g_return_if_fail (rec->state == RECORDER_STATE_PREPARED);

  g_assert (rec->region == NULL);
  rec->region = gdk_region_rectangle (&rec->area);
  gdk_window_add_filter (NULL, 
      byzanz_recorder_filter_damage_event, rec);
  byzanz_recorder_queue_image (rec);
  
  rec->state = RECORDER_STATE_RECORDING;
}

void
byzanz_recorder_stop (ByzanzRecorder *rec)
{
  GTimeVal tv;
  RecorderJob *job;

  g_return_if_fail (BYZANZ_IS_RECORDER (rec));
  g_return_if_fail (rec->state == RECORDER_STATE_RECORDING);

  /* byzanz_recorder_queue_image (rec); - useless because last image would have a 0 time */
  g_get_current_time (&tv);
  job = recorder_job_new (rec, RECORDER_JOB_QUIT, &tv, NULL);
  g_async_queue_push (rec->jobs, job);
  gdk_window_remove_filter (NULL, 
      byzanz_recorder_filter_damage_event, rec);
  if (rec->timeout != 0) {
    if (!g_source_remove (rec->timeout))
      g_assert_not_reached ();
    rec->timeout = 0;
  }
  
  rec->state = RECORDER_STATE_STOPPED;
}

void
byzanz_recorder_destroy (ByzanzRecorder *rec)
{
  Display *dpy;

  g_return_if_fail (BYZANZ_IS_RECORDER (rec));

  while (rec->state != RECORDER_STATE_ERROR &&
         rec->state != RECORDER_STATE_STOPPED)
    byzanz_recorder_state_advance (rec);

  if (g_thread_join (rec->encoder) != rec)
    g_assert_not_reached ();
	  
  dpy = gdk_x11_display_get_xdisplay (gdk_display_get_default ());
  XFixesDestroyRegion (dpy, rec->damaged);
  XFixesDestroyRegion (dpy, rec->tmp_region);
  XDamageDestroy (dpy, rec->damage);
  gdk_region_destroy (rec->region);

  gifenc_close (rec->gifenc);

  g_free (rec);
}

