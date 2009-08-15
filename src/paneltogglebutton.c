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

#ifdef HAVE_CONFIG_H
#  include "config.h"
#endif
#include "paneltogglebutton.h"

static GtkToggleButtonClass *parent_class = NULL;


static void 
panel_toggle_button_size_request (GtkWidget *widget, 
    GtkRequisition *requisition)
{
  GtkWidget *child;
  
  child = GTK_BIN (widget)->child;

  if (child) {
    gtk_widget_size_request (child, requisition);
  } else {
    requisition->width = requisition->height = 0;
  }
}

static void
panel_toggle_button_size_allocate (GtkWidget *widget,
    GtkAllocation *allocation)
{
  GtkWidget *child;
  
  child = GTK_BIN (widget)->child;

  widget->allocation = *allocation;
  
  if (GTK_WIDGET_REALIZED (widget))
    gdk_window_move_resize (GTK_BUTTON (widget)->event_window, 
	widget->allocation.x, widget->allocation.y,
	widget->allocation.width, widget->allocation.height);
  
  if (child)
    gtk_widget_size_allocate (child, allocation);
}

static gboolean
panel_toggle_button_expose (GtkWidget *widget, GdkEventExpose *event)
{
  if (GTK_WIDGET_DRAWABLE (widget)) {
    GtkWidget *child = GTK_BIN (widget)->child;
    GtkStateType state_type;
    GtkShadowType shadow_type;

    state_type = GTK_WIDGET_STATE (widget);
    
    /* FIXME: someone make this layout work nicely for all themes
     * Currently I'm trying to imitate the volume applet's widget */
    if (GTK_TOGGLE_BUTTON (widget)->inconsistent) {
      if (state_type == GTK_STATE_ACTIVE)
	state_type = GTK_STATE_NORMAL;
      shadow_type = GTK_SHADOW_ETCHED_IN;
    } else {
      shadow_type = GTK_BUTTON (widget)->depressed ? GTK_SHADOW_IN : GTK_SHADOW_OUT;
    }
    if (GTK_BUTTON (widget)->depressed)
      state_type = GTK_STATE_SELECTED;
    /* FIXME: better detail? */
    gtk_paint_flat_box (widget->style, widget->window, state_type, shadow_type,
	&event->area, widget, "togglebutton", widget->allocation.x,
	widget->allocation.y, widget->allocation.width, widget->allocation.height);
      
    if (child)
      gtk_container_propagate_expose (GTK_CONTAINER (widget), child, event);
  }
  
  return FALSE;
}

static gboolean
panel_toggle_button_button_press (GtkWidget *widget, GdkEventButton *event)
{
  if (event->button == 3 || event->button == 2)
    return FALSE;

  return GTK_WIDGET_CLASS (parent_class)->button_press_event (widget, event);
}

static gboolean
panel_toggle_button_button_release (GtkWidget *widget, GdkEventButton *event)
{
  if (event->button == 3 || event->button == 2)
    return FALSE;

  return GTK_WIDGET_CLASS (parent_class)->button_release_event (widget, event);
}

static void
panel_toggle_button_class_init (PanelToggleButtonClass *class)
{
  GtkWidgetClass *widget_class = GTK_WIDGET_CLASS (class);

  parent_class = g_type_class_peek_parent (class);

  widget_class->size_request = panel_toggle_button_size_request;
  widget_class->size_allocate = panel_toggle_button_size_allocate;
  widget_class->expose_event = panel_toggle_button_expose;
  widget_class->button_press_event = panel_toggle_button_button_press;
  widget_class->button_release_event = panel_toggle_button_button_release;
}

static void
panel_toggle_button_init (PanelToggleButton *toggle_button)
{
}

GType
panel_toggle_button_get_type (void)
{
  static GType toggle_button_type = 0;

  if (!toggle_button_type)
    {
      static const GTypeInfo toggle_button_info =
      {
	sizeof (PanelToggleButtonClass),
	NULL,		/* base_init */
	NULL,		/* base_finalize */
	(GClassInitFunc) panel_toggle_button_class_init,
	NULL,		/* class_finalize */
	NULL,		/* class_data */
	sizeof (PanelToggleButton),
	0,		/* n_preallocs */
	(GInstanceInitFunc) panel_toggle_button_init,
	NULL,		/* value_table */
      };

      toggle_button_type = g_type_register_static (GTK_TYPE_TOGGLE_BUTTON, "PanelToggleButton", 
					 &toggle_button_info, 0);
    }

  return toggle_button_type;
}

GtkWidget *
panel_toggle_button_new (void)
{
  return GTK_WIDGET (g_object_new (PANEL_TYPE_TOGGLE_BUTTON, NULL));
}

