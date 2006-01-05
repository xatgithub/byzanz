/* simple gif encoder
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

#include <string.h>
#include <gtk/gtk.h>
#include <gdk/gdkx.h>
#include <X11/extensions/Xdamage.h>
#include "gifenc.h"
#include "i18n.h"

static int duration = 10;
static int delay = 0;
static gboolean loop = FALSE;
static GdkRectangle screen = { 0, 0, G_MAXINT / 2, G_MAXINT / 2 };
static GOptionEntry entries[] = 
{
  { "duration", 'd', 0, G_OPTION_ARG_INT, &duration, N_("Duration of animation"), N_("SECS") },
  { "delay", 0, 0, G_OPTION_ARG_INT, &delay, N_("Delay before start"), N_("SECS") },
  { "loop", 'l', 0, G_OPTION_ARG_NONE, &loop, N_("Let the animation loop"), NULL },
  { "x", 'x', 0, G_OPTION_ARG_INT, &screen.x, N_("X coordinate of rectangle to record"), N_("PIXEL") },
  { "y", 'y', 0, G_OPTION_ARG_INT, &screen.y, N_("Y coordinate of rectangle to record"), N_("PIXEL") },
  { "width", 'w', 0, G_OPTION_ARG_INT, &screen.width, N_("Width of recording rectangle"), N_("PIXEL") },
  { "height", 'h', 0, G_OPTION_ARG_INT, &screen.height, N_("Height of recording rectangle"), N_("PIXEL") },
  { NULL }
};
    
static void
dither_image (Gifenc *enc, guint8 *data, GdkImage *image, GdkRectangle *rect)
{
  g_return_if_fail (image->bpp == 3 || image->bpp == 4);
  
  gifenc_dither_rgb_into (data + screen.width * (rect->y - screen.y) 
	  + (rect->x - screen.x), screen.width,
	  enc->palette,
	  image->mem + (rect->y - screen.y) * image->bpl 
	  + (rect->x - screen.x) * image->bpp + 
	    (image->bpp == 4 && image->byte_order == GDK_MSB_FIRST ? 1 : 0),
	  rect->width, rect->height, image->bpp, image->bpl);
}

static void
usage (const char *prgname)
{
  g_print ("usage: %s [OPTIONS] filename\n", prgname);
  g_print ("       %s --help\n", prgname);
}

int
main (int argc, char **argv)
{
  Display *dpy;
  Damage damage;
  int event_base, error_base;
  GdkRegion *region;
  GdkRectangle rect;
  GdkImage *image;
  GdkRectangle image_rect;
  XEvent ev;
  XserverRegion fixesregion, part;
  GTimeVal start, last, now, end;
  Gifenc *enc;
  guint8 *data;
  GOptionContext *context;
  GError *error = NULL;
  
#ifdef GETTEXT_PACKAGE
  bindtextdomain (GETTEXT_PACKAGE, LOCALEDIR);
  bind_textdomain_codeset (GETTEXT_PACKAGE, "UTF-8");
  textdomain (GETTEXT_PACKAGE);
#endif

  context = g_option_context_new (_("record your current desktop session"));
  g_option_context_add_main_entries (context, entries, GETTEXT_PACKAGE);
  g_option_context_add_group (context, gtk_get_option_group (TRUE));
  if (!g_option_context_parse (context, &argc, &argv, &error)) {
    g_printerr ("Wrong option: %s\n", error->message);
    usage (argv[0]);
    return 1;
  }

  dpy = gdk_x11_display_get_xdisplay (gdk_display_get_default ());
  if (!XDamageQueryExtension (dpy, &event_base, &error_base))  {
    g_printerr ("no damage extension\n");
    return 1;
  }
  if (argc != 2) {
    usage (argv[0]);
    return 0;
  }
  damage = XDamageCreate (dpy, gdk_x11_get_default_root_xwindow (), XDamageReportDeltaRectangles);
  image_rect.x = image_rect.y = 0;
  gdk_drawable_get_size (gdk_get_default_root_window (),
      &image_rect.width, &image_rect.height);
  gdk_rectangle_intersect (&screen, &image_rect, &screen);
  image_rect = screen;
  region = gdk_region_new ();
  fixesregion = XFixesCreateRegion (dpy, 0, 0);
  part = XFixesCreateRegion (dpy, 0, 0);
  g_get_current_time (&start);
  g_time_val_add (&start, G_USEC_PER_SEC * delay);
  do {
    g_usleep (1000);
    g_get_current_time (&now);
  } while (now.tv_sec < start.tv_sec || 
	   (now.tv_sec == start.tv_sec && now.tv_usec < start.tv_usec));
  image = gdk_drawable_get_image (gdk_get_default_root_window (),
      image_rect.x, image_rect.y, image_rect.width, image_rect.height);
  enc = gifenc_open (image_rect.width, image_rect.height, 
      argv[1]);
#if 0
  gifenc_set_palette (enc, gifenc_palette_get_simple (
      (image->byte_order == GDK_MSB_FIRST) ? G_BIG_ENDIAN : G_LITTLE_ENDIAN, FALSE));
#else
  gifenc_set_palette (enc, gifenc_quantize_image (
      image->mem + (image->bpp == 4 && image->byte_order == GDK_MSB_FIRST ? 1 : 0),
      screen.width, screen.height, image->bpp, image->bpl, TRUE,
      (image->byte_order == GDK_MSB_FIRST) ? G_BIG_ENDIAN : G_LITTLE_ENDIAN, 
      255));
#endif
  if (loop)
    gifenc_set_looping (enc);
  data = g_malloc (screen.width * screen.height);
  last = end = start;
  dither_image (enc, data, image, &image_rect);

  g_time_val_add (&end, G_USEC_PER_SEC * duration);
  for (;;) {
    g_time_val_add (&start, G_USEC_PER_SEC / 25);
    do {
      g_usleep (1000);
      g_get_current_time (&now);
    } while (now.tv_sec < start.tv_sec || 
	     (now.tv_sec == start.tv_sec && now.tv_usec < start.tv_usec));
    if (end.tv_sec < now.tv_sec ||
	(end.tv_sec == now.tv_sec && end.tv_usec < now.tv_usec)) {
      break;
    }
    while (XPending (dpy)) {
      XNextEvent (dpy, &ev);
      if (ev.type == event_base + XDamageNotify) {
	XDamageNotifyEvent *dev = (XDamageNotifyEvent *) &ev;
	rect.x = dev->area.x;
	rect.y = dev->area.y;
	rect.width = dev->area.width;
	rect.height = dev->area.height;
	XFixesSetRegion (dpy, part, &dev->area, 1);
	XFixesUnionRegion (dpy, fixesregion, fixesregion, part);
	if (gdk_rectangle_intersect (&rect, &screen, &rect))
	  gdk_region_union_with_rect (region, &rect);
      }
    }
    XDamageSubtract (dpy, damage, fixesregion, None);
    XFixesSetRegion (dpy, fixesregion, 0, 0);
    XFlush (dpy);
    if (!gdk_region_empty (region)) {
      gifenc_add_image (enc, image_rect.x - screen.x, image_rect.y - screen.y, 
	  image_rect.width, image_rect.height, 
	  (now.tv_sec - last.tv_sec) * 1000 + (now.tv_usec - last.tv_usec) / 1000, 
	  data + screen.width * (image_rect.y - screen.y) + image_rect.x - screen.x,
	  screen.width);
#if 1
      {
	guint8 transparent = gifenc_palette_get_alpha_index (enc->palette);
	GdkRectangle *rects;
	GdkRegion *rev;
	int i, line, nrects;
	gdk_region_get_rectangles (region, &rects, &nrects);
	for (i = 0; i < nrects; i++) {
	  gdk_drawable_copy_to_image (gdk_get_default_root_window (), image, 
	      rects[i].x, rects[i].y, 
	      rects[i].x - screen.x, rects[i].y - screen.y, 
	      rects[i].width, rects[i].height);
	  dither_image (enc, data, image, &rects[i]);
	}
	g_free (rects);
	gdk_region_get_clipbox (region, &image_rect);
	rev = gdk_region_rectangle (&image_rect);
	gdk_region_subtract (rev, region);
	gdk_region_get_rectangles (rev, &rects, &nrects);
	for (i = 0; i < nrects; i++) {
	  for (line = 0; line < rects[i].height; line++) {
	    memset (data + rects[i].x - screen.x + 
		screen.width * (rects[i].y - screen.y + line), 
		transparent, rects[i].width);
	  }
	}
	g_free (rects);
      }
#else
      gdk_region_get_clipbox (region, &image_rect);
      g_assert (gdk_rectangle_intersect (&image_rect, &screen, &image_rect));
      gdk_drawable_copy_to_image (gdk_get_default_root_window (), image, 
	  image_rect.x, image_rect.y, 
	  image_rect.x - screen.x, image_rect.y - screen.y, 
	  image_rect.width, image_rect.height);
      dither_image (enc, data, image, &image_rect);
#endif
      last = now;
      gdk_region_destroy (region);
      region = gdk_region_new ();
    }
  }
  gifenc_add_image (enc, image_rect.x - screen.x, image_rect.y - screen.y, 
      image_rect.width, image_rect.height, 
      (now.tv_sec - last.tv_sec) * 1000 + (now.tv_usec - last.tv_usec) / 1000, 
      data + screen.width * (image_rect.y - screen.y) + image_rect.x - screen.x,
      screen.width);
  g_free (data);
  gifenc_close (enc);
  return 0;
}
