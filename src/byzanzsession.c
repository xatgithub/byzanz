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
#include <gtk/gtk.h>
#include <gdk/gdkx.h>
#include <X11/extensions/Xdamage.h>
#include <X11/extensions/Xfixes.h>
#include "gifenc.h"
#include "i18n.h"

typedef enum {
  SESSION_STATE_ERROR,
  SESSION_STATE_CREATED,
  SESSION_STATE_RECORDING,
  SESSION_STATE_STOPPED
} SessionState;

typedef enum {
  SESSION_JOB_QUIT,
  SESSION_JOB_ENCODE,
} SessionJobType;

typedef gboolean (* DitherRegionGetDataFunc) (ByzanzSession *rec, 
    gpointer data, GdkRectangle *rect,
    gpointer *data_out, guint *bpl_out);

typedef struct {
  SessionJobType	type;		/* type of job */
  GTimeVal		tv;		/* time this job was enqueued */
  cairo_surface_t *	image;		/* image to process */
  GdkRegion *		region;		/* relevant region of image */
} SessionJob;

typedef struct {
  GdkRegion *		region;		/* the region this image represents */
  GTimeVal		tv;		/* timestamp of image */
  int			fd;		/* file the image is stored in */
  char *		filename;	/* only set if last image in file */
  off_t			offset;		/* offset at which the data starts */
} StoredImage;

struct _ByzanzSession {
  /*< private >*/
  /* set by user - accessed ALSO by thread */
  GdkRectangle		area;		/* area of the screen we record */
  gboolean		loop;		/* wether the resulting gif should loop */
  guint			frame_duration;	/* minimum frame duration in msecs */
  /* state */
  SessionState		state;		/* state the session is in */
  guint			timeout;	/* signal id for timeout */
  GdkWindow *		window;		/* root window we record */
  Damage		damage;		/* the Damage object */
  XserverRegion		damaged;	/* the damaged region */
  XserverRegion		tmp_region;	/* temporary region to construct the damaged region */
  GHashTable *		cursors;	/* all the cursors */
  XFixesCursorImage * 	cursor;		/* current cursor */
  gint			cursor_x;	/* last rendered x position of cursor */
  gint			cursor_y;	/* last rendered y position of cursor */
  GdkRectangle		cursor_area;	/* area occupied by cursor */
  GdkRegion *		region;		/* the region we need to record next time */
  GThread *		encoder;	/* encoding thread */
  /* accessed ALSO by thread */
  gint			encoder_running;/* TRUE while the encoder is running */
  GAsyncQueue *		jobs;		/* jobs the encoding thread has to do */
  /* accessed ONLY by thread */
  Gifenc *		gifenc;		/* encoder used to encode the image */
  GTimeVal		current;	/* timestamp of last encoded picture */
  guint8 *		data;		/* data used to hold palettized data */
  guint8 *		data_full;    	/* palettized data of full image to compare additions to */
  GdkRectangle		relevant_data;	/* relevant area to encode */
};
#define IS_RECORDING_CURSOR(rec) ((rec)->cursors != NULL)

/* XDamage needs these */
static int dmg_event_base = 0;
static int dmg_error_base = 0;
/* XFixes needs these */
static int fixes_event_base = 0;
static int fixes_error_base = 0;
    

/*** JOB FUNCTIONS ***/

static void
byzanz_cairo_set_source_window (cairo_t *cr, GdkWindow *window, double x, double y)
{
  cairo_t *tmp;

  tmp = gdk_cairo_create (window);
  cairo_set_source_surface (cr, cairo_get_target (tmp), x, y);
  cairo_destroy (tmp);
}

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
session_job_new (ByzanzSession *rec, SessionJobType type, 
    const GTimeVal *tv, GdkRegion *region)
{
  SessionJob *job;

  job = g_slice_new0 (SessionJob);
  
  if (tv)
    job->tv = *tv;
  job->type = type;
  job->region = region;
  if (region != NULL) {
    cairo_t *cr;
    job->image = cairo_image_surface_create (CAIRO_FORMAT_RGB24,
        rec->area.width, rec->area.height);
    if (type == SESSION_JOB_ENCODE) {
      Display *dpy = gdk_x11_drawable_get_xdisplay (rec->window);
      XDamageSubtract (dpy, rec->damage, rec->damaged, rec->damaged);
      XFixesSubtractRegion (dpy, rec->damaged, rec->damaged, rec->damaged);
    }
    cr = cairo_create (job->image);
    byzanz_cairo_set_source_window (cr, rec->window, -rec->area.x, -rec->area.y);
    gdk_region_offset (region, -rec->area.x, -rec->area.y);
    gdk_cairo_region (cr, region);
    cairo_paint (cr);
    cairo_destroy (cr);
  }
  return job;
}

