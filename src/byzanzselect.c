/* desktop session recorder
 * Copyright (C) 2005 Benjamin Otte <otte@gnome.org
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Library General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
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
#  include "config.h"
#endif

#include "byzanzselect.h"
#include "i18n.h"
#include "screenshot-utils.h"

/*** SELECT AREA ***/

typedef struct {
  GtkWidget *window;
  GdkImage *root; /* only used without XComposite, NULL otherwise */
  GMainLoop *loop;
  gint x0;
  gint y0;
  gint x1;
  gint y1;
} WindowData;

/* define for SLOW selection mechanism */
#undef TARGET_LINE

static gboolean
expose_cb (GtkWidget *widget, GdkEventExpose *event, gpointer datap)
{
  cairo_t *cr;
  WindowData *data = datap;
#ifdef TARGET_LINE
  static double dashes[] = { 1.0, 2.0 };
#endif

  cr = gdk_cairo_create (widget->window);
  cairo_rectangle (cr, event->area.x, event->area.y, event->area.width, event->area.height);
  cairo_clip (cr);
  if (data->root) {
    gdk_draw_image (widget->window, widget->style->black_gc, data->root,
	event->area.x, event->area.y, event->area.x, event->area.y,
	event->area.width, event->area.height);
  } else {
    cairo_set_operator (cr, CAIRO_OPERATOR_SOURCE);
    cairo_set_source_rgba (cr, 0.0, 0.0, 0.0, 0.0);
    cairo_paint (cr);
  }
  /* FIXME: make colors use theme */
  cairo_set_line_width (cr, 1.0);
#ifdef TARGET_LINE
  cairo_set_source_rgba (cr, 1.0, 0.0, 0.0, 1.0);
  cairo_set_dash (cr, dashes, G_N_ELEMENTS (dashes), 0.0);
  cairo_move_to (cr, data->x1 - 0.5, 0.0);
  cairo_line_to (cr, data->x1 - 0.5, event->area.y + event->area.height); /* end of screen really */
  cairo_move_to (cr, 0.0, data->y1 - 0.5);
  cairo_line_to (cr, event->area.x + event->area.width, data->y1 - 0.5); /* end of screen really */
  cairo_stroke (cr);
#endif
  if (data->x0 >= 0) {
    double x, y, w, h;
    x = MIN (data->x0, data->x1);
    y = MIN (data->y0, data->y1);
    w = MAX (data->x0, data->x1) - x;
    h = MAX (data->y0, data->y1) - y;
    cairo_set_source_rgba (cr, 0.0, 0.0, 0.5, 0.2);
    cairo_set_dash (cr, NULL, 0, 0.0);
    cairo_rectangle (cr, x, y, w, h);
    cairo_fill (cr);
    cairo_set_source_rgba (cr, 0.0, 0.0, 0.5, 0.5);
    cairo_rectangle (cr, x + 0.5, y + 0.5, w - 1, h - 1);
    cairo_stroke (cr);
  }
  if (cairo_status (cr) != CAIRO_STATUS_SUCCESS)
    g_warning ("cairo error: %s\n", cairo_status_to_string (cairo_status (cr)));
  cairo_destroy (cr);
  return FALSE;
}

static void
byzanz_select_area_stop (WindowData *data)
{
  gtk_widget_destroy (data->window);
  g_main_loop_quit (data->loop);
}

static gboolean
button_pressed_cb (GtkWidget *widget, GdkEventButton *event, gpointer datap)
{
  WindowData *data = datap;

  if (event->button != 1) {
    data->x0 = data->y0 = -1;
    byzanz_select_area_stop (data);
    return TRUE;
  }
  data->x0 = event->x;
  data->y0 = event->y;

  gtk_widget_queue_draw (widget);

  return TRUE;
}

static gboolean
button_released_cb (GtkWidget *widget, GdkEventButton *event, gpointer datap)
{
  WindowData *data = datap;
  
  if (event->button == 1 && data->x0 >= 0) {
    data->x1 = event->x + 1;
    data->y1 = event->y + 1;
    byzanz_select_area_stop (data);
  }
  
  return TRUE;
}

