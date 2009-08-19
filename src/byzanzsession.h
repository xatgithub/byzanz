/* desktop session recorder
 * Copyright (C) 2005 Benjamin Otte <otte@gnome.org
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

#ifndef __HAVE_BYZANZ_RECORDER_H__
#define __HAVE_BYZANZ_RECORDER_H__

typedef struct _ByzanzSession ByzanzSession;
#define BYZANZ_IS_RECORDER(obj) ((obj) != NULL)

ByzanzSession * 	byzanz_session_new		(const gchar *		filename,
							 GdkWindow *		window,
							 GdkRectangle *		area,
							 gboolean		loop,
							 gboolean		record_cursor);
ByzanzSession * 	byzanz_session_new_fd		(gint			fd,
							 GdkWindow *		window,
							 GdkRectangle *		area,
							 gboolean		loop,
							 gboolean		record_cursor);
void			byzanz_session_prepare		(ByzanzSession *	session);
void			byzanz_session_start		(ByzanzSession *	session);
void			byzanz_session_stop		(ByzanzSession *	session);
void			byzanz_session_destroy		(ByzanzSession *	session);
gboolean		byzanz_session_is_active	(ByzanzSession *	session);
/* property functions */
void			byzanz_session_set_max_cache	(ByzanzSession *	session,
							 guint			max_cache_bytes);
guint			byzanz_session_get_max_cache	(ByzanzSession *       session);
guint			byzanz_session_get_cache	(ByzanzSession *       session);
					

#endif /* __HAVE_BYZANZ_RECORDER_H__ */
