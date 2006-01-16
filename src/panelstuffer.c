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

#include <config.h>
#include <panel-applet.h>
#include "panelstuffer.h"

static GtkContainerClass *parent_class = NULL;

#if 0
#  define DEBUG(...) g_print(__VA_ARGS__)
#else
#  define DEBUG(...)
#endif

typedef struct {
  GtkWidget *widget;
  GtkRequisition desired;
  GtkRequisition desired_total;
  gboolean vertical :1;
  gboolean may_rotate :1;
  gboolean fill :1;
} WidgetDefinition;

#define REQUISITION_FITS(stuffer,req) \
  (((stuffer)->orient == GTK_ORIENTATION_HORIZONTAL && (stuffer)->space >= (req)->height) || \
   ((stuffer)->orient == GTK_ORIENTATION_VERTICAL && (stuffer)->space >= (req)->width))

static int
compute_required (PanelStuffer *stuffer, const GtkRequisition *in,
    WidgetDefinition *widgets, int n_widgets, int best)
{
  GtkRequisition req;

  DEBUG ("%*scurrent size is %dx%d\n", 6 - 2 * n_widgets, "", in->width, in->height);
  if (n_widgets <= 0)
    return MIN (stuffer->orient == GTK_ORIENTATION_HORIZONTAL ? in->width : in->height, best);

  if (!GTK_WIDGET_VISIBLE (widgets->widget)) {
    int ret = compute_required (stuffer, in, widgets + 1, n_widgets - 1, best);
    if (ret < best) {
      widgets->desired_total = *in;
    }
    return ret;
  }
  if (widgets->may_rotate) {
    int temp;
    widgets->may_rotate = FALSE;
    best = compute_required (stuffer, in, widgets, n_widgets, best);
    temp = widgets->desired.width;
    widgets->desired.width = widgets->desired.height;
    widgets->desired.height = temp;
    best = compute_required (stuffer, in, widgets, n_widgets, best);
    /* need to rotate back here? */
    widgets->may_rotate = TRUE;
    return best;
  }
  
  req.width = widgets->desired.width + (in->width ? in->width + stuffer->spacing : 0);
  req.height = MAX (in->height, widgets->desired.height);
  if (REQUISITION_FITS (stuffer, &req)) {
    int ret = compute_required (stuffer, &req, widgets + 1, n_widgets - 1, best);
    if (ret < best) {
      widgets->vertical = FALSE;
      widgets->desired_total = req;
      best = ret;
    }
  }
  req.width = MAX (in->width, widgets->desired.width);
  req.height = widgets->desired.height + (in->height ? in->height + stuffer->spacing : 0);
  if (REQUISITION_FITS (stuffer, &req)) {
    int ret = compute_required (stuffer, &req, widgets + 1, n_widgets - 1, best);
    if (ret < best) {
      widgets->vertical = TRUE;
      widgets->desired_total = req;
      best = ret;
    }
  }
  return best;
}

static void
allocate_widget (WidgetDefinition *widget, GtkAllocation *allocation)
{
  GtkAllocation set;
  
  DEBUG ("allocating %dx%d %dx%d\n", allocation->x, allocation->y, 
      allocation->width, allocation->height);
  g_assert (allocation->width >= 0);
  g_assert (allocation->height >= 0);
  if (widget->fill) {
    gtk_widget_size_allocate (widget->widget, allocation);
    return;
  }
  if (widget->may_rotate &&
      allocation->width > allocation->height &&
      widget->desired.width < widget->desired.height) {
    int temp;
    temp = widget->desired.width;
    widget->desired.width = widget->desired.height;
    widget->desired.height = temp;
  }
  set.width = MIN (allocation->width, widget->desired.width);
  set.height = MIN (allocation->height, widget->desired.height);
  set.x = allocation->x + (allocation->width - set.width) / 2;
  set.y = allocation->y + (allocation->height - set.height) / 2;
  gtk_widget_size_allocate (widget->widget, &set);
}

