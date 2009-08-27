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

#include "byzanzencoder.h"

typedef struct _ByzanzEncoderJob ByzanzEncoderJob;
struct _ByzanzEncoderJob {
  GTimeVal		tv;		/* time this job was enqueued */
  cairo_surface_t *	surface;	/* image to process */
  GdkRegion *		region;		/* relevant region of image */
};

static void
byzanz_encoder_job_free (ByzanzEncoderJob *job)
{
  if (job->surface)
    cairo_surface_destroy (job->surface);
  if (job->region)
    gdk_region_destroy (job->region);

  g_slice_free (ByzanzEncoderJob, job);
}

static gboolean
byzanz_encoder_finished (gpointer data)
{
  ByzanzEncoder *encoder = data;
  ByzanzEncoderJob *job;

  encoder->error = g_thread_join (encoder->thread);
  encoder->thread = NULL;

  while ((job = g_async_queue_try_pop (encoder->jobs)))
    byzanz_encoder_job_free (job);

  g_object_freeze_notify (G_OBJECT (encoder));
  g_object_notify (G_OBJECT (encoder), "running");
  if (encoder->error)
    g_object_notify (G_OBJECT (encoder), "error");
  g_object_thaw_notify (G_OBJECT (encoder));

  return FALSE;
}

/*** INSIDE THREAD ***/

static gpointer
byzanz_encoder_run (gpointer enc)
{
  ByzanzEncoder *encoder = enc;
  ByzanzEncoderClass *klass = BYZANZ_ENCODER_GET_CLASS (enc);
  GError *error = NULL;

  if (!klass->setup (encoder, encoder->output_stream, encoder->width, encoder->height,
        encoder->cancellable, &error))
    return error;

  do {
    ByzanzEncoderJob *job = g_async_queue_pop (encoder->jobs);

    /* quit */
    if (job->surface == NULL) {
      if (klass->close (encoder, encoder->output_stream, &job->tv,
              encoder->cancellable, &error))
        g_output_stream_close (encoder->output_stream, encoder->cancellable, &error);
      byzanz_encoder_job_free (job);
      break;
    }

    /* decode */
    klass->process (encoder, encoder->output_stream,
          job->surface, job->region, &job->tv,
          encoder->cancellable, &error);
    byzanz_encoder_job_free (job);
  } while (error == NULL);


  g_idle_add_full (G_PRIORITY_DEFAULT, byzanz_encoder_finished, enc, NULL);
  return error;
}

/*** OUTSIDE THREAD ***/

enum {
  PROP_0,
  PROP_OUTPUT,
  PROP_CANCELLABLE,
  PROP_WIDTH,
  PROP_HEIGHT,
  PROP_ERROR,
  PROP_RUNNING
};

static void
byzanz_encoder_base_init (gpointer klass)
{
  ByzanzEncoderClass *encoder_class = klass;

  encoder_class->filter = NULL;
}

static void
byzanz_encoder_base_finalize (gpointer klass)
{
  ByzanzEncoderClass *encoder_class = klass;

  if (encoder_class->filter)
    g_object_unref (encoder_class->filter);
}

/* cannot use this here, the file filter requires base_init and base_finalize
 * G_DEFINE_ABSTRACT_TYPE (ByzanzEncoder, byzanz_encoder, G_TYPE_OBJECT)
 */
static void     byzanz_encoder_init       (GTypeInstance *instance, gpointer klass);
static void     byzanz_encoder_class_init (ByzanzEncoderClass *klass);
static gpointer byzanz_encoder_parent_class = NULL;
static void     byzanz_encoder_class_intern_init (gpointer klass, gpointer data)
{
  byzanz_encoder_parent_class = g_type_class_peek_parent (klass);
  byzanz_encoder_class_init ((ByzanzEncoderClass*) klass);
}

GType
byzanz_encoder_get_type (void)
{
  static volatile gsize g_define_type_id__volatile = 0;
  if (g_once_init_enter (&g_define_type_id__volatile)) {
    GTypeInfo info = {
      sizeof (ByzanzEncoderClass),
      byzanz_encoder_base_init,
      byzanz_encoder_base_finalize,
      byzanz_encoder_class_intern_init,
      NULL,
      NULL,
      sizeof (ByzanzEncoder),
      0,
      byzanz_encoder_init,
      NULL
    };
    GType g_define_type_id;

    g_define_type_id = g_type_register_static (G_TYPE_OBJECT, g_intern_static_string ("ByzanzEncoder"),
      &info, G_TYPE_FLAG_ABSTRACT);
    g_once_init_leave (&g_define_type_id__volatile, g_define_type_id);
  }
  return g_define_type_id__volatile;
}

