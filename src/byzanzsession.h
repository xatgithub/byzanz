/* desktop session recorder
 * Copyright (C) 2005,2009 Benjamin Otte <otte@gnome.org
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

#include <glib.h>
#include <gtk/gtk.h>

#include "byzanzrecorder.h"
#include "gifenc.h"

#ifndef __HAVE_BYZANZ_SESSION_H__
#define __HAVE_BYZANZ_SESSION_H__

typedef struct _ByzanzSession ByzanzSession;
typedef struct _ByzanzSessionClass ByzanzSessionClass;

#define BYZANZ_TYPE_SESSION                    (byzanz_session_get_type())
#define BYZANZ_IS_SESSION(obj)                 (G_TYPE_CHECK_INSTANCE_TYPE ((obj), BYZANZ_TYPE_SESSION))
#define BYZANZ_IS_SESSION_CLASS(klass)         (G_TYPE_CHECK_CLASS_TYPE ((klass), BYZANZ_TYPE_SESSION))
#define BYZANZ_SESSION(obj)                    (G_TYPE_CHECK_INSTANCE_CAST ((obj), BYZANZ_TYPE_SESSION, ByzanzSession))
#define BYZANZ_SESSION_CLASS(klass)            (G_TYPE_CHECK_CLASS_CAST ((klass), BYZANZ_TYPE_SESSION, ByzanzSessionClass))
#define BYZANZ_SESSION_GET_CLASS(obj)          (G_TYPE_INSTANCE_GET_CLASS ((obj), BYZANZ_TYPE_SESSION, ByzanzSessionClass))

struct _ByzanzSession {
  GObject		object;
  
  /* set by user - accessed ALSO by thread */
  gboolean		loop;		/* wether the resulting gif should loop */
  ByzanzRecorder *      recorder;       /* the recorder in use */
  GThread *		encoder;	/* encoding thread */
  GError *              error;          /* NULL or the recording error */
  GCancellable *        cancellable;    /* cancellable to use for aborting the session */
  /* accessed ALSO by thread */
  GAsyncQueue *		jobs;		/* jobs the encoding thread has to do */
  /* accessed ONLY by thread */
  GOutputStream *       stream;         /* stream we write to */
  Gifenc *		gifenc;		/* encoder used to encode the image */
  GTimeVal		current;	/* timestamp of last encoded picture */
  guint8 *		data;		/* data used to hold palettized data */
  guint8 *		data_full;    	/* palettized data of full image to compare additions to */
  GdkRectangle		relevant_data;	/* relevant area to encode */
};

struct _ByzanzSessionClass {
  GObjectClass		object_class;
};

GType		        byzanz_session_get_type		(void) G_GNUC_CONST;


ByzanzSession * 	byzanz_session_new		(GFile *                destination,
							 GdkWindow *		window,
							 GdkRectangle *		area,
							 gboolean		loop,
							 gboolean		record_cursor);
void			byzanz_session_start		(ByzanzSession *	session);
void			byzanz_session_stop		(ByzanzSession *	session);
void			byzanz_session_abort            (ByzanzSession *	session);

gboolean                byzanz_session_is_recording     (ByzanzSession *        session);
gboolean                byzanz_session_is_encoding      (ByzanzSession *        session);
const GError *          byzanz_session_get_error        (ByzanzSession *        session);
					

#endif /* __HAVE_BYZANZ_SESSION_H__ */