static void
perform_allocation (PanelStuffer *stuffer, WidgetDefinition *widgets, 
    int n_widgets, GtkAllocation *allocation)
{
  GtkAllocation rest, this;
  WidgetDefinition *cur;

  cur = &widgets[n_widgets - 1];
  DEBUG ("allocating %d widgets into %dx%d %dx%d (desired %dx%d - total %dx%d)\n", n_widgets, 
      allocation->x, allocation->y, allocation->width, allocation->height,
      cur->desired.width, cur->desired.height,
      cur->desired_total.width, cur->desired_total.height);
  g_assert (allocation->width >= 0);
  g_assert (allocation->height >= 0);
  /* special cases */
  if (n_widgets < 1)
    return;
  if (!GTK_WIDGET_VISIBLE (cur->widget)) {
    perform_allocation (stuffer, widgets, n_widgets - 1, allocation);
    return;
  }
  if (cur->desired.width == 0 || cur->desired.height == 0) {
    perform_allocation (stuffer, widgets, n_widgets - 1, allocation);
    this.x = allocation->x;
    this.y = allocation->y;
    this.width = this.height = 0;
    allocate_widget (widgets, &this);
    return;
  }
  if (n_widgets == 1) {
    allocate_widget (widgets, allocation);
    return;
  }

  /* for easier usage further down, set desired width and height of 
   * rotatable widgets to min resp. max of those values */
  /* do this elsewhere? */
  if (cur->may_rotate) {
    int min, max;
    min = MIN (cur->desired.height, cur->desired.width);
    max = MAX (cur->desired.height, cur->desired.width);
    cur->desired.width = min;
    cur->desired.height = max;
  }
  
  if (cur->vertical) {
    int desired_rest, desired_this;
    rest.x = this.x = allocation->x;
    rest.width = this.width = allocation->width;
    /* Compute minimum space required by this widget.
     * This gets somewhat tricky if it's allowed to rotate the widget.
     */
    if (cur->may_rotate) {
      if (cur->desired.height <= this.width)
	desired_this = cur->desired.width;
      else
	desired_this = cur->desired.height;
    } else {
      desired_this = cur->desired.height;
    }
    /* we look up the desired size of the rest of widgets one
     * widget further up. This assumes that desired_total is
     * filled in correctly even for special widgets (think: not visible)
     */
    desired_rest = widgets[n_widgets - 2].desired_total.height;
    if (desired_rest == 0) {
      this.y = rest.y = allocation->y;
      this.height = allocation->height;
      rest.height = 0;
    } else {
      int desired_total = desired_this + desired_rest;
      int available = allocation->height - stuffer->spacing;
      this.height = available * desired_this / desired_total;
      rest.height = available - this.height;
      rest.y = allocation->y;
      this.y = rest.y + rest.height + stuffer->spacing;
    }
  } else {
    int desired_rest, desired_this;
    rest.y = this.y = allocation->y;
    rest.height = this.height = allocation->height;
    /* Compute minimum space required by this widget.
     * This gets somewhat tricky if it's allowed to rotate the widget.
     */
    if (cur->may_rotate) {
      if (cur->desired.height <= this.height)
	desired_this = cur->desired.width;
      else
	desired_this = cur->desired.height;
    } else {
      desired_this = cur->desired.width;
    }
    /* we look up the desired size of the rest of widgets one
     * widget further up. This assumes that desired_total is
     * filled in correctly even for special widgets (think: not visible)
     */
    desired_rest = widgets[n_widgets - 2].desired_total.width;
    if (desired_rest == 0) {
      this.x = rest.x = allocation->x;
      this.width = allocation->width;
      rest.width = 0;
    } else {
      int desired_total = desired_this + desired_rest;
      int available = allocation->width - stuffer->spacing;
      this.width = available * desired_this / desired_total;
      rest.width = available - this.width;
      rest.x = allocation->x;
      this.x = rest.x + rest.width + stuffer->spacing;
    }
  }
  allocate_widget (&widgets[n_widgets - 1], &this);
  perform_allocation (stuffer, widgets, n_widgets - 1, &rest);  
}

static int
perform_placement (PanelStuffer *stuffer)
{
  WidgetDefinition *widgets;
  GtkRequisition empty = { 0, 0};

  if (stuffer->children->len == 0)
    return 0;
  widgets = &g_array_index (stuffer->children, WidgetDefinition, 0);
  return compute_required (stuffer, &empty, widgets,
      stuffer->children->len, G_MAXINT);
}

static gint
panel_stuffer_find (PanelStuffer *stuffer, GtkWidget *widget)
{
  guint i;

  for (i = 0; i < stuffer->children->len; i++) {
    WidgetDefinition *d = &g_array_index(stuffer->children,
	WidgetDefinition, i);
    if (d->widget == widget)
      return i;
  }
  return -1;
}

static void
panel_stuffer_add (GtkContainer *container,
    GtkWidget *widget)
{
  PanelStuffer *stuffer = PANEL_STUFFER (container);

  panel_stuffer_add_full (stuffer, widget, FALSE, TRUE);
}

void
panel_stuffer_add_full (PanelStuffer *stuffer, GtkWidget *widget,
    gboolean may_rotate, gboolean fill)
{
  WidgetDefinition d = { 0, };

  g_return_if_fail (PANEL_IS_STUFFER (stuffer));
  g_return_if_fail (GTK_IS_WIDGET (widget));
  
  d.widget = widget;
  d.may_rotate = may_rotate;
  d.fill = fill;
  g_array_append_val (stuffer->children, d);
  gtk_widget_set_parent (widget, GTK_WIDGET (stuffer));
  gtk_widget_queue_resize (GTK_WIDGET (stuffer));
}

