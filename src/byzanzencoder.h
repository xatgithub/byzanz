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

#include <glib-object.h>
#include <gio/gio.h>
#include <gdk/gdk.h>
#include <cairo.h>

#ifndef __HAVE_BYZANZ_ENCODER_H__
#define __HAVE_BYZANZ_ENCODER_H__

typedef struct _ByzanzEncoder ByzanzEncoder;
typedef struct _ByzanzEncoderClass ByzanzEncoderClass;

#define BYZANZ_TYPE_ENCODER                    (byzanz_encoder_get_type())
#define BYZANZ_IS_ENCODER(obj)                 (G_TYPE_CHECK_INSTANCE_TYPE ((obj), BYZANZ_TYPE_ENCODER))
#define BYZANZ_IS_ENCODER_CLASS(klass)         (G_TYPE_CHECK_CLASS_TYPE ((klass), BYZANZ_TYPE_ENCODER))
#define BYZANZ_ENCODER(obj)                    (G_TYPE_CHECK_INSTANCE_CAST ((obj), BYZANZ_TYPE_ENCODER, ByzanzEncoder))
#define BYZANZ_ENCODER_CLASS(klass)            (G_TYPE_CHECK_CLASS_CAST ((klass), BYZANZ_TYPE_ENCODER, ByzanzEncoderClass))
#define BYZANZ_ENCODER_GET_CLASS(obj)          (G_TYPE_INSTANCE_GET_CLASS ((obj), BYZANZ_TYPE_ENCODER, ByzanzEncoderClass))

struct _ByzanzEncoder {
  GObject		object;
  
  /*<private >*/
  GOutputStream *       output_stream;          /* stream we write to (passed to the vfuncs) */
  GCancellable *        cancellable;            /* cancellable to use in thread */
  GError *              error;                  /* NULL or the encoding error */
  guint                 width;                  /* width of image */
  guint                 height;                 /* height of image */

  GAsyncQueue *         jobs;                   /* the stuff we still need to encode */
  GThread *             thread;                 /* the encoding thread */
};

struct _ByzanzEncoderClass {
  GObjectClass		object_class;

  gboolean		(* setup)		(ByzanzEncoder *	encoder,
						 GOutputStream *	stream,
                                                 guint                  width,
                                                 guint                  height,
                                                 GCancellable *         cancellable,
						 GError **		error);
  gboolean		(* process)		(ByzanzEncoder *	encoder,
						 GOutputStream *	stream,
						 cairo_surface_t *	surface,
						 const GdkRegion *	region,
						 const GTimeVal *	total_elapsed,
                                                 GCancellable *         cancellable,
						 GError **		error);
  gboolean		(* close)		(ByzanzEncoder *	encoder,
						 GOutputStream *	stream,
						 const GTimeVal *	total_elapsed,
                                                 GCancellable *         cancellable,
						 GError **		error);
};

GType		byzanz_encoder_get_type		(void) G_GNUC_CONST;

ByzanzEncoder *	byzanz_encoder_new		(GOutputStream *        stream,
                                                 guint                  width,
                                                 guint                  height,
                                                 GCancellable *         cancellable);
void		byzanz_encoder_process		(ByzanzEncoder *	encoder,
						 cairo_surface_t *	surface,
						 const GdkRegion *	region,
						 const GTimeVal *	total_elapsed);
void		byzanz_encoder_close		(ByzanzEncoder *	encoder,
						 const GTimeVal *	total_elapsed);

gboolean        byzanz_encoder_is_running       (ByzanzEncoder *        encoder);
const GError *  byzanz_encoder_get_error        (ByzanzEncoder *        encoder);


#endif /* __HAVE_BYZANZ_ENCODER_H__ */
