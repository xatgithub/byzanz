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

#ifdef HAVE_CONFIG_H
#  include "config.h"
#endif
#include <gtk/gtk.h>
#include "paneldropdown.h"
#include "paneltogglebutton.h"

static GtkBinClass *parent_class = NULL;

static void
panel_dropdown_position_menu (GtkMenu *menu, gint *x_out, gint *y_out, 
    gboolean *push_in, gpointer data)
{
  gint x, y, screen_w, screen_h, window_x, window_y;
  GdkScreen *screen;
  GtkRequisition req;
  PanelDropdown *dropdown = PANEL_DROPDOWN (data);
  GtkWidget *widget = GTK_WIDGET (data);

  screen = gtk_widget_get_screen (widget);
  screen_w = gdk_screen_get_width (screen);
  screen_h = gdk_screen_get_height (screen);
  gdk_window_get_origin (widget->window, &window_x, &window_y);
  gtk_widget_size_request (GTK_WIDGET (menu), &req);
  /* figure out correct x position */
  switch (dropdown->orient) {
    case PANEL_APPLET_ORIENT_UP:
    case PANEL_APPLET_ORIENT_DOWN:
      x = window_x + widget->allocation.x;
      if (x + req.width > screen_w) {
	x = x + widget->allocation.width - req.width;
	if (x < 0)
	  x = 0;
      }
      break;
    case PANEL_APPLET_ORIENT_LEFT:
      x = window_x + widget->allocation.x + widget->allocation.width;
      break;
    case PANEL_APPLET_ORIENT_RIGHT:
      x = window_x - req.width;
      break;
    default:
      x = 0;
      g_assert_not_reached ();
      break;
  }
  /* now figure out y position (duplicated code!!!) */
  switch (dropdown->orient) {
    case PANEL_APPLET_ORIENT_LEFT:
    case PANEL_APPLET_ORIENT_RIGHT:
      x = window_y + widget->allocation.y;
      if (y + req.height > screen_h) {
	y = y + widget->allocation.height - req.height;
	if (y < 0)
	  y = 0;
      }
      break;
    case PANEL_APPLET_ORIENT_UP:
      y = window_y + widget->allocation.y + widget->allocation.height;
      break;
    case PANEL_APPLET_ORIENT_DOWN:
      y = window_y - req.height;
      break;
    default:
      y = 0;
      g_assert_not_reached ();
      break;
  }
  
  *x_out = x;
  *y_out = y;
  *push_in = TRUE;
}

static void
panel_dropdown_toggled_cb (GtkWidget *button, PanelDropdown *dropdown)
{
  gboolean active = gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (button));

  g_return_if_fail (dropdown->popup);

  if (active == GTK_WIDGET_VISIBLE (dropdown->popup))
    return;

  if (active) {
    /* FIXME: better way to know the button? */
    gtk_menu_popup (GTK_MENU (dropdown->popup), NULL, NULL, 
	panel_dropdown_position_menu, dropdown, 0, GDK_CURRENT_TIME);
  } else {
    gtk_menu_shell_deactivate (GTK_MENU_SHELL (dropdown->popup));
  }
}

static void
panel_dropdown_add (GtkContainer *container, GtkWidget *child)
{
  PanelDropdown *dropdown = PANEL_DROPDOWN (container);
  
  if (child == dropdown->box) {
    GTK_CONTAINER_CLASS (parent_class)->add (container, child);
  } else if (child == dropdown->arrow) {
    GtkWidget *button = panel_toggle_button_new ();
    gtk_container_add (GTK_CONTAINER (button), dropdown->arrow);
    gtk_widget_show (button);
    g_signal_connect (button, "toggled", 
	G_CALLBACK (panel_dropdown_toggled_cb), dropdown);
    gtk_box_pack_end (GTK_BOX (dropdown->box), button, FALSE, TRUE, 0);
  } else {
    if (dropdown->child != NULL) {
      g_warning ("Attempting to add a widget with type %s to a %s, "
		 "but as a GtkBin subclass a %s can only contain one widget at a time; "
		 "it already contains a widget of type %s",
		 g_type_name (G_OBJECT_TYPE (child)),
		 g_type_name (G_OBJECT_TYPE (dropdown)),
		 g_type_name (G_OBJECT_TYPE (dropdown)),
		 g_type_name (G_OBJECT_TYPE (dropdown->child)));
      return;
    }
    gtk_box_pack_start (GTK_BOX (dropdown->box), child, TRUE, TRUE, 0);
  }
}

