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

#include "byzanzencodergif.h"

#include <string.h>

#include "gifenc.h"

G_DEFINE_TYPE (ByzanzEncoderGif, byzanz_encoder_gif, BYZANZ_TYPE_ENCODER)

static gboolean
byzanz_encoder_write_data (gpointer       closure,
                           const guchar * data,
                           gsize          len,
                           GError **      error)
{
  ByzanzEncoder *encoder = closure;

  return g_output_stream_write_all (encoder->output_stream, data, len,
      NULL, encoder->cancellable, error);
}

static gboolean
byzanz_encoder_gif_setup (ByzanzEncoder * encoder,
                          GOutputStream * stream,
                          guint           width,
                          guint           height,
                          GCancellable *  cancellable,
                          GError **	  error)
{
  ByzanzEncoderGif *gif = BYZANZ_ENCODER_GIF (encoder);

  gif->gifenc = gifenc_new (width, height, byzanz_encoder_write_data, encoder, NULL);

  gif->image_data = g_malloc (width * height);
  gif->cached_data = g_malloc (width * height);
  gif->cached_tmp = g_malloc (width * height);
  return TRUE;
}

static void
byzanz_encoder_gif_quantize (ByzanzEncoderGif * gif,
                             cairo_surface_t *  surface)
{
  GifencPalette *palette;

  g_assert (!gif->has_quantized);

  palette = gifenc_quantize_image (cairo_image_surface_get_data (surface),
      cairo_image_surface_get_width (surface), cairo_image_surface_get_height (surface),
      cairo_image_surface_get_stride (surface), TRUE, 255);
  
  gifenc_initialize (gif->gifenc, palette, TRUE, NULL);
  memset (gif->image_data,
      gifenc_palette_get_alpha_index (palette),
      gifenc_get_width (gif->gifenc) * gifenc_get_height (gif->gifenc));

  gif->has_quantized = TRUE;
}

static void
byzanz_encoder_write_image (ByzanzEncoderGif *gif, const GTimeVal *tv)
{
  glong msecs;
  guint width;

  g_assert (gif->cached_data != NULL);
  g_assert (gif->cached_area.width > 0);
  g_assert (gif->cached_area.height > 0);

  width = gifenc_get_width (gif->gifenc);
  msecs = (tv->tv_sec - gif->cached_time.tv_sec) * 1000 + 
	  (tv->tv_usec - gif->cached_time.tv_usec) / 1000 + 5;
  if (msecs < 10)
    g_printerr ("<10 msecs for a frame, can this be?\n");
  msecs = MAX (msecs, 10);

  gifenc_add_image (gif->gifenc, gif->cached_area.x, gif->cached_area.y, 
	gif->cached_area.width, gif->cached_area.height, msecs,
	gif->cached_data + width * gif->cached_area.y + gif->cached_area.x,
        width, NULL);

  gif->cached_time = *tv;
}

static gboolean
byzanz_encoder_gif_encode_image (ByzanzEncoderGif * gif,
                                 cairo_surface_t *  surface,
                                 const GdkRegion *  region,
                                 GdkRectangle *     area_out)
{
  GdkRectangle extents, area, *rects;
  guint8 transparent;
  guint i, n_rects, stride, width;

  gdk_region_get_clipbox (region, &extents);
  transparent = gifenc_palette_get_alpha_index (gif->gifenc->palette);
  stride = cairo_image_surface_get_stride (surface);
  width = gifenc_get_width (gif->gifenc);

  /* clear area */
  /* FIXME: only do this in parts not captured by region */
  for (i = extents.y; i < (guint) (extents.y + extents.height); i++) {
    memset (gif->cached_tmp + width * i + extents.x, transparent, extents.width);
  }

  /* render changed parts */
  gdk_region_get_rectangles (region, &rects, (int *) &n_rects);
  memset (area_out, 0, sizeof (GdkRectangle));
  for (i = 0; i < n_rects; i++) {
    if (gifenc_dither_rgb_with_full_image (
          gif->cached_tmp + width * rects[i].y + rects[i].x, width,
	  gif->image_data + width * rects[i].y + rects[i].x, width, 
	  gif->gifenc->palette, 
          cairo_image_surface_get_data (surface) + (rects[i].x - extents.x) * 4
              + (rects[i].y - extents.y) * stride,
          rects[i].width, rects[i].height, stride, &area)) {
      area.x += rects[i].x;
      area.y += rects[i].y;
      if (area_out->width > 0 && area_out->height > 0)
        gdk_rectangle_union (area_out, &area, area_out);
      else
        *area_out = area;
    }
  }
  g_free (rects);

  return area_out->width > 0 && area_out->height > 0;
}

static void
byzanz_encoder_swap_image (ByzanzEncoderGif * gif,
                           GdkRectangle *     area)
{
  guint8 *swap;

  swap = gif->cached_data;
  gif->cached_data = gif->cached_tmp;
  gif->cached_tmp = swap;
  gif->cached_area = *area;
}

static gboolean
byzanz_encoder_gif_process (ByzanzEncoder *   encoder,
                            GOutputStream *   stream,
                            cairo_surface_t * surface,
                            const GdkRegion * region,
                            const GTimeVal *  total_elapsed,
                            GCancellable *    cancellable,
                            GError **	      error)
{
  ByzanzEncoderGif *gif = BYZANZ_ENCODER_GIF (encoder);
  GdkRectangle area;

  if (!gif->has_quantized) {
    byzanz_encoder_gif_quantize (gif, surface);
    gif->cached_time = *total_elapsed;
    if (!byzanz_encoder_gif_encode_image (gif, surface, region, &area)) {
      g_assert_not_reached ();
    }
    byzanz_encoder_swap_image (gif, &area);
  } else {
    if (byzanz_encoder_gif_encode_image (gif, surface, region, &area)) {
      byzanz_encoder_write_image (gif, total_elapsed);
      byzanz_encoder_swap_image (gif, &area);
    }
  }

  return TRUE;
}

static gboolean
byzanz_encoder_gif_close (ByzanzEncoder *  encoder,
                          GOutputStream *  stream,
                          const GTimeVal * total_elapsed,
                          GCancellable *   cancellable,
                          GError **	   error)
{
  ByzanzEncoderGif *gif = BYZANZ_ENCODER_GIF (encoder);

  if (!gif->has_quantized) {
    g_set_error_literal (error, G_IO_ERROR, G_IO_ERROR_FAILED, "No image to encode.");
    return FALSE;
  }

  byzanz_encoder_write_image (gif, total_elapsed);
  gifenc_close (gif->gifenc, NULL);
  return TRUE;
}

static void
byzanz_encoder_gif_finalize (GObject *object)
{
  ByzanzEncoderGif *gif = BYZANZ_ENCODER_GIF (object);

  g_free (gif->image_data);
  g_free (gif->cached_data);
  g_free (gif->cached_tmp);
  if (gif->gifenc)
    gifenc_free (gif->gifenc);

  G_OBJECT_CLASS (byzanz_encoder_gif_parent_class)->finalize (object);
}

static void
byzanz_encoder_gif_class_init (ByzanzEncoderGifClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);
  ByzanzEncoderClass *encoder_class = BYZANZ_ENCODER_CLASS (klass);

  object_class->finalize = byzanz_encoder_gif_finalize;

  encoder_class->setup = byzanz_encoder_gif_setup;
  encoder_class->process = byzanz_encoder_gif_process;
  encoder_class->close = byzanz_encoder_gif_close;
}

static void
byzanz_encoder_gif_init (ByzanzEncoderGif *encoder_gif)
{
}
