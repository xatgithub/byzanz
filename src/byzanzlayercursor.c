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

#include "byzanzlayercursor.h"

#include <gdk/gdkx.h>

G_DEFINE_TYPE (ByzanzLayerCursor, byzanz_layer_cursor, BYZANZ_TYPE_LAYER)

static void
byzanz_layer_cursor_read_cursor (ByzanzLayerCursor *clayer)
{
  Display *dpy = gdk_x11_drawable_get_xdisplay (BYZANZ_LAYER (clayer)->recorder->window);

  clayer->cursor_next = XFixesGetCursorImage (dpy);
  if (clayer->cursor_next)
    g_hash_table_insert (clayer->cursors, clayer->cursor_next, clayer->cursor_next);
}

static gboolean
byzanz_layer_cursor_event (ByzanzLayer * layer,
                           GdkXEvent *   gdkxevent)
{
  ByzanzLayerCursor *clayer = BYZANZ_LAYER_CURSOR (layer);
  XFixesCursorNotifyEvent *event = gdkxevent;

  if (event->type == layer->recorder->fixes_event_base + XFixesCursorNotify) {
    XFixesCursorImage hack;

    hack.cursor_serial = event->cursor_serial;
    clayer->cursor_next = g_hash_table_lookup (clayer->cursors, &hack);
    if (clayer->cursor_next == NULL)
      byzanz_layer_cursor_read_cursor (clayer);
    if (clayer->cursor_next != clayer->cursor)
      byzanz_layer_invalidate (layer);
    return TRUE;
  }

  return FALSE;
}

static gboolean
byzanz_layer_cursor_poll (gpointer data)
{
  ByzanzLayerCursor *clayer = data;
  int x, y;
  
  gdk_window_get_pointer (BYZANZ_LAYER (clayer)->recorder->window, &x, &y, NULL);
  if (x == clayer->cursor_x &&
      y == clayer->cursor_y)
    return TRUE;

  clayer->poll_source = 0;
  byzanz_layer_invalidate (BYZANZ_LAYER (clayer));
  return FALSE;
}

static void
byzanz_layer_cursor_setup_poll (ByzanzLayerCursor *clayer)
{
  if (clayer->poll_source != 0)
    return;

  /* FIXME: Is 10ms ok or is it too much? */
  clayer->poll_source = g_timeout_add (10, byzanz_layer_cursor_poll, clayer);
}

static void
byzanz_recorder_invalidate_cursor (GdkRegion *region, XFixesCursorImage *cursor, int x, int y)
{
  GdkRectangle cursor_rect;

  if (cursor == NULL)
    return;

  cursor_rect.x = x - cursor->xhot;
  cursor_rect.y = y - cursor->yhot;
  cursor_rect.width = cursor->width;
  cursor_rect.height = cursor->height;

  gdk_region_union_with_rect (region, &cursor_rect);
}

static GdkRegion *
byzanz_layer_cursor_snapshot (ByzanzLayer *layer)
{
  ByzanzLayerCursor *clayer = BYZANZ_LAYER_CURSOR (layer);
  GdkRegion *region, *area;
  int x, y;
  
  gdk_window_get_pointer (layer->recorder->window, &x, &y, NULL);
  if (x == clayer->cursor_x &&
      y == clayer->cursor_y &&
      clayer->cursor_next == clayer->cursor)
    return NULL;

  region = gdk_region_new ();
  byzanz_recorder_invalidate_cursor (region, clayer->cursor, clayer->cursor_x, clayer->cursor_y);
  byzanz_recorder_invalidate_cursor (region, clayer->cursor_next, x, y);
  area = gdk_region_rectangle (&layer->recorder->area);
  gdk_region_intersect (region, area);
  gdk_region_destroy (area);

  clayer->cursor = clayer->cursor_next;
  clayer->cursor_x = x;
  clayer->cursor_y = y;
  byzanz_layer_cursor_setup_poll (clayer);

  return region;
}