static void
panel_dropdown_remove (GtkContainer *container, GtkWidget *child)
{
  PanelDropdown *dropdown = PANEL_DROPDOWN (container);
  
  if (child == dropdown->box) {
    GTK_CONTAINER_CLASS (parent_class)->remove (container, child);
  } else if (child == dropdown->arrow) {
    child = gtk_widget_get_parent (child);
    dropdown->arrow = NULL;
    gtk_container_remove (GTK_CONTAINER (dropdown->box), child);
  } else {
    g_return_if_fail (child == dropdown->child);
    dropdown->child = NULL;
    gtk_container_remove (GTK_CONTAINER (dropdown->box), child);
  }
}

static void 
panel_dropdown_size_request (GtkWidget *widget, 
    GtkRequisition *requisition)
{
  gtk_widget_size_request (GTK_BIN (widget)->child, requisition);
}

static void
panel_dropdown_size_allocate (GtkWidget *widget, GtkAllocation *allocation)
{
  widget->allocation = *allocation;

  gtk_widget_size_allocate (GTK_BIN (widget)->child, allocation);
}

static void
panel_dropdown_finalize (GObject *object)
{
  PanelDropdown *dropdown = PANEL_DROPDOWN (object);

  if (dropdown->arrow) {
    g_object_unref (dropdown->arrow);
    gtk_container_remove (GTK_CONTAINER (dropdown), dropdown->arrow);
  }
  gtk_container_remove (GTK_CONTAINER (dropdown), dropdown->box);
}

static void
panel_dropdown_class_init (PanelDropdownClass *class)
{
  GObjectClass *object_class = G_OBJECT_CLASS (class);
  GtkWidgetClass *widget_class = GTK_WIDGET_CLASS (class);
  GtkContainerClass *container_class = GTK_CONTAINER_CLASS (class);

  parent_class = g_type_class_peek_parent (class);

  object_class->finalize = panel_dropdown_finalize;

  widget_class->size_request = panel_dropdown_size_request;
  widget_class->size_allocate = panel_dropdown_size_allocate;

  container_class->add = panel_dropdown_add;
  container_class->remove = panel_dropdown_remove;
}

static void
panel_dropdown_init (PanelDropdown *dropdown)
{
  dropdown->orient = PANEL_APPLET_ORIENT_UP;
  dropdown->arrow = gtk_arrow_new (GTK_ARROW_DOWN, GTK_SHADOW_NONE);
  dropdown->box = gtk_hbox_new (FALSE, 0);
  g_object_ref (dropdown->arrow);
  gtk_widget_show (dropdown->box);
  gtk_container_add (GTK_CONTAINER (dropdown), dropdown->box);
  gtk_widget_show (dropdown->arrow);
  gtk_container_add (GTK_CONTAINER (dropdown), dropdown->arrow);
  panel_dropdown_set_popup_widget (dropdown, NULL);
}

GType
panel_dropdown_get_type (void)
{
  static GType dropdown_type = 0;

  if (!dropdown_type)
    {
      static const GTypeInfo dropdown_info =
      {
	sizeof (PanelDropdownClass),
	NULL,		/* base_init */
	NULL,		/* base_finalize */
	(GClassInitFunc) panel_dropdown_class_init,
	NULL,		/* class_finalize */
	NULL,		/* class_data */
	sizeof (PanelDropdown),
	0,		/* n_preallocs */
	(GInstanceInitFunc) panel_dropdown_init,
	NULL,		/* value_table */
      };

      dropdown_type = g_type_register_static (GTK_TYPE_BIN, "PanelDropdown", 
					 &dropdown_info, 0);
    }

  return dropdown_type;
}

GtkWidget *
panel_dropdown_new (void)
{
  return GTK_WIDGET (g_object_new (PANEL_TYPE_DROPDOWN, NULL));
}

static void
panel_dropdown_deactivate_menu_cb (GtkMenu *menu, PanelDropdown *dropdown)
{
  gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (
	gtk_widget_get_parent (dropdown->arrow)), FALSE);
}

void
panel_dropdown_set_popup_widget	(PanelDropdown *dropdown, GtkWidget *widget)
{
  g_return_if_fail (PANEL_IS_DROPDOWN (dropdown));
  /* FIXME: make this work with GtkWidget (by putting it in a window and showing that */
  g_return_if_fail (widget == NULL || GTK_IS_MENU (widget));

  if (dropdown->popup) {
    if (g_signal_handlers_disconnect_by_func (dropdown->popup, 
	panel_dropdown_deactivate_menu_cb, dropdown) != 1)
      g_assert_not_reached ();
    g_object_unref (dropdown->popup);
  }
  dropdown->popup = widget;
  if (dropdown->popup) {
    g_object_ref (dropdown->popup);
    gtk_object_sink (GTK_OBJECT (dropdown->popup));
    g_signal_connect (dropdown->popup, "deactivate", 
	G_CALLBACK (panel_dropdown_deactivate_menu_cb), dropdown);
  }
  gtk_widget_set_sensitive (gtk_widget_get_parent (dropdown->arrow), 
      dropdown->popup != NULL);
}

