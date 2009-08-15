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

#include "byzanzrecorder.h"
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <fcntl.h>
#include <cairo.h>
#include <glib/gstdio.h>
#include <gtk/gtk.h>
#include <gdk/gdkx.h>
#include <X11/extensions/Xdamage.h>
#include <X11/extensions/Xfixes.h>
#include "gifenc.h"
#include "i18n.h"

/* use a maximum of 50 Mbytes to cache images */
#define BYZANZ_RECORDER_MAX_CACHE (50*1024*1024)
/* as big as possible for 32bit ints without risking overflow
 * The current values gets overflows with pictures >= 2048x2048 */
#define BYZANZ_RECORDER_MAX_FILE_CACHE (0xFF000000)
/* split that into ~ 16 files please */
#define BYZANZ_RECORDER_MAX_FILE_SIZE (BYZANZ_RECORDER_MAX_FILE_CACHE / 16)

typedef enum {
  RECORDER_STATE_ERROR,
  RECORDER_STATE_CREATED,
  RECORDER_STATE_PREPARED,
  RECORDER_STATE_RECORDING,
  RECORDER_STATE_STOPPED
} RecorderState;

typedef enum {
  RECORDER_JOB_QUIT,
  RECORDER_JOB_QUIT_NOW,
  RECORDER_JOB_QUANTIZE,
  RECORDER_JOB_ENCODE,
  RECORDER_JOB_USE_FILE_CACHE,
} RecorderJobType;

typedef gboolean (* DitherRegionGetDataFunc) (ByzanzRecorder *rec, 
    gpointer data, GdkRectangle *rect,
    gpointer *data_out, guint *bpl_out);

typedef struct {
  RecorderJobType	type;		/* type of job */
  GTimeVal		tv;		/* time this job was enqueued */
  cairo_surface_t *	image;		/* image to process */
  GdkRegion *		region;		/* relevant region of image */
} RecorderJob;

typedef struct {
  GdkRegion *		region;		/* the region this image represents */
  GTimeVal		tv;		/* timestamp of image */
  int			fd;		/* file the image is stored in */
  char *		filename;	/* only set if last image in file */
  off_t			offset;		/* offset at which the data starts */
} StoredImage;

