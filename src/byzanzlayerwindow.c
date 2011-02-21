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

#include "byzanzlayerwindow.h"

#include <math.h>
#include <gdk/gdkx.h>

G_DEFINE_TYPE (ByzanzLayerWindow, byzanz_layer_window, BYZANZ_TYPE_LAYER)

static gboolean
byzanz_layer_window_event (ByzanzLayer * layer,
                           GdkXEvent *   gdkxevent)
{
  XDamageNotifyEvent *event = (XDamageNotifyEvent *) gdkxevent;
  ByzanzLayerWindow *wlayer = BYZANZ_LAYER_WINDOW (layer);

  if (event->type == layer->recorder->damage_event_base + XDamageNotify && 
      event->damage == wlayer->damage) {
    GdkRectangle rect;

    rect.x = event->area.x;
    rect.y = event->area.y;
    rect.width = event->area.width;
    rect.height = event->area.height;
    if (gdk_rectangle_intersect (&rect, &layer->recorder->area, &rect)) {
      gdk_region_union_with_rect (wlayer->invalid, &rect);
      byzanz_layer_invalidate (layer);
    }
    return TRUE;
  }

  return FALSE;
}

static XserverRegion
byzanz_server_region_from_gdk (Display *dpy, GdkRegion *source)
{
  XserverRegion dest;
  XRectangle *dest_rects;
  GdkRectangle *source_rects;
  int n_rectangles, i;

  gdk_region_get_rectangles (source, &source_rects, &n_rectangles);
  g_assert (n_rectangles);
  dest_rects = g_new (XRectangle, n_rectangles);
  for (i = 0; i < n_rectangles; i++) {
    dest_rects[i].x = source_rects[i].x;
    dest_rects[i].y = source_rects[i].y;
    dest_rects[i].width = source_rects[i].width;
    dest_rects[i].height = source_rects[i].height;
  }
  dest = XFixesCreateRegion (dpy, dest_rects, n_rectangles);
  g_free (dest_rects);
  g_free (source_rects);

  return dest;
}

static GdkRegion *
byzanz_layer_window_snapshot (ByzanzLayer *layer)
{
  Display *dpy = gdk_x11_drawable_get_xdisplay (layer->recorder->window);
  ByzanzLayerWindow *wlayer = BYZANZ_LAYER_WINDOW (layer);
  XserverRegion reg;
  GdkRegion *region;

  if (gdk_region_empty (wlayer->invalid))
    return NULL;

  reg = byzanz_server_region_from_gdk (dpy, wlayer->invalid);
  XDamageSubtract (dpy, wlayer->damage, reg, reg);
  XFixesDestroyRegion (dpy, reg);

  region = wlayer->invalid;
  wlayer->invalid = gdk_region_new ();
  return region;
}

#if CAIRO_VERSION < CAIRO_VERSION_ENCODE (1, 8, 8)
  /* This fix is for RHEL6 only */
static void
byzanz_layer_window_render (ByzanzLayer *layer,
                            cairo_t *    cr)
{
  cairo_t *tmp;

  byzanz_cairo_set_source_window (cr, layer->recorder->window, 0, 0);
  cairo_paint (cr);
}
#else
static void
byzanz_cairo_set_source_window (cairo_t *cr, GdkWindow *window, double x, double y)
{
#if CAIRO_VERSION < CAIRO_VERSION_ENCODE (1, 80, 10)
  /* This fix is for RHEL6 only */
  {
    static const cairo_user_data_key_t key;
    double x1, y1_, x2, y2;
    int w, h;
    GdkPixmap *pixmap;
    GdkGC *gc;
    
    cairo_clip_extents (cr, &x1, &y1_, &x2, &y2);
    gdk_drawable_get_size (window, &w, &h);
    
    x1 = floor (x1);
    y1_ = floor (y1_);
    x2 = floor (x2);
    y2 = floor (y2);
    x1 = MAX (x1, 0);
    y1_ = MAX (y1_, 0);
    x2 = MIN (x2, w);
    y2 = MIN (y2, h);

    pixmap = gdk_pixmap_new (window, x2 - x1, y2 - y1_, -1);
    gc = gdk_gc_new (pixmap);
    gdk_gc_set_subwindow (gc, GDK_INCLUDE_INFERIORS);

    gdk_draw_drawable (pixmap, gc, window,
                       x1, y1_,
                       0, 0,
                       x2 - x1, y2 - y1_);
    gdk_cairo_set_source_pixmap (cr, pixmap, x1, y1_);

    g_object_unref (gc);
    cairo_set_user_data (cr, &key, pixmap, g_object_unref);
  }
#else
  cairo_t *tmp;

  tmp = gdk_cairo_create (window);
  cairo_set_source_surface (cr, cairo_get_target (tmp), x, y);
  cairo_destroy (tmp);
#endif
}

static void
byzanz_layer_window_render (ByzanzLayer *layer,
                            cairo_t *    cr)
{
  byzanz_cairo_set_source_window (cr, layer->recorder->window, 0, 0);
  cairo_paint (cr);
}
#endif

static void
byzanz_layer_window_finalize (GObject *object)
{
  Display *dpy = gdk_x11_drawable_get_xdisplay (BYZANZ_LAYER (object)->recorder->window);
  ByzanzLayerWindow *wlayer = BYZANZ_LAYER_WINDOW (object);

  XDamageDestroy (dpy, wlayer->damage);
  gdk_region_destroy (wlayer->invalid);

  G_OBJECT_CLASS (byzanz_layer_window_parent_class)->finalize (object);
}

static void
byzanz_layer_window_constructed (GObject *object)
{
  ByzanzLayer *layer = BYZANZ_LAYER (object);
  GdkWindow *window = layer->recorder->window;
  Display *dpy = gdk_x11_drawable_get_xdisplay (window);
  ByzanzLayerWindow *wlayer = BYZANZ_LAYER_WINDOW (object);

  wlayer->damage = XDamageCreate (dpy, gdk_x11_drawable_get_xid (window), XDamageReportDeltaRectangles);
  gdk_region_union_with_rect (wlayer->invalid, &layer->recorder->area);

  G_OBJECT_CLASS (byzanz_layer_window_parent_class)->constructed (object);
}

static void
byzanz_layer_window_class_init (ByzanzLayerWindowClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);
  ByzanzLayerClass *layer_class = BYZANZ_LAYER_CLASS (klass);

  object_class->finalize = byzanz_layer_window_finalize;
  object_class->constructed = byzanz_layer_window_constructed;

  layer_class->event = byzanz_layer_window_event;
  layer_class->snapshot = byzanz_layer_window_snapshot;
  layer_class->render = byzanz_layer_window_render;
}

static void
byzanz_layer_window_init (ByzanzLayerWindow *wlayer)
{
  wlayer->invalid = gdk_region_new ();
}