static gboolean
motion_notify_cb (GtkWidget *widget, GdkEventMotion *event, gpointer datap)
{
  WindowData *data = datap;
  
#ifdef TARGET_LINE
  gtk_widget_queue_draw (widget);
#else
  if (data->x0 >= 0) {
    GdkRectangle rect;
    rect.x = MIN (data->x0, MIN (data->x1, event->x + 1));
    rect.width = MAX (data->x0, MAX (data->x1, event->x + 1)) - rect.x;
    rect.y = MIN (data->y0, MIN (data->y1, event->y + 1));
    rect.height = MAX (data->y0, MAX (data->y1, event->y + 1)) - rect.y;
    gtk_widget_queue_draw_area (widget, rect.x, rect.y, rect.width, rect.height);
  }
#endif
  data->x1 = event->x + 1;
  data->y1 = event->y + 1;

  return TRUE;
}

static void
realize_cb (GtkWidget *widget, gpointer datap)
{
  GdkWindow *window = widget->window;
  GdkCursor *cursor;

  gdk_window_set_events (window, gdk_window_get_events (window) |
      GDK_BUTTON_PRESS_MASK |
      GDK_BUTTON_RELEASE_MASK |
      GDK_POINTER_MOTION_MASK);
  cursor = gdk_cursor_new (GDK_CROSSHAIR);
  gdk_window_set_cursor (window, cursor);
  gdk_cursor_unref (cursor);
  gdk_window_set_back_pixmap (window, NULL, FALSE);
}

static gboolean
quit_cb (gpointer datap)
{
  WindowData *data = datap;

  g_main_loop_quit (data->loop);

  return FALSE;
}

static GdkWindow *
byzanz_select_area (GdkRectangle *rect)
{
  GdkColormap *rgba;
  WindowData *data;
  GdkWindow *ret = NULL;
  
  rgba = gdk_screen_get_rgba_colormap (gdk_screen_get_default ());
  data = g_new0 (WindowData, 1);
  data->window = gtk_window_new (GTK_WINDOW_TOPLEVEL);
  data->loop = g_main_loop_new (NULL, FALSE);
  data->x0 = data->y0 = -1;
  if (rgba && gdk_screen_is_composited (gdk_screen_get_default ())) {
    gtk_widget_set_colormap (data->window, rgba);
  } else {
    GdkWindow *root = gdk_get_default_root_window ();
    gint width, height;
    gdk_drawable_get_size (root, &width, &height);
    data->root = gdk_drawable_get_image (root, 0, 0, width, height);
  }
  gtk_widget_set_app_paintable (data->window, TRUE);
  gtk_window_fullscreen (GTK_WINDOW (data->window));
  g_signal_connect (data->window, "expose-event", G_CALLBACK (expose_cb), data);
  g_signal_connect (data->window, "button-press-event", G_CALLBACK (button_pressed_cb), data);
  g_signal_connect (data->window, "button-release-event", G_CALLBACK (button_released_cb), data);
  g_signal_connect (data->window, "motion-notify-event", G_CALLBACK (motion_notify_cb), data);
  g_signal_connect (data->window, "delete-event", G_CALLBACK (gtk_main_quit), data);
  g_signal_connect_after (data->window, "realize", G_CALLBACK (realize_cb), data);
  gtk_widget_show_all (data->window);

  g_main_loop_run (data->loop);
  
  if (data->x0 >= 0) {
    rect->x = MIN (data->x1, data->x0);
    rect->y = MIN (data->y1, data->y0);
    rect->width = MAX (data->x1, data->x0) - rect->x;
    rect->height = MAX (data->y1, data->y0) - rect->y;
    ret = gdk_get_default_root_window ();
    /* stupid hack to get around a recorder recording the selection screen */
    gdk_display_sync (gdk_display_get_default ());
    g_timeout_add (1000, quit_cb, data);
    g_main_loop_run (data->loop);
  }
  g_main_loop_unref (data->loop);
  if (data->root)
    g_object_unref (data->root);
  g_free (data);
  g_object_ref (ret);
  return ret;
}

/*** WHOLE SCREEN ***/

static GdkWindow *
byzanz_select_screen (GdkRectangle *rect)
{
  GdkWindow *root;
  
  root = gdk_get_default_root_window ();
  rect->x = rect->y = 0;
  gdk_drawable_get_size (root, &rect->width, &rect->height);
  g_object_ref (root);
  
  return root;
}

/*** APPLICATION WINDOW ***/

typedef struct {
  GMainLoop *loop;
  GdkWindow *window;
} PickWindowData;