/*** THREAD FUNCTIONS ***/

static gboolean
byzanz_session_dither_region (ByzanzSession *rec, GdkRegion *region,
    DitherRegionGetDataFunc func, gpointer data)
{
  GdkRectangle *rects;
  GdkRegion *rev;
  int i, line, nrects;
  guint8 transparent;
  guint bpl;
  gpointer mem;
  GdkRectangle area;
  
  transparent = gifenc_palette_get_alpha_index (rec->gifenc->palette);
  gdk_region_get_clipbox (region, &rec->relevant_data);
  /* dither changed pixels */
  gdk_region_get_rectangles (region, &rects, &nrects);
  rev = gdk_region_new ();
  for (i = 0; i < nrects; i++) {
    if (!(*func) (rec, data, rects + i, &mem, &bpl))
      return FALSE;
    if (gifenc_dither_rgb_with_full_image (
	rec->data + rec->area.width * rects[i].y + rects[i].x, rec->area.width, 
	rec->data_full + rec->area.width * rects[i].y + rects[i].x, rec->area.width, 
	rec->gifenc->palette, mem, rects[i].width, rects[i].height, bpl, &area)) {
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
      memset (rec->data + rects[i].x + rec->area.width * (rects[i].y + line), 
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
  if (rec->data == NULL) {
    guint count = rec->area.width * rec->area.height;
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
	rec->data + rec->area.width * rec->relevant_data.y + rec->relevant_data.x,
	rec->area.width, NULL);
    rec->current = *tv;
  }
}

static void
byzanz_session_quantize (ByzanzSession *rec, cairo_surface_t *image)
{
  GifencPalette *palette;

  palette = gifenc_quantize_image (cairo_image_surface_get_data (image),
      rec->area.width, rec->area.height, cairo_image_surface_get_stride (image), TRUE, 255);
  
  gifenc_initialize (rec->gifenc, palette, rec->loop, NULL);
}

static gboolean 
byzanz_session_encode_get_data (ByzanzSession *rec, gpointer data, GdkRectangle *rect,
    gpointer *data_out, guint *bpl_out)
{
  cairo_surface_t *image = data;

  *data_out = cairo_image_surface_get_data (image)
      + rect->y * cairo_image_surface_get_stride (image)
      + rect->x * 4;
  *bpl_out = cairo_image_surface_get_stride (image);
  return TRUE;
}

static void
byzanz_session_encode (ByzanzSession *rec, cairo_surface_t *image, GdkRegion *region)
{
  g_assert (!gdk_region_empty (region));
  
  byzanz_session_dither_region (rec, region, byzanz_session_encode_get_data,
      image);
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
  
  byzanz_session_add_image (rec, &quit_tv);
  gifenc_close (rec->gifenc, NULL);

  g_free (rec->data);
  rec->data = NULL;
  g_free (rec->data_full);
  rec->data_full = NULL;
  g_atomic_int_add (&rec->encoder_running, -1);

  return rec;
}

/*** MAIN FUNCTIONS ***/

static void
render_cursor_to_image (cairo_surface_t *image, XFixesCursorImage *cursor, gint x, gint y)
{
  cairo_surface_t *cursor_surface;
  cairo_t *cr;

  cursor_surface = cairo_image_surface_create_for_data ((guchar *) cursor->pixels,
      CAIRO_FORMAT_ARGB32, cursor->width, cursor->height, cursor->width * 4);
  cr = cairo_create (image);
  
  cairo_translate (cr, x, y);
  cairo_set_source_surface (cr, cursor_surface, -(double) cursor->xhot, -(double) cursor->yhot);
  cairo_paint (cr);

  cairo_destroy (cr);
  cairo_surface_destroy (cursor_surface);
}
    
static guint
cursor_hash (gconstpointer key)
{
  return (guint) ((const XFixesCursorImage *) key)->cursor_serial;
}

static gboolean
cursor_equal (gconstpointer c1, gconstpointer c2)
{
  return ((const XFixesCursorImage *) c1)->cursor_serial == 
    ((const XFixesCursorImage *) c2)->cursor_serial;
}

static gboolean byzanz_session_timeout_cb (gpointer session);
static void
byzanz_session_queue_image (ByzanzSession *rec)
{
  SessionJob *job;
  GTimeVal tv;
  gboolean render_cursor = FALSE;
  
  g_get_current_time (&tv);
  if (IS_RECORDING_CURSOR (rec)) {
    GdkRectangle cursor_rect;
    gdk_region_union_with_rect (rec->region, &rec->cursor_area);
    cursor_rect.x = rec->cursor_x - rec->cursor->xhot;
    cursor_rect.y = rec->cursor_y - rec->cursor->yhot;
    cursor_rect.width = rec->cursor->width;
    cursor_rect.height = rec->cursor->height;
    render_cursor = gdk_rectangle_intersect (&cursor_rect, &rec->area, &rec->cursor_area);
    gdk_region_union_with_rect (rec->region, &rec->cursor_area);
  } else {
    g_assert (!gdk_region_empty (rec->region));
  }
  
  if (!gdk_region_empty (rec->region)) {
    job = session_job_new (rec, SESSION_JOB_ENCODE, &tv, rec->region);
    if (job) {
      if (render_cursor) 
	render_cursor_to_image (job->image, rec->cursor, 
	    rec->cursor_x - rec->area.x, rec->cursor_y - rec->area.y);
      g_async_queue_push (rec->jobs, job);
      //g_print ("pushing ENCODE\n");
      rec->region = gdk_region_new ();
    }
  }
  
  if (rec->timeout == 0) {
    rec->timeout = g_timeout_add (rec->frame_duration, 
	byzanz_session_timeout_cb, rec);
  }
}

static gboolean
byzanz_session_timeout_cb (gpointer session)
{
  ByzanzSession *rec = session;

  if (IS_RECORDING_CURSOR (rec)) {
    gint x, y;
    gdk_window_get_pointer (rec->window, &x, &y, NULL);
    if (x == rec->cursor_x && y == rec->cursor_y && gdk_region_empty (rec->region))
      return TRUE;
    rec->cursor_x = x;
    rec->cursor_y = y;
  } else {
    if (gdk_region_empty (rec->region)) {
      rec->timeout = 0;
      return FALSE;
    }
  }
  byzanz_session_queue_image (rec);
  return TRUE;
}

static gboolean
byzanz_session_idle_cb (gpointer session)
{
  ByzanzSession *rec = session;

  g_assert (!gdk_region_empty (rec->region));

  rec->timeout = 0;
  byzanz_session_queue_image (rec);
  return FALSE;
}

static GdkFilterReturn
byzanz_session_filter_events (GdkXEvent *xevent, GdkEvent *event, gpointer data)
{
  ByzanzSession *rec = data;
  XDamageNotifyEvent *dev = (XDamageNotifyEvent *) xevent;
  Display *dpy;

  if (event->any.window != rec->window)
    return GDK_FILTER_CONTINUE;

  dev = (XDamageNotifyEvent *) xevent;
  dpy = gdk_x11_display_get_xdisplay (gdk_display_get_default ());

  if (dev->type == dmg_event_base + XDamageNotify && 
      dev->damage == rec->damage) {
    GdkRectangle rect;

    rect.x = dev->area.x;
    rect.y = dev->area.y;
    rect.width = dev->area.width;
    rect.height = dev->area.height;
    XFixesSetRegion (dpy, rec->tmp_region, &dev->area, 1);
    XFixesUnionRegion (dpy, rec->damaged, rec->damaged, rec->tmp_region);
    //XDamageSubtract (dpy, rec->damage, rec->damaged, None);
    //g_print ("-> %d %d %d %d\n", rect.x, rect.y, rect.width, rect.height);
    if (gdk_rectangle_intersect (&rect, &rec->area, &rect)) {
      gdk_region_union_with_rect (rec->region, &rect);
      if (rec->timeout == 0) 
	rec->timeout = g_idle_add_full (G_PRIORITY_DEFAULT,
	    byzanz_session_idle_cb, rec, NULL);
    }
    return GDK_FILTER_REMOVE;
  } else if (dev->type == fixes_event_base + XFixesCursorNotify) {
    XFixesCursorNotifyEvent *cevent = xevent;
    XFixesCursorImage hack;

    g_assert (IS_RECORDING_CURSOR (rec));
    hack.cursor_serial = cevent->cursor_serial;
    rec->cursor = g_hash_table_lookup (rec->cursors, &hack);
    if (rec->cursor == NULL) {
      rec->cursor = XFixesGetCursorImage (dpy);
      if (rec->cursor)
	g_hash_table_insert (rec->cursors, rec->cursor, rec->cursor);
    }
    return GDK_FILTER_REMOVE;
  }
  return GDK_FILTER_CONTINUE;
}

static void
byzanz_session_state_advance (ByzanzSession *session)
{
  switch (session->state) {
    case SESSION_STATE_CREATED:
      byzanz_session_start (session);
      break;
    case SESSION_STATE_RECORDING:
      byzanz_session_stop (session);
      break;
    case SESSION_STATE_STOPPED:
    case SESSION_STATE_ERROR:
    default:
      break;
  }
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
byzanz_session_new (const gchar *filename, GdkWindow *window, GdkRectangle *area,
    gboolean loop, gboolean record_cursor)
{
  gint fd;

  g_return_val_if_fail (filename != NULL, NULL);
  g_return_val_if_fail (area->x >= 0, NULL);
  g_return_val_if_fail (area->y >= 0, NULL);
  g_return_val_if_fail (area->width > 0, NULL);
  g_return_val_if_fail (area->height > 0, NULL);
  
  fd = g_open (filename, O_WRONLY | O_CREAT | O_TRUNC, 0666);
  if (fd < 0)
    return NULL;

  return byzanz_session_new_fd (fd, window, area, loop, record_cursor);
}

static gboolean
session_gifenc_write (gpointer closure, const guchar *data, gsize len, GError **error)
{
  gssize written;
  int fd = GPOINTER_TO_INT (closure);
  
  do {
    written = write (fd, data, len);
    if (written < 0) {
      int err = errno;

      if (err == EINTR)
	continue;
	  
      g_set_error (error, G_IO_ERROR, g_io_error_from_errno (err),
          _("Error writing: %s"), g_strerror (err));
      return FALSE;
    } else {
      len -= written;
      data += written;
    }
  } while (len > 0);

  return TRUE;
}

static void
session_gifenc_close (gpointer closure)
{
  close (GPOINTER_TO_INT (closure));
}

ByzanzSession *
byzanz_session_new_fd (gint fd, GdkWindow *window, GdkRectangle *area,
    gboolean loop, gboolean record_cursor)
{
  ByzanzSession *session;
  Display *dpy;
  GdkRectangle root_rect;

  g_return_val_if_fail (area->x >= 0, NULL);
  g_return_val_if_fail (area->y >= 0, NULL);
  g_return_val_if_fail (area->width > 0, NULL);
  g_return_val_if_fail (area->height > 0, NULL);
  
  dpy = gdk_x11_display_get_xdisplay (gdk_display_get_default ());
  if (dmg_event_base == 0) {
    if (!XDamageQueryExtension (dpy, &dmg_event_base, &dmg_error_base) ||
	!XFixesQueryExtension (dpy, &fixes_event_base, &fixes_error_base))
      return NULL;
    gdk_x11_register_standard_event_type (gdk_display_get_default (), 
	dmg_event_base + XDamageNotify, 1);
    gdk_x11_register_standard_event_type (gdk_display_get_default (), 
	fixes_event_base + XFixesCursorNotify, 1);
  }
  
  session = g_new0 (ByzanzSession, 1);

  /* set user properties */
  session->area = *area;
  session->loop = loop;
  session->frame_duration = 1000 / 25;
  
  /* prepare thread first, so we can easily error out on failure */
  session->window = window;
  g_object_ref (window);
  root_rect.x = root_rect.y = 0;
  gdk_drawable_get_size (session->window,
      &root_rect.width, &root_rect.height);
  gdk_rectangle_intersect (&session->area, &root_rect, &session->area);
  session->gifenc = gifenc_new (session->area.width, session->area.height, 
      session_gifenc_write, GINT_TO_POINTER (fd), session_gifenc_close);
  if (!session->gifenc) {
    g_free (session);
    return NULL;
  }
  session->jobs = g_async_queue_new ();
  session->encoder_running = 1;
  session->encoder = g_thread_create (byzanz_session_run_encoder, session, 
      TRUE, NULL);
  if (!session->encoder) {
    gifenc_free (session->gifenc);
    g_async_queue_unref (session->jobs);
    g_free (session);
    return NULL;
  }

  /* do setup work */
  session->damaged = XFixesCreateRegion (dpy, 0, 0);
  session->tmp_region = XFixesCreateRegion (dpy, 0, 0);
  if (record_cursor)
    session->cursors = g_hash_table_new_full (cursor_hash, cursor_equal, 
      NULL, (GDestroyNotify) XFree);

  session->state = SESSION_STATE_CREATED;
  return session;
}

void
byzanz_session_start (ByzanzSession *rec)
{
  Display *dpy;

  g_return_if_fail (BYZANZ_IS_SESSION (rec));
  g_return_if_fail (rec->state == SESSION_STATE_CREATED);

  dpy = gdk_x11_display_get_xdisplay (gdk_display_get_default ());
  rec->region = gdk_region_rectangle (&rec->area);
  gdk_window_add_filter (rec->window, 
      byzanz_session_filter_events, rec);
  rec->damage = XDamageCreate (dpy, GDK_DRAWABLE_XID (rec->window), 
      XDamageReportDeltaRectangles);
  if (rec->cursors) {
    XFixesSelectCursorInput (dpy, GDK_DRAWABLE_XID (rec->window),
	XFixesDisplayCursorNotifyMask);
    rec->cursor = XFixesGetCursorImage (dpy);
    if (rec->cursor)
      g_hash_table_insert (rec->cursors, rec->cursor, rec->cursor);
    gdk_window_get_pointer (rec->window, &rec->cursor_x, &rec->cursor_y, NULL);
  }
  /* byzanz_session_queue_image (rec); - we'll get a damage event anyway */
  
  rec->state = SESSION_STATE_RECORDING;
}

void
byzanz_session_stop (ByzanzSession *rec)
{
  GTimeVal tv;
  SessionJob *job;
  Display *dpy;

  g_return_if_fail (BYZANZ_IS_SESSION (rec));
  g_return_if_fail (rec->state == SESSION_STATE_RECORDING);

  /* byzanz_session_queue_image (rec); - useless because last image would have a 0 time */
  g_get_current_time (&tv);
  job = session_job_new (rec, SESSION_JOB_QUIT, &tv, NULL);
  g_async_queue_push (rec->jobs, job);
  //g_print ("pushing QUIT\n");
  gdk_window_remove_filter (rec->window, 
      byzanz_session_filter_events, rec);
  if (rec->timeout != 0) {
    if (!g_source_remove (rec->timeout))
      g_assert_not_reached ();
    rec->timeout = 0;
  }
  dpy = gdk_x11_display_get_xdisplay (gdk_display_get_default ());
  XDamageDestroy (dpy, rec->damage);
  if (IS_RECORDING_CURSOR (rec))
    XFixesSelectCursorInput (dpy, GDK_DRAWABLE_XID (rec->window),
	0);
  
  rec->state = SESSION_STATE_STOPPED;
}

void
byzanz_session_destroy (ByzanzSession *rec)
{
  Display *dpy;

  g_return_if_fail (BYZANZ_IS_SESSION (rec));

  while (rec->state != SESSION_STATE_ERROR &&
         rec->state != SESSION_STATE_STOPPED)
    byzanz_session_state_advance (rec);

  if (g_thread_join (rec->encoder) != rec)
    g_assert_not_reached ();

  dpy = gdk_x11_display_get_xdisplay (gdk_display_get_default ());
  XFixesDestroyRegion (dpy, rec->damaged);
  XFixesDestroyRegion (dpy, rec->tmp_region);
  gdk_region_destroy (rec->region);
  if (IS_RECORDING_CURSOR (rec))
    g_hash_table_destroy (rec->cursors);

  gifenc_free (rec->gifenc);
  g_object_unref (rec->window);

  g_assert (g_async_queue_length (rec->jobs) == 0);
  g_async_queue_unref (rec->jobs);
  
  g_free (rec);
}

/**
 * byzanz_session_is_active:
 * @session: ia recording session
 *
 * Checks if the session is currently running or - after being stopped - if 
 * the encoder is still actively processing cached data.
 * Note that byzanz_session_destroy() will block until all cached data has been 
 * processed, so it might take a long time.
 *
 * Returns: TRUE if the recording session is still active.
 **/
gboolean
byzanz_session_is_active (ByzanzSession *session)
{
  g_return_val_if_fail (BYZANZ_IS_SESSION (session), 0);
  
  return g_atomic_int_get (&session->encoder_running) > 0;
}