static void
panel_stuffer_remove (GtkContainer *container,
    GtkWidget *widget)
{
  gint i;
  gboolean was_visible;
  PanelStuffer *stuffer;

  stuffer = PANEL_STUFFER (container);

  i = panel_stuffer_find (stuffer, widget);
  if (i < 0)
    return;
  g_array_remove_index (stuffer->children, i);

  was_visible = GTK_WIDGET_VISIBLE (widget);
  gtk_widget_unparent (widget);
  if (was_visible)
    gtk_widget_queue_resize (GTK_WIDGET (container));
}

static GType
panel_stuffer_child_type (GtkContainer *container)
{
  return GTK_TYPE_WIDGET;
}

static void
panel_stuffer_forall (GtkContainer *container, gboolean include_internals,
    GtkCallback callback, gpointer callback_data)
{
  PanelStuffer *stuffer;
  guint i;

  g_return_if_fail (callback != NULL);

  stuffer = PANEL_STUFFER (container);

  for (i = 0; i < stuffer->children->len; i++) {
    WidgetDefinition *d = &g_array_index(stuffer->children,
	WidgetDefinition, i);
    (* callback) (d->widget, callback_data);
  }
}

static void 
panel_stuffer_size_request (GtkWidget *widget, GtkRequisition *requisition)
{
  guint i, border;
  PanelStuffer *stuffer = PANEL_STUFFER (widget);

  border = GTK_CONTAINER (stuffer)->border_width;
  for (i = 0; i < stuffer->children->len; i++) {
    WidgetDefinition *d = &g_array_index(stuffer->children,
	WidgetDefinition, i);
    if (GTK_WIDGET_VISIBLE (d->widget)) {
      gtk_widget_size_request (d->widget, &d->desired);
      if (stuffer->orient == GTK_ORIENTATION_VERTICAL &&
	  d->desired.width > stuffer->space &&
	  (!d->may_rotate || d->desired.height > stuffer->space))
	d->desired.width = stuffer->space;
      if (stuffer->orient == GTK_ORIENTATION_HORIZONTAL &&
	  d->desired.height > stuffer->space &&
	  (!d->may_rotate || d->desired.width > stuffer->space))
	d->desired.height = stuffer->space;
    } else {
      d->desired.height = d->desired.width = 0;
    }
  }
  if (stuffer->orient == GTK_ORIENTATION_VERTICAL) {
    requisition->width = stuffer->space + 2 * border;
    requisition->height = perform_placement (stuffer)
	+ 2 * border;
  } else {
    requisition->width = perform_placement (stuffer)
	+ 2 * border;
    requisition->height = stuffer->space + 2 * border;
  }
  DEBUG ("size: %dx%d (%d children)\n", requisition->width, requisition->height, stuffer->children->len);
}

static void 
panel_stuffer_size_allocate (GtkWidget *widget, GtkAllocation *allocation)
{
  PanelStuffer *stuffer = PANEL_STUFFER (widget);

  /* should we perform allocation if space is wrong anyway? */
  perform_allocation (stuffer, &g_array_index(stuffer->children, WidgetDefinition, 0),
      stuffer->children->len, allocation);
  if (stuffer->orient == GTK_ORIENTATION_HORIZONTAL && 
      allocation->height != stuffer->space) {
    stuffer->space = allocation->height;
    gtk_widget_queue_resize (widget);
  } else if (stuffer->orient == GTK_ORIENTATION_VERTICAL && 
      allocation->width != stuffer->space) {
    stuffer->space = allocation->width;
    gtk_widget_queue_resize (widget);
  }
  DEBUG ("allocation done into %s widget\n", 
      stuffer->orient == GTK_ORIENTATION_HORIZONTAL ? "horizontal" : "vertical");

  GTK_WIDGET_CLASS (parent_class)->size_allocate (widget, allocation);
}

static void
panel_stuffer_orientation_cb (PanelApplet *applet, guint orient, PanelStuffer *stuffer)
{
  GtkOrientation orientation;
  
  switch (orient) {
    default:
      g_warning ("Unkown panel orientation %u", orient);
    case PANEL_APPLET_ORIENT_UP:
    case PANEL_APPLET_ORIENT_DOWN:
      orientation = GTK_ORIENTATION_HORIZONTAL;
      break;
    case PANEL_APPLET_ORIENT_LEFT:
    case PANEL_APPLET_ORIENT_RIGHT:
      orientation = GTK_ORIENTATION_VERTICAL;
      break;
  }
  panel_stuffer_set_orientation (stuffer, orientation);
}