static void
byzanz_encoder_get_property (GObject *object, guint param_id, GValue *value, 
    GParamSpec * pspec)
{
  ByzanzEncoder *encoder = BYZANZ_ENCODER (object);

  switch (param_id) {
    case PROP_OUTPUT:
      g_value_set_object (value, encoder->output_stream);
      break;
    case PROP_CANCELLABLE:
      g_value_set_object (value, encoder->cancellable);
      break;
    case PROP_ERROR:
      g_value_set_pointer (value, encoder->error);
      break;
    case PROP_RUNNING:
      g_value_set_boolean (value, encoder->thread != NULL);
      break;
    case PROP_WIDTH:
      g_value_set_uint (value, encoder->width);
      break;
    case PROP_HEIGHT:
      g_value_set_uint (value, encoder->height);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, param_id, pspec);
      break;
  }
}

static void
byzanz_encoder_set_property (GObject *object, guint param_id, const GValue *value, 
    GParamSpec * pspec)
{
  ByzanzEncoder *encoder = BYZANZ_ENCODER (object);

  switch (param_id) {
    case PROP_OUTPUT:
      encoder->output_stream = g_value_dup_object (value);
      g_assert (encoder->output_stream != NULL);
      break;
    case PROP_CANCELLABLE:
      encoder->cancellable = g_value_dup_object (value);
      break;
    case PROP_WIDTH:
      encoder->width = g_value_get_uint (value);
      break;
    case PROP_HEIGHT:
      encoder->height = g_value_get_uint (value);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, param_id, pspec);
      break;
  }
}

static void
byzanz_encoder_finalize (GObject *object)
{
  ByzanzEncoder *encoder = BYZANZ_ENCODER (object);

  /* FIXME: handle the case where the thread is still alive */
  g_assert (encoder->thread == NULL);

  g_object_unref (encoder->output_stream);
  if (encoder->cancellable)
    g_object_unref (encoder->cancellable);
  if (encoder->error)
    g_error_free (encoder->error);

  g_async_queue_unref (encoder->jobs);

  G_OBJECT_CLASS (byzanz_encoder_parent_class)->finalize (object);
}

static void
byzanz_encoder_constructed (GObject *object)
{
  ByzanzEncoder *encoder = BYZANZ_ENCODER (object);

  encoder->thread = g_thread_create (byzanz_encoder_run, encoder, 
      TRUE, &encoder->error);

  if (G_OBJECT_CLASS (byzanz_encoder_parent_class)->constructed)
    G_OBJECT_CLASS (byzanz_encoder_parent_class)->constructed (object);
}

static void
byzanz_encoder_class_init (ByzanzEncoderClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);

  object_class->get_property = byzanz_encoder_get_property;
  object_class->set_property = byzanz_encoder_set_property;
  object_class->finalize = byzanz_encoder_finalize;
  object_class->constructed = byzanz_encoder_constructed;

  g_object_class_install_property (object_class, PROP_OUTPUT,
      g_param_spec_object ("output", "output", "stream to write data to",
	  G_TYPE_OUTPUT_STREAM, G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY));
  g_object_class_install_property (object_class, PROP_CANCELLABLE,
      g_param_spec_object ("cancellable", "cancellable", "cancellable for stopping the thread",
	  G_TYPE_CANCELLABLE, G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY));
  g_object_class_install_property (object_class, PROP_ERROR,
      g_param_spec_pointer ("error", "error", "error that happened on the thread",
	  G_PARAM_READABLE));
  g_object_class_install_property (object_class, PROP_RUNNING,
      g_param_spec_boolean ("running", "running", "TRUE while the encoding thread is running",
	  TRUE, G_PARAM_READABLE));
  g_object_class_install_property (object_class, PROP_WIDTH,
      g_param_spec_uint ("width", "width", "width of the stream that gets encoded",
	  1, G_MAXUINT, 1, G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY));
  g_object_class_install_property (object_class, PROP_HEIGHT,
      g_param_spec_uint ("height", "height", "height of the stream that gets encoded",
	  1, G_MAXUINT, 1, G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY));
}

static void
byzanz_encoder_init (GTypeInstance *instance, gpointer klass)
{
  ByzanzEncoder *encoder = BYZANZ_ENCODER (instance);

  encoder->jobs = g_async_queue_new ();
}

ByzanzEncoder *
byzanz_encoder_new (GType           encoder_type,
                    GOutputStream * stream,
                    guint           width,
                    guint           height,
                    GCancellable *  cancellable)
{
  ByzanzEncoder *encoder;

  g_return_val_if_fail (g_type_is_a (encoder_type, BYZANZ_TYPE_ENCODER), NULL);
  g_return_val_if_fail (G_IS_OUTPUT_STREAM (stream), NULL);
  g_return_val_if_fail (cancellable == NULL || G_IS_CANCELLABLE (cancellable), NULL);
  g_return_val_if_fail (width > 0, NULL);
  g_return_val_if_fail (height > 0, NULL);

  encoder = g_object_new (encoder_type, "output", stream,
      "cancellable", cancellable, "width", width, "height", height, NULL);

  return encoder;
}

