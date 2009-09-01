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

#include "byzanzserialize.h"

#include <string.h>
#include <glib/gi18n.h>

#define IDENTIFICATION "ByzanzRecording"

static guchar
byte_order_to_uchar (void)
{
  switch (G_BYTE_ORDER) {
    case G_BIG_ENDIAN:
      return 'B';
    case G_LITTLE_ENDIAN:
      return 'L';
    default:
      g_assert_not_reached ();
      return 0;
  }
}

gboolean
byzanz_serialize_header (GOutputStream * stream,
                         guint           width,
                         guint           height,
                         GCancellable *  cancellable,
                         GError **       error)
{
  guint32 w, h;
  guchar endian;

  g_return_val_if_fail (G_IS_OUTPUT_STREAM (stream), FALSE);
  g_return_val_if_fail (width <= G_MAXUINT32, FALSE);
  g_return_val_if_fail (height <= G_MAXUINT32, FALSE);

  w = width;
  h = height;
  endian = byte_order_to_uchar ();

  return g_output_stream_write_all (stream, IDENTIFICATION, strlen (IDENTIFICATION), NULL, cancellable, error) &&
    g_output_stream_write_all (stream, &endian, sizeof (guchar), NULL, cancellable, error) &&
    g_output_stream_write_all (stream, &w, sizeof (guint32), NULL, cancellable, error) &&
    g_output_stream_write_all (stream, &h, sizeof (guint32), NULL, cancellable, error);
}

gboolean
byzanz_deserialize_header (GInputStream * stream,
                           guint *        width,
                           guint *        height,
                           GCancellable * cancellable,
                           GError **      error)
{
  char result[strlen (IDENTIFICATION) + 1];
  guint32 size[2];
  guchar endian;

  g_return_val_if_fail (G_IS_INPUT_STREAM (stream), FALSE);
  g_return_val_if_fail (width != NULL, FALSE);
  g_return_val_if_fail (height != NULL, FALSE);

  if (!g_input_stream_read_all (stream, result, sizeof (result), NULL, cancellable, error))
    return FALSE;

  if (strncmp (result, IDENTIFICATION, strlen (IDENTIFICATION)) != 0) {
    g_set_error_literal (error, G_IO_ERROR, G_IO_ERROR_FAILED,
        _("Not a Byzanz recording"));
    return FALSE;
  }
  endian = result[strlen (IDENTIFICATION)];
  if (endian != byte_order_to_uchar ()) {
    g_set_error_literal (error, G_IO_ERROR, G_IO_ERROR_FAILED,
        _("Unsupported byte order"));
    return FALSE;
  }

  if (!g_input_stream_read_all (stream, &size, sizeof (size), NULL, cancellable, error))
    return FALSE;

  *width = size[0];
  *height = size[1];

  return TRUE;
} 

gboolean
byzanz_serialize (GOutputStream *   stream,
                  guint64           msecs,
                  cairo_surface_t * surface,
                  const GdkRegion * region,
                  GCancellable *    cancellable,
                  GError **         error)
{
  guint i, stride;
  GdkRectangle *rects, extents;
  guchar *data;
  guint32 n;
  int y, n_rects;

  g_return_val_if_fail (G_IS_OUTPUT_STREAM (stream), FALSE);
  g_return_val_if_fail ((surface == NULL) == (region == NULL), FALSE);
  g_return_val_if_fail (region == NULL || !gdk_region_empty (region), FALSE);

  if (!g_output_stream_write_all (stream, &msecs, sizeof (guint64), NULL, cancellable, error))
    return FALSE;

  if (surface == 0) {
    n = 0;
    return g_output_stream_write_all (stream, &n, sizeof (guint32), NULL, cancellable, error);
  }

  gdk_region_get_rectangles (region, &rects, &n_rects);
  n = n_rects;
  if (!g_output_stream_write_all (stream, &n, sizeof (guint32), NULL, cancellable, error))
    goto fail;
  for (i = 0; i < n; i++) {
    gint32 ints[4] = { rects[i].x, rects[i].y, rects[i].width, rects[i].height };

    g_assert (sizeof (ints) == 16);
    if (!g_output_stream_write_all (stream, ints, sizeof (ints), NULL, cancellable, error))
      goto fail;
  }

  stride = cairo_image_surface_get_stride (surface);
  gdk_region_get_clipbox (region, &extents);
  for (i = 0; i < n; i++) {
    data = cairo_image_surface_get_data (surface) 
      + stride * (rects[i].y - extents.y) 
      + sizeof (guint32) * (rects[i].x - extents.x);
    for (y = 0; y < rects[i].height; y++) {
      if (!g_output_stream_write_all (G_OUTPUT_STREAM (stream), data, 
            rects[i].width * sizeof (guint32), NULL, cancellable, error))
        goto fail;
      data += stride;
    }
  }

  g_free (rects);
  return TRUE;

fail:
  g_free (rects);
  return FALSE;
}

gboolean
byzanz_deserialize (GInputStream *     stream,
                    guint64 *          msecs_out,
                    cairo_surface_t ** surface_out,
                    GdkRegion **       region_out,
                    GCancellable *     cancellable,
                    GError **          error)
{
  guint i, stride;
  GdkRectangle extents, *rects;
  GdkRegion *region;
  cairo_surface_t *surface;
  guchar *data;
  guint32 n;
  int y;

  g_return_val_if_fail (G_IS_INPUT_STREAM (stream), FALSE);
  g_return_val_if_fail (msecs_out != NULL, FALSE);
  g_return_val_if_fail (surface_out != NULL, FALSE);
  g_return_val_if_fail (region_out != NULL, FALSE);

  if (!g_input_stream_read_all (stream, msecs_out, sizeof (guint64), NULL, cancellable, error) ||
      !g_input_stream_read_all (stream, &n, sizeof (guint32), NULL, cancellable, error))
    return FALSE;

  if (n == 0) {
    /* end of stream */
    *surface_out = NULL;
    *region_out = NULL;
    return TRUE;
  }

  region = gdk_region_new ();
  rects = g_new (GdkRectangle, n);
  surface = NULL;
  for (i = 0; i < n; i++) {
    gint ints[4];
    if (!g_input_stream_read_all (stream, ints, sizeof (ints), NULL, cancellable, error))
      goto fail;

    rects[i].x = ints[0];
    rects[i].y = ints[1];
    rects[i].width = ints[2];
    rects[i].height = ints[3];
    gdk_region_union_with_rect (region, &rects[i]);
  }

  gdk_region_get_clipbox (region, &extents);
  surface = cairo_image_surface_create (CAIRO_FORMAT_RGB24, extents.width, extents.height);
  cairo_surface_set_device_offset (surface, -extents.x, -extents.y);
  stride = cairo_image_surface_get_stride (surface);
  for (i = 0; i < n; i++) {
    data = cairo_image_surface_get_data (surface) 
      + stride * (rects[i].y - extents.y) 
      + sizeof (guint32) * (rects[i].x - extents.x);
    for (y = 0; y < rects[i].height; y++) {
      if (!g_input_stream_read_all (stream, data, 
            rects[i].width * sizeof (guint32), NULL, cancellable, error))
        goto fail;
      data += stride;
    }
  }

  g_free (rects);
  *region_out = region;
  *surface_out = surface;
  return TRUE;

fail:
  if (surface)
    cairo_surface_destroy (surface);
  gdk_region_destroy (region);
  g_free (rects);
  return FALSE;
}

