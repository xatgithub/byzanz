/*
 * Copyright (C) 2005 by Benjamin Otte <otte@gnome.org>
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 3 of the License, or
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


#ifndef __PANEL_DROPDOWN_H__
#define __PANEL_DROPDOWN_H__

#include <panel-applet.h>
#include <gtk/gtktogglebutton.h>

G_BEGIN_DECLS


#define PANEL_TYPE_DROPDOWN            (panel_dropdown_get_type ())
#define PANEL_DROPDOWN(obj)            (G_TYPE_CHECK_INSTANCE_CAST ((obj), PANEL_TYPE_DROPDOWN, PanelDropdown))
#define PANEL_DROPDOWN_CLASS(klass)    (G_TYPE_CHECK_CLASS_CAST ((klass), PANEL_TYPE_DROPDOWN, PanelDropdownClass))
#define PANEL_IS_DROPDOWN(obj)         (G_TYPE_CHECK_INSTANCE_TYPE ((obj), PANEL_TYPE_DROPDOWN))
#define PANEL_IS_DROPDOWN_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE ((klass), PANEL_TYPE_DROPDOWN))
#define PANEL_DROPDOWN_GET_CLASS(obj)  (G_TYPE_INSTANCE_GET_CLASS ((obj), PANEL_TYPE_DROPDOWN, PanelDropdownClass))

typedef struct _PanelDropdown		    PanelDropdown;
typedef struct _PanelDropdownClass	    PanelDropdownClass;

struct _PanelDropdown
{
  GtkBin		bin;

  GtkWidget *		box;
  GtkWidget *		popup;
  GtkWidget *		arrow_button;
  GtkWidget *		arrow;
  GtkWidget *		child;
  /* monitoring how to display */
  PanelApplet *		applet;
  PanelAppletOrient	orient;		/* orientation advertised by panel */
  PanelAppletOrient	layout_orient;	/* orientation we layouted with */
  gboolean		small_layout;	/* if the layout is rotated */
};

struct _PanelDropdownClass
{
  GtkBinClass		bin_class;
};

GType		panel_dropdown_get_type		(void) G_GNUC_CONST;

GtkWidget *	panel_dropdown_new		(void);

void		panel_dropdown_set_popup_widget	(PanelDropdown *	dropdown,
						 GtkWidget *		widget);
void		panel_dropdown_set_applet	(PanelDropdown *        dropdown,
						 PanelApplet *		applet);


G_END_DECLS

#endif /* __PANEL_DROPDOWN_H__ */