void
byzanz_encoder_process (ByzanzEncoder *	 encoder,
		        cairo_surface_t *surface,
			const GdkRegion *region,
			const GTimeVal * total_elapsed)
{
  ByzanzEncoderJob *job;

  g_return_if_fail (BYZANZ_IS_ENCODER (encoder));
  g_return_if_fail (surface != NULL);
  g_return_if_fail (region != NULL);
  g_return_if_fail (total_elapsed != NULL);

  if (encoder->error)
    return;

  job = g_slice_new (ByzanzEncoderJob);
  job->surface = cairo_surface_reference (surface);
  job->region = gdk_region_copy (region);
  job->tv = *total_elapsed;

  g_async_queue_push (encoder->jobs, job);
}

void
byzanz_encoder_close (ByzanzEncoder *encoder,
		      const GTimeVal * total_elapsed)
{
  ByzanzEncoderJob *job;

  g_return_if_fail (BYZANZ_IS_ENCODER (encoder));
  g_return_if_fail (total_elapsed != NULL);

  if (encoder->error)
    return;

  job = g_slice_new (ByzanzEncoderJob);
  job->surface = NULL;
  job->region = NULL;
  job->tv = *total_elapsed;

  g_async_queue_push (encoder->jobs, job);
}

gboolean
byzanz_encoder_is_running (ByzanzEncoder *encoder)
{
  g_return_val_if_fail (BYZANZ_IS_ENCODER (encoder), FALSE);

  return encoder->thread != NULL;
}

const GError *
byzanz_encoder_get_error (ByzanzEncoder *encoder)
{
  g_return_val_if_fail (BYZANZ_IS_ENCODER (encoder), FALSE);

  return encoder->error;
}

GtkFileFilter *
byzanz_encoder_type_get_filter (GType encoder_type)
{
  ByzanzEncoderClass *klass;
  GtkFileFilter *filter;

  g_return_val_if_fail (g_type_is_a (encoder_type, BYZANZ_TYPE_ENCODER), NULL);

  klass = g_type_class_ref (encoder_type);
  filter = klass->filter;
  if (filter) {
    g_assert (!g_object_is_floating (filter));
    g_object_ref (filter);
    g_object_set_data (G_OBJECT (filter), "byzanz-encoder-type",
        GSIZE_TO_POINTER (encoder_type));
  }

  g_type_class_unref (klass);
  return filter;
}

/* all the encoders */
#include "byzanzencodergif.h"

typedef GType (* TypeFunc) (void);
static const TypeFunc functions[] = {
  byzanz_encoder_gif_get_type
};
#define BYZANZ_ENCODER_DEFAULT_TYPE (functions[0] ())

GType
byzanz_encoder_type_iter_init (ByzanzEncoderIter *iter)
{
  g_return_val_if_fail (iter != NULL, G_TYPE_NONE);

  *iter = GSIZE_TO_POINTER (0);

  return functions[0] ();
}

GType
byzanz_encoder_type_iter_next (ByzanzEncoderIter *iter)
{
  guint id;

  g_return_val_if_fail (iter != NULL, G_TYPE_NONE);

  id = GPOINTER_TO_SIZE (*iter);

  id++;
  if (id >= G_N_ELEMENTS (functions))
    return G_TYPE_NONE;

  *iter = GSIZE_TO_POINTER (id);

  return functions[id] ();
}

GType
byzanz_encoder_get_type_from_filter (GtkFileFilter *filter)
{
  GType type;

  g_return_val_if_fail (filter == NULL || GTK_IS_FILE_FILTER (filter), BYZANZ_ENCODER_DEFAULT_TYPE);

  if (filter == NULL)
    return BYZANZ_ENCODER_DEFAULT_TYPE;

  type = GPOINTER_TO_SIZE (g_object_get_data (G_OBJECT (filter), "byzanz-encoder-type"));
  if (type == 0)
    return BYZANZ_ENCODER_DEFAULT_TYPE;

  return type;
}

GType
byzanz_encoder_get_type_from_file (GFile *file)
{
  ByzanzEncoderIter iter;
  GtkFileFilterInfo info;
  GType type;

  g_return_val_if_fail (G_IS_FILE (file), BYZANZ_ENCODER_DEFAULT_TYPE);

  info.contains = 0;
  
  info.filename = g_file_get_path (file);
  if (info.filename)
    info.contains |= GTK_FILE_FILTER_FILENAME;

  info.uri = g_file_get_uri (file);
  if (info.uri)
    info.contains |= GTK_FILE_FILTER_URI;

  /* uh oh */
  info.display_name = g_file_get_parse_name (file);
  if (info.display_name)
    info.contains |= GTK_FILE_FILTER_DISPLAY_NAME;

  for (type = byzanz_encoder_type_iter_init (&iter);
       type != G_TYPE_NONE;
       type = byzanz_encoder_type_iter_next (&iter)) {
    GtkFileFilter *filter = byzanz_encoder_type_get_filter (type);
    if (filter == NULL)
      continue;

    if (gtk_file_filter_filter (filter, &info)) {
      g_object_unref (filter);
      break;
    }
    
    g_object_unref (filter);
  }
  if (type == G_TYPE_NONE)
    type = BYZANZ_ENCODER_DEFAULT_TYPE;

  g_free ((char *) info.filename);
  g_free ((char *) info.uri);
  g_free ((char *) info.display_name);

  return type;
}

