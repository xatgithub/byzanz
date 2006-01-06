/*
 * Copyright (C) 2005 by Benjamin Otte <otte@gnome.org>
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 59 Temple Street #330, Boston, MA 02111-1307, USA.
 *
 $Id$
 */


#ifndef __PANEL_STUFFER_H__
#define __PANEL_STUFFER_H__

#include <gtk/gtkcontainer.h>

G_BEGIN_DECLS


#define PANEL_TYPE_STUFFER            (panel_stuffer_get_type ())
#define PANEL_STUFFER(obj)            (G_TYPE_CHECK_INSTANCE_CAST ((obj), PANEL_TYPE_STUFFER, PanelStuffer))
#define PANEL_STUFFER_CLASS(klass)    (G_TYPE_CHECK_CLASS_CAST ((klass), PANEL_TYPE_STUFFER, PanelStufferClass))
#define PANEL_IS_STUFFER(obj)         (G_TYPE_CHECK_INSTANCE_TYPE ((obj), PANEL_TYPE_STUFFER))
#define PANEL_IS_STUFFER_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE ((klass), PANEL_TYPE_STUFFER))
#define PANEL_STUFFER_GET_CLASS(obj)  (G_TYPE_INSTANCE_GET_CLASS ((obj), PANEL_TYPE_STUFFER, PanelStufferClass))

typedef struct _PanelStuffer	    PanelStuffer;
typedef struct _PanelStufferClass   PanelStufferClass;

struct _PanelStuffer
{
  GtkContainer container;

  /*< protected >*/
  GArray *children;
  GtkOrientation orient;	/* orientation of stuffer */
  gulong orient_signal;		/* connected signal to monitor orientation changes 
				   if PanelApplet descendant */
  gint space;			/* depending on orient, allowed width
				   or height to stuff into */
  gint spacing;			/* spacing of widgets */
};

struct _PanelStufferClass
{
  GtkContainerClass container_class;
};

GType		panel_stuffer_get_type	(void) G_GNUC_CONST;

GtkWidget *	panel_stuffer_new	(GtkOrientation		orient);
void		panel_stuffer_set_orientation
					(PanelStuffer *		stuffer,
					 GtkOrientation		orient);
void		panel_stuffer_add_full	(PanelStuffer *		stuffer,
					 GtkWidget *		widget,
					 gboolean		may_rotate,
					 gboolean		fill);

G_END_DECLS


#endif /* __PANEL_STUFFER_H__ */