struct _ByzanzRecorder {
  /*< private >*/
  /* set by user - accessed ALSO by thread */
  GdkRectangle		area;		/* area of the screen we record */
  gboolean		loop;		/* wether the resulting gif should loop */
  guint			frame_duration;	/* minimum frame duration in msecs */
  guint			max_cache_size;	/* maximum allowed size of cache */
  gint			max_file_size;	/* maximum allowed size of one cache file - ATOMIC */
  gint			max_file_cache;	/* maximum allowed size of all cache files together - ATOMIC */
  /* state */
  guint			cache_size;	/* current cache size */
  RecorderState		state;		/* state the recorder is in */
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
  gint			use_file_cache :1; /* set whenever we signal using the file cache */
  /* accessed ALSO by thread */
  gint			encoder_running;/* TRUE while the encoder is running */
  GAsyncQueue *		jobs;		/* jobs the encoding thread has to do */
  GAsyncQueue *		finished;	/* store for leftover images */
  gint			cur_file_cache;	/* current amount of data cached in files */
  /* accessed ONLY by thread */
  Gifenc *		gifenc;		/* encoder used to encode the image */
  GTimeVal		current;	/* timestamp of last encoded picture */
  guint8 *		data;		/* data used to hold palettized data */
  guint8 *		data_full;    	/* palettized data of full image to compare additions to */
  GdkRectangle		relevant_data;	/* relevant area to encode */
  GQueue *		file_cache;	/* queue of sorted images */
  int			cur_cache_fd;	/* current cache file */
  char *		cur_cache_file;	/* name of current cache file */
  guint8 *		file_cache_data;	/* data read in from file */
  guint			file_cache_data_size;	/* data read in from file */
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

static guint
compute_image_size (cairo_surface_t *image)
{
  return cairo_image_surface_get_stride (image) * cairo_image_surface_get_height (image);
}

static void
recorder_job_free (ByzanzRecorder *rec, RecorderJob *job)
{
  if (job->image) {
    rec->cache_size -= compute_image_size (job->image);
    cairo_surface_destroy (job->image);
  }
  if (job->region)
    gdk_region_destroy (job->region);

  g_free (job);
}

/* UGH: This function takes ownership of region, but only if a job could be created */
static RecorderJob *
recorder_job_new (ByzanzRecorder *rec, RecorderJobType type, 
    const GTimeVal *tv, GdkRegion *region)
{
  RecorderJob *job;

  for (;;) {
    job = g_async_queue_try_pop (rec->finished);
    if (!job || !job->image)
      break;
    if (rec->cache_size - compute_image_size (job->image) <= rec->max_cache_size)
      break;
    recorder_job_free (rec, job);
  }
  if (!job) 
    job = g_new0 (RecorderJob, 1);
  
  g_assert (job->region == NULL);
  
  if (tv)
    job->tv = *tv;
  job->type = type;
  job->region = region;
  if (region != NULL) {
    cairo_t *cr;
    if (!job->image) {
      if (rec->cache_size <= rec->max_cache_size) {
	job->image = cairo_image_surface_create (CAIRO_FORMAT_RGB24,
	    rec->area.width, rec->area.height);
	rec->cache_size += compute_image_size (job->image);
	if (!rec->use_file_cache &&
	    rec->cache_size >= rec->max_cache_size / 2) {
	  RecorderJob *tmp;
	  guint count;
	  rec->use_file_cache = TRUE;
	  tmp = recorder_job_new (rec, RECORDER_JOB_USE_FILE_CACHE, NULL, NULL);
	  /* push job to the front */
	  g_async_queue_lock (rec->jobs);
	  count = g_async_queue_length_unlocked (rec->jobs);
	  //g_print ("pushing USE_FILE_CACHE\n");
	  g_async_queue_push_unlocked (rec->jobs, tmp);
	  while (count > 0) {
	    tmp = g_async_queue_pop_unlocked (rec->jobs);
	    g_async_queue_push_unlocked (rec->jobs, tmp);
	    count--;
	  }
	  g_async_queue_unlock (rec->jobs);
	}
      }
      if (!job->image) {
	g_free (job);
	return NULL;
      }
    } 
    if (type == RECORDER_JOB_ENCODE) {
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
byzanz_recorder_dither_region (ByzanzRecorder *rec, GdkRegion *region,
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
byzanz_recorder_add_image (ByzanzRecorder *rec, const GTimeVal *tv)
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
	rec->area.width);
    rec->current = *tv;
  }
}

static void
stored_image_remove_file (ByzanzRecorder *rec, int fd, char *filename)
{
  guint size;

  size = (guint) lseek (fd, 0, SEEK_END);
  g_atomic_int_add (&rec->cur_file_cache, - (gint) size);
  close (fd);
  g_unlink (filename);
  g_free (filename);
}

/* returns FALSE if no more images can be cached */
static gboolean
stored_image_store (ByzanzRecorder *rec, cairo_surface_t *image, GdkRegion *region, const GTimeVal *tv)
{
  off_t offset;
  StoredImage *store;
  GdkRectangle *rects;
  gint i, line, nrects;
  gboolean ret = FALSE;
  guint cache, val;
  guint stride;
  guchar *data;
  
  val = g_atomic_int_get (&rec->max_file_cache);
  cache = g_atomic_int_get (&rec->cur_file_cache);
  if (cache >= val) {
    g_print ("cache full %u/%u bytes\n", cache, val);
    return FALSE;
  }

  if (rec->cur_cache_fd < 0) {
    rec->cur_cache_fd =	g_file_open_tmp ("byzanzcacheXXXXXX", &rec->cur_cache_file, NULL);
    if (rec->cur_cache_fd < 0) {
      g_print ("no temp file: %d\n", rec->cur_cache_fd);
      return FALSE;
    }
    offset = 0;
  } else {
    offset = lseek (rec->cur_cache_fd, 0, SEEK_END);
  }
  store = g_new (StoredImage, 1);
  store->region = region;
  store->tv = *tv;
  store->fd = rec->cur_cache_fd;
  store->filename = NULL;
  store->offset = offset;
  gdk_region_get_rectangles (store->region, &rects, &nrects);
  stride = cairo_image_surface_get_stride (image);
  data = cairo_image_surface_get_data (image);
  for (i = 0; i < nrects; i++) {
    guchar *mem;
    mem = data + rects[i].x * 4 + rects[i].y * stride;
    for (line = 0; line < rects[i].height; line++) {
      int amount = rects[i].width * 4;
      /* This can be made smarter, like retrying and catching EINTR and stuff */
      if (write (store->fd, mem, amount) != amount) {
	g_print ("couldn't write %d bytes\n", amount);	
	goto out_err;
      }
      mem += stride;
    }
  }

  g_queue_push_tail (rec->file_cache, store);
  ret = TRUE;
out_err:
  offset = lseek (store->fd, 0, SEEK_CUR);
  g_assert (offset > 0); /* FIXME */
  val = g_atomic_int_get (&rec->max_file_size);
  if ((guint) offset >= val) {
    rec->cur_cache_fd = -1;
    if (!ret)
      store = g_queue_peek_tail (rec->file_cache);
    if (store->filename)
      stored_image_remove_file (rec, rec->cur_cache_fd, rec->cur_cache_file);
    else
      store->filename = rec->cur_cache_file;
    rec->cur_cache_file = NULL;
  }
  //g_print ("current file is stored from %u to %u\n", (guint) store->offset, (guint) offset);
  offset -= store->offset;
  g_atomic_int_add (&rec->cur_file_cache, offset);
  //g_print ("cache size is now %u\n", g_atomic_int_get (&rec->cur_file_cache));

  return ret;
}

static gboolean
stored_image_dither_get_data (ByzanzRecorder *rec, gpointer data, GdkRectangle *rect,
    gpointer *data_out, guint *bpl_out)
{
  StoredImage *store = data;
  guint required_size = rect->width * rect->height * 4;
  guint8 *ptr;

  if (required_size > rec->file_cache_data_size) {
    rec->file_cache_data = g_realloc (rec->file_cache_data, required_size);
    rec->file_cache_data_size = required_size;
  }

  ptr = rec->file_cache_data;
  while (required_size > 0) {
    int ret = read (store->fd, ptr, required_size);
    if (ret < 0)
      return FALSE;
    ptr += ret;
    required_size -= ret;
  }
  *bpl_out = 4 * rect->width;
  *data_out = rec->file_cache_data;
  return TRUE;
}

static gboolean
stored_image_process (ByzanzRecorder *rec)
{
  StoredImage *store;
  gboolean ret;

  store = g_queue_pop_head (rec->file_cache);
  if (!store)
    return FALSE;

  /* FIXME: can that assertion trigger? */
  if (store->offset != lseek (store->fd, store->offset, SEEK_SET)) {
    g_printerr ("Couldn't seek to %d\n", (int) store->offset);
    g_assert_not_reached ();
  }
  byzanz_recorder_add_image (rec, &store->tv);
  lseek (store->fd, store->offset, SEEK_SET);
  ret = byzanz_recorder_dither_region (rec, store->region, stored_image_dither_get_data, store);

  if (store->filename)
    stored_image_remove_file (rec, store->fd, store->filename);
  gdk_region_destroy (store->region);
  g_free (store);
  return ret;
}

static void
byzanz_recorder_quantize (ByzanzRecorder *rec, cairo_surface_t *image)
{
  GifencPalette *palette;

  palette = gifenc_quantize_image (cairo_image_surface_get_data (image),
      rec->area.width, rec->area.height, cairo_image_surface_get_stride (image), TRUE, 255);
  
  gifenc_set_palette (rec->gifenc, palette);
  if (rec->loop)
    gifenc_set_looping (rec->gifenc);
}

static gboolean 
byzanz_recorder_encode_get_data (ByzanzRecorder *rec, gpointer data, GdkRectangle *rect,
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
byzanz_recorder_encode (ByzanzRecorder *rec, cairo_surface_t *image, GdkRegion *region)
{
  g_assert (!gdk_region_empty (region));
  
  byzanz_recorder_dither_region (rec, region, byzanz_recorder_encode_get_data,
      image);
}

static gpointer
byzanz_recorder_run_encoder (gpointer data)
{
  ByzanzRecorder *rec = data;
  RecorderJob *job;
  GTimeVal quit_tv;
  gboolean quit = FALSE;
#define USING_FILE_CACHE(rec) ((rec)->file_cache_data_size > 0)

  rec->cur_cache_fd = -1;
  rec->file_cache = g_queue_new ();

  while (TRUE) {
    if (USING_FILE_CACHE (rec)) {
loop:
      job = g_async_queue_try_pop (rec->jobs);
      if (!job) {
	if (!stored_image_process (rec)) {
	  if (quit)
	    break;
	  goto loop;
	}
	if (quit)
	  goto loop;
	job = g_async_queue_pop (rec->jobs);
      }
    } else {
      if (quit)
	break;
      job = g_async_queue_pop (rec->jobs);
    }
    switch (job->type) {
      case RECORDER_JOB_QUANTIZE:
	byzanz_recorder_quantize (rec, job->image);
	break;
      case RECORDER_JOB_ENCODE:
	if (USING_FILE_CACHE (rec)) {
	  while (!stored_image_store (rec, job->image, job->region, &job->tv)) {
	    if (!stored_image_process (rec))
	      /* fix this (bad error handling here) */
	      g_assert_not_reached ();
	  }
	  job->region = NULL;
	} else {
	  byzanz_recorder_add_image (rec, &job->tv);
	  byzanz_recorder_encode (rec, job->image, job->region);
	}
	break;
      case RECORDER_JOB_USE_FILE_CACHE:
	if (!USING_FILE_CACHE (rec)) {
	  rec->file_cache_data_size = 4 * 64 * 64;
	  rec->file_cache_data = g_malloc (rec->file_cache_data_size);
	}
	break;
      case RECORDER_JOB_QUIT_NOW:
	/* clean up cache files and exit */
	g_assert_not_reached ();
	break;
      case RECORDER_JOB_QUIT:
	quit_tv = job->tv;
	quit = TRUE;
	break;
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
  
  byzanz_recorder_add_image (rec, &quit_tv);

  g_free (rec->data);
  rec->data = NULL;
  g_free (rec->data_full);
  rec->data_full = NULL;
  if (USING_FILE_CACHE (rec)) {
    if (rec->cur_cache_fd) {
      stored_image_remove_file (rec, rec->cur_cache_fd, rec->cur_cache_file);
      rec->cur_cache_file = NULL;
      rec->cur_cache_fd = -1;
    }
    g_free (rec->file_cache_data);
    rec->file_cache_data = NULL;
    rec->file_cache_data_size = 0;
  }
  g_queue_free (rec->file_cache);
  g_atomic_int_add (&rec->encoder_running, -1);

  return rec;
#undef USING_FILE_CACHE
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

static gboolean byzanz_recorder_timeout_cb (gpointer recorder);
static void
byzanz_recorder_queue_image (ByzanzRecorder *rec)
{
  RecorderJob *job;
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
    job = recorder_job_new (rec, RECORDER_JOB_ENCODE, &tv, rec->region);
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
	byzanz_recorder_timeout_cb, rec);
  }
}

static gboolean
byzanz_recorder_timeout_cb (gpointer recorder)
{
  ByzanzRecorder *rec = recorder;

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
  byzanz_recorder_queue_image (rec);
  return TRUE;
}

static gboolean
byzanz_recorder_idle_cb (gpointer recorder)
{
  ByzanzRecorder *rec = recorder;

  g_assert (!gdk_region_empty (rec->region));

  rec->timeout = 0;
  byzanz_recorder_queue_image (rec);
  return FALSE;
}

static GdkFilterReturn
byzanz_recorder_filter_events (GdkXEvent *xevent, GdkEvent *event, gpointer data)
{
  ByzanzRecorder *rec = data;
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
	    byzanz_recorder_idle_cb, rec, NULL);
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
 * @filename: filename to record to
 * @window: window to record
 * @area: area of window that should be recorded
 * @loop: if the resulting animation should loop
 * @record_cursor: if the cursor image should be recorded
 *
 * Creates a new #ByzanzRecorder and initializes all basic variables. 
 * gtk_init() and g_thread_init() must have been called before.
 *
 * Returns: a new #ByzanzRecorder or NULL if an error occured. Most likely
 *          the XDamage extension is not available on the current X server
 *          then. Another reason would be a thread creation failure.
 **/
ByzanzRecorder *
byzanz_recorder_new (const gchar *filename, GdkWindow *window, GdkRectangle *area,
    gboolean loop, gboolean record_cursor)
{
  gint fd;

  g_return_val_if_fail (filename != NULL, NULL);
  g_return_val_if_fail (area->x >= 0, NULL);
  g_return_val_if_fail (area->y >= 0, NULL);
  g_return_val_if_fail (area->width > 0, NULL);
  g_return_val_if_fail (area->height > 0, NULL);
  g_return_val_if_fail (gdk_drawable_get_depth (window) == 24 || \
      gdk_drawable_get_depth (window) == 32, NULL);
  
  fd = g_open (filename, O_WRONLY | O_CREAT | O_TRUNC, 0666);
  if (fd < 0)
    return NULL;

  return byzanz_recorder_new_fd (fd, window, area, loop, record_cursor);
}

ByzanzRecorder *
byzanz_recorder_new_fd (gint fd, GdkWindow *window, GdkRectangle *area,
    gboolean loop, gboolean record_cursor)
{
  ByzanzRecorder *recorder;
  Display *dpy;
  GdkRectangle root_rect;

  g_return_val_if_fail (area->x >= 0, NULL);
  g_return_val_if_fail (area->y >= 0, NULL);
  g_return_val_if_fail (area->width > 0, NULL);
  g_return_val_if_fail (area->height > 0, NULL);
  g_return_val_if_fail (gdk_drawable_get_depth (window) == 24 || \
      gdk_drawable_get_depth (window) == 32, NULL);
  
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
  
  recorder = g_new0 (ByzanzRecorder, 1);

  /* set user properties */
  recorder->area = *area;
  recorder->loop = loop;
  recorder->frame_duration = 1000 / 25;
  recorder->max_cache_size = BYZANZ_RECORDER_MAX_CACHE;
  recorder->max_file_size = BYZANZ_RECORDER_MAX_FILE_SIZE;
  recorder->max_file_cache = BYZANZ_RECORDER_MAX_FILE_CACHE;
  
  /* prepare thread first, so we can easily error out on failure */
  recorder->window = window;
  g_object_ref (window);
  root_rect.x = root_rect.y = 0;
  gdk_drawable_get_size (recorder->window,
      &root_rect.width, &root_rect.height);
  gdk_rectangle_intersect (&recorder->area, &root_rect, &recorder->area);
  recorder->gifenc = gifenc_open_fd (fd, recorder->area.width, recorder->area.height);
  if (!recorder->gifenc) {
    g_free (recorder);
    return NULL;
  }
  recorder->jobs = g_async_queue_new ();
  recorder->finished = g_async_queue_new ();
  recorder->encoder_running = 1;
  recorder->encoder = g_thread_create (byzanz_recorder_run_encoder, recorder, 
      TRUE, NULL);
  if (!recorder->encoder) {
    gifenc_close (recorder->gifenc);
    g_async_queue_unref (recorder->jobs);
    g_free (recorder);
    return NULL;
  }

  /* do setup work */
  recorder->damaged = XFixesCreateRegion (dpy, 0, 0);
  recorder->tmp_region = XFixesCreateRegion (dpy, 0, 0);
  if (record_cursor)
    recorder->cursors = g_hash_table_new_full (cursor_hash, cursor_equal, 
      NULL, (GDestroyNotify) XFree);

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
  //g_print ("pushing QUANTIZE\n");
  rec->state = RECORDER_STATE_PREPARED;
}

void
byzanz_recorder_start (ByzanzRecorder *rec)
{
  Display *dpy;

  g_return_if_fail (BYZANZ_IS_RECORDER (rec));
  g_return_if_fail (rec->state == RECORDER_STATE_PREPARED);

  g_assert (rec->region == NULL);

  dpy = gdk_x11_display_get_xdisplay (gdk_display_get_default ());
  rec->region = gdk_region_rectangle (&rec->area);
  gdk_window_add_filter (rec->window, 
      byzanz_recorder_filter_events, rec);
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
  /* byzanz_recorder_queue_image (rec); - we'll get a damage event anyway */
  
  rec->state = RECORDER_STATE_RECORDING;
}

void
byzanz_recorder_stop (ByzanzRecorder *rec)
{
  GTimeVal tv;
  RecorderJob *job;
  Display *dpy;

  g_return_if_fail (BYZANZ_IS_RECORDER (rec));
  g_return_if_fail (rec->state == RECORDER_STATE_RECORDING);

  /* byzanz_recorder_queue_image (rec); - useless because last image would have a 0 time */
  g_get_current_time (&tv);
  job = recorder_job_new (rec, RECORDER_JOB_QUIT, &tv, NULL);
  g_async_queue_push (rec->jobs, job);
  //g_print ("pushing QUIT\n");
  gdk_window_remove_filter (rec->window, 
      byzanz_recorder_filter_events, rec);
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
  
  rec->state = RECORDER_STATE_STOPPED;
}

void
byzanz_recorder_destroy (ByzanzRecorder *rec)
{
  Display *dpy;
  RecorderJob *job;

  g_return_if_fail (BYZANZ_IS_RECORDER (rec));

  while (rec->state != RECORDER_STATE_ERROR &&
         rec->state != RECORDER_STATE_STOPPED)
    byzanz_recorder_state_advance (rec);

  if (g_thread_join (rec->encoder) != rec)
    g_assert_not_reached ();

  while ((job = g_async_queue_try_pop (rec->finished)) != NULL)
    recorder_job_free (rec, job);
  dpy = gdk_x11_display_get_xdisplay (gdk_display_get_default ());
  XFixesDestroyRegion (dpy, rec->damaged);
  XFixesDestroyRegion (dpy, rec->tmp_region);
  gdk_region_destroy (rec->region);
  if (IS_RECORDING_CURSOR (rec))
    g_hash_table_destroy (rec->cursors);

  gifenc_close (rec->gifenc);
  g_object_unref (rec->window);

  g_assert (g_async_queue_length (rec->jobs) == 0);
  g_assert (rec->cache_size == 0);
  
  g_free (rec);
}

/**
 * byzanz_recorder_set_max_cache:
 * @rec: a recording session
 * @max_cache_bytes: maximum allowed cache size in bytes
 *
 * Sets the maximum allowed cache size. Since the recorder uses two threads -
 * one for taking screenshots and one for encoding these screenshots into the
 * final file, on heavy screen changes a big number of screenshot images can 
 * build up waiting to be encoded. This value is used to determine the maximum
 * allowed amount of memory these images may take. You can adapt this value 
 * during a recording session.
 **/
void
byzanz_recorder_set_max_cache (ByzanzRecorder *rec,
    guint max_cache_bytes)
{
  g_return_if_fail (BYZANZ_IS_RECORDER (rec));
  g_return_if_fail (max_cache_bytes > G_MAXINT);

  rec->max_cache_size = max_cache_bytes;
  while (rec->cache_size > max_cache_bytes) {
    RecorderJob *job = g_async_queue_try_pop (rec->finished);
    if (!job)
      break;
    recorder_job_free (rec, job);
  }
}

/**
 * byzanz_recorder_get_max_cache:
 * @rec: a recording session
 *
 * Gets the maximum allowed cache size. See byzanz_recorder_set_max_cache()
 * for details.
 *
 * Returns: the maximum allowed cache size in bytes
 **/
guint
byzanz_recorder_get_max_cache (ByzanzRecorder *rec)
{
  g_return_val_if_fail (BYZANZ_IS_RECORDER (rec), 0);

  return rec->max_cache_size;
}

/**
 * byzanz_recorder_get_cache:
 * @rec: a recording session
 *
 * Determines the current amount of image cache used.
 *
 * Returns: current cache used in bytes
 **/
guint
byzanz_recorder_get_cache (ByzanzRecorder *rec)
{
  g_return_val_if_fail (BYZANZ_IS_RECORDER (rec), 0);
  
  return rec->cache_size;
}

/**
 * byzanz_recorder_is_active:
 * @recorder: ia recording session
 *
 * Checks if the recorder is currently running or - after being stopped - if 
 * the encoder is still actively processing cached data.
 * Note that byzanz_recorder_destroy() will block until all cached data has been 
 * processed, so it might take a long time.
 *
 * Returns: TRUE if the recording session is still active.
 **/
gboolean
byzanz_recorder_is_active (ByzanzRecorder *recorder)
{
  g_return_val_if_fail (BYZANZ_IS_RECORDER (recorder), 0);
  
  return g_atomic_int_get (&recorder->encoder_running) > 0;
}