static gboolean
select_window_button_pressed_cb (GtkWidget *widget, GdkEventButton *event, gpointer datap)
{
  PickWindowData *data = datap;
  
  gdk_pointer_ungrab (event->time);
  g_main_loop_quit (data->loop);
  if (event->button == 1) {
    Window w;
    w = screenshot_find_current_window (TRUE);
    if (w != None)
      data->window = gdk_window_foreign_new (w);
    else
      data->window = gdk_get_default_root_window ();
  }
  return TRUE;
}

static GdkWindow *
byzanz_select_window (GdkRectangle *area)
{
  GdkCursor *cursor;
  GtkWidget *widget;
  PickWindowData data = { NULL, NULL };
  
  cursor = gdk_cursor_new (GDK_CROSSHAIR);
  widget = gtk_invisible_new ();
  g_signal_connect (widget, "button-press-event", 
      G_CALLBACK (select_window_button_pressed_cb), &data);
  gtk_widget_show (widget);
  gdk_pointer_grab (widget->window, FALSE, GDK_BUTTON_PRESS_MASK, NULL, cursor, GDK_CURRENT_TIME);
  data.loop = g_main_loop_new (NULL, FALSE);
  
  g_main_loop_run (data.loop);
  
  g_main_loop_unref (data.loop);
  gtk_widget_destroy (widget);
  gdk_cursor_unref (cursor);
  if (!data.window)
    return NULL;
  gdk_window_get_root_origin (data.window, &area->x, &area->y);
  gdk_drawable_get_size (data.window, &area->width, &area->height);
  g_object_unref (data.window);

  return g_object_ref (gdk_get_default_root_window ());
}
  
/*** API ***/

static const struct {
  const char * mnemonic;
  const char * description;
  const char * icon_name;
  const char * method_name;
  GdkWindow * (* select) (GdkRectangle *rect);
} methods [] = {
  { N_("Record _Desktop"), N_("Record the entire desktop"), 
    "byzanz-record-desktop", "screen", byzanz_select_screen },
  { N_("Record _Area"), N_("Record a selected area of the desktop"), 
    "byzanz-record-area", "area", byzanz_select_area },
  { N_("Record _Window"), N_("Record a selected window"), 
    "byzanz-record-window", "window", byzanz_select_window }
};
#define BYZANZ_METHOD_COUNT G_N_ELEMENTS(methods)

guint
byzanz_select_get_method_count (void)
{
  return BYZANZ_METHOD_COUNT;
}

const char *
byzanz_select_method_get_icon_name (guint method)
{
  g_return_val_if_fail (method < BYZANZ_METHOD_COUNT, NULL);

  return methods[method].icon_name;
}

const char *
byzanz_select_method_get_name (guint method)
{
  g_return_val_if_fail (method < BYZANZ_METHOD_COUNT, NULL);

  return methods[method].method_name;
}

int
byzanz_select_method_lookup (const char *name)
{
  guint i;

  g_return_val_if_fail (name != NULL, -1);

  for (i = 0; i < BYZANZ_METHOD_COUNT; i++) {
    if (g_str_equal (name, methods[i].method_name))
      return i;
  }
  return -1;
}

const char *
byzanz_select_method_describe (guint method)
{
  g_return_val_if_fail (method < BYZANZ_METHOD_COUNT, NULL);

  return _(methods[method].description);
}

const char *
byzanz_select_method_get_mnemonic (guint method)
{
  g_return_val_if_fail (method < BYZANZ_METHOD_COUNT, NULL);

  return _(methods[method].mnemonic);
}

/**
 * byzanz_select_method_select:
 * @method: id of the method to use
 * @rect: rectangle that will be set to the coordinates to record relative to 
 *        the window
 *
 * Gets the area of the window to record. Note that this might require running
 * the main loop while waiting for user interaction, so be prepared for this.
 *
 * Returns: The #GdkWindow to record from or %NULL if the user aborted.
 *          You must g_object_unref() the window after use.
 **/
GdkWindow *
byzanz_select_method_select (guint method, GdkRectangle *rect)
{
  g_return_val_if_fail (method < BYZANZ_METHOD_COUNT, NULL);
  g_return_val_if_fail (rect != NULL, NULL);

  g_assert (methods[method].select != NULL);

  return methods[method].select (rect);
}