static void
byzanz_layer_cursor_render (ByzanzLayer *layer,
                            cairo_t *    cr)
{
  ByzanzLayerCursor *clayer = BYZANZ_LAYER_CURSOR (layer);
  XFixesCursorImage *cursor = clayer->cursor;
  cairo_surface_t *cursor_surface;

  if (clayer->cursor == NULL)
    return;

  cursor_surface = cairo_image_surface_create_for_data ((guchar *) cursor->pixels,
      CAIRO_FORMAT_ARGB32, cursor->width * sizeof (unsigned long) / 4, cursor->height,
      cursor->width * sizeof (unsigned long));
  
  cairo_save (cr);

  cairo_translate (cr, clayer->cursor_x - cursor->xhot, clayer->cursor_y - cursor->yhot);

  /* This is neeed to map an unsigned long array to a uint32_t array */
  cairo_scale (cr, 4.0 / sizeof (unsigned long), 1);
#if G_BYTE_ORDER == G_BIG_ENDIAN
  cairo_translate (cr, (4.0 - sizeof (unsigned long)) / sizeof (unsigned long), 0);
#endif

  cairo_set_source_surface (cr, cursor_surface, 0, 0);
  /* Next line is also neeed for mapping the unsigned long array to a uint32_t array */
  cairo_pattern_set_filter (cairo_get_source (cr), CAIRO_FILTER_NEAREST);
  cairo_paint (cr);
  cairo_restore (cr);

  cairo_surface_destroy (cursor_surface);
}

static void
byzanz_layer_cursor_finalize (GObject *object)
{
  ByzanzLayerCursor *clayer = BYZANZ_LAYER_CURSOR (object);
  GdkWindow *window = BYZANZ_LAYER (object)->recorder->window;
  Display *dpy = gdk_x11_drawable_get_xdisplay (window);

  XFixesSelectCursorInput (dpy, gdk_x11_drawable_get_xid (window), 0);

  g_hash_table_destroy (clayer->cursors);

  if (clayer->poll_source != 0) {
    g_source_remove (clayer->poll_source);
  }

  G_OBJECT_CLASS (byzanz_layer_cursor_parent_class)->finalize (object);
}

static void
byzanz_layer_cursor_constructed (GObject *object)
{
  ByzanzLayerCursor *clayer = BYZANZ_LAYER_CURSOR (object);
  GdkWindow *window = BYZANZ_LAYER (object)->recorder->window;
  Display *dpy = gdk_x11_drawable_get_xdisplay (window);

  XFixesSelectCursorInput (dpy, gdk_x11_drawable_get_xid (window), XFixesDisplayCursorNotifyMask);
  byzanz_layer_cursor_read_cursor (clayer);
  byzanz_layer_cursor_setup_poll (clayer);

  G_OBJECT_CLASS (byzanz_layer_cursor_parent_class)->constructed (object);
}

static void
byzanz_layer_cursor_class_init (ByzanzLayerCursorClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);
  ByzanzLayerClass *layer_class = BYZANZ_LAYER_CLASS (klass);

  object_class->constructed = byzanz_layer_cursor_constructed;
  object_class->finalize = byzanz_layer_cursor_finalize;

  layer_class->event = byzanz_layer_cursor_event;
  layer_class->snapshot = byzanz_layer_cursor_snapshot;
  layer_class->render = byzanz_layer_cursor_render;
}

static guint
byzanz_cursor_hash (gconstpointer key)
{
  return (guint) ((const XFixesCursorImage *) key)->cursor_serial;
}

static gboolean
byzanz_cursor_equal (gconstpointer c1, gconstpointer c2)
{
  return ((const XFixesCursorImage *) c1)->cursor_serial == 
    ((const XFixesCursorImage *) c2)->cursor_serial;
}

static void
byzanz_layer_cursor_init (ByzanzLayerCursor *clayer)
{
  clayer->cursors = g_hash_table_new_full (byzanz_cursor_hash,
      byzanz_cursor_equal, NULL, (GDestroyNotify) XFree);
}