static void
panel_stuffer_parent_set (GtkWidget *widget, GtkWidget *previous_parent)
{
  PanelStuffer *stuffer;

  stuffer = PANEL_STUFFER (widget);
  if (stuffer->orient_signal) {
    g_signal_handler_disconnect (previous_parent, stuffer->orient_signal);
    stuffer->orient_signal = 0;
  }
  if (GTK_WIDGET_CLASS (parent_class)->parent_set)
    GTK_WIDGET_CLASS (parent_class)->parent_set (widget, previous_parent);
  if (PANEL_IS_APPLET (gtk_widget_get_parent (widget))) {
    PanelApplet *parent = PANEL_APPLET (gtk_widget_get_parent (widget));
    stuffer->orient_signal = g_signal_connect (parent,
	"change-orient", G_CALLBACK (panel_stuffer_orientation_cb), stuffer);
    panel_stuffer_orientation_cb (parent, panel_applet_get_orient (parent), stuffer);
  }
}

static void
panel_stuffer_finalize (GObject *object)
{
  PanelStuffer *stuffer;

  stuffer = PANEL_STUFFER (object);
  g_array_free (stuffer->children, TRUE);

  g_assert (stuffer->orient_signal == 0);
      
  G_OBJECT_CLASS (parent_class)->finalize (object);
}

static void
panel_stuffer_class_init (PanelStufferClass *class)
{
  GObjectClass *gobject_class = G_OBJECT_CLASS (class);
  GtkWidgetClass *widget_class = GTK_WIDGET_CLASS (class);
  GtkContainerClass *container_class = GTK_CONTAINER_CLASS (class);

  parent_class = g_type_class_peek_parent (class);

  gobject_class->finalize = panel_stuffer_finalize;
  //gobject_class->set_property = panel_stuffer_set_property;
  //gobject_class->get_property = panel_stuffer_get_property;
   
  widget_class->size_request = panel_stuffer_size_request;
  widget_class->size_allocate = panel_stuffer_size_allocate;
  widget_class->parent_set = panel_stuffer_parent_set;

  container_class->add = panel_stuffer_add;
  container_class->remove = panel_stuffer_remove;
  container_class->forall = panel_stuffer_forall;
  container_class->child_type = panel_stuffer_child_type;
  //container_class->set_child_property = panel_stuffer_set_child_property;
  //container_class->get_child_property = panel_stuffer_get_child_property;
}

static void
panel_stuffer_init (PanelStuffer *stuffer)
{
  GTK_WIDGET_SET_FLAGS (stuffer, GTK_NO_WINDOW);
  gtk_widget_set_redraw_on_allocate (GTK_WIDGET (stuffer), FALSE);
  
  stuffer->children = g_array_new (FALSE, FALSE, sizeof (WidgetDefinition));
  stuffer->spacing = 2;
  stuffer->space = 50; /* random number */
}

GType
panel_stuffer_get_type (void)
{
  static GType stuffer_type = 0;

  if (!stuffer_type)
    {
      static const GTypeInfo stuffer_info =
      {
	sizeof (PanelStufferClass),
	NULL,		/* base_init */
	NULL,		/* base_finalize */
	(GClassInitFunc) panel_stuffer_class_init,
	NULL,		/* class_finalize */
	NULL,		/* class_data */
	sizeof (PanelStuffer),
	0,		/* n_preallocs */
	(GInstanceInitFunc) panel_stuffer_init,
	NULL,		/* value_table */
      };

      stuffer_type = g_type_register_static (GTK_TYPE_CONTAINER, "PanelStuffer", 
					 &stuffer_info, 0);
    }

  return stuffer_type;
}

GtkWidget *
panel_stuffer_new (GtkOrientation orient)
{
  PanelStuffer *stuffer;

  g_return_val_if_fail (orient == GTK_ORIENTATION_HORIZONTAL || orient == GTK_ORIENTATION_VERTICAL, NULL);

  stuffer = g_object_new (PANEL_TYPE_STUFFER, NULL);
  stuffer->orient = orient;

  return GTK_WIDGET (stuffer);
}

void
panel_stuffer_set_orientation (PanelStuffer *stuffer, GtkOrientation orient)
{
  g_return_if_fail (PANEL_IS_STUFFER (stuffer));
  g_return_if_fail (orient == GTK_ORIENTATION_HORIZONTAL || orient == GTK_ORIENTATION_VERTICAL);
  
  if (stuffer->orient == orient)
    return;
  stuffer->orient = orient;
  gtk_widget_queue_resize (GTK_WIDGET (stuffer));
}

