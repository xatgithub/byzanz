/* desktop recorder
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

#include <panel-applet.h>
#include <gtk/gtklabel.h>
#include "byzanzrecorder.h"
#include "byzanzselect.h"
#include "panelstuffer.h"
#include "i18n.h"

typedef struct {
  PanelApplet *		applet;		/* the applet we manage */

  GtkWidget *		button;		/* recording button */
  GtkWidget *		label;		/* infotext label */
  GtkWidget *		progress;	/* progressbar showing cache effects */
  
  ByzanzRecorder *	rec;		/* the recorder (if recording) */
  GTimeVal		start;		/* time the recording started */
  guint			idle_func;	/* id of idle function that updates state */
} AppletPrivate;

static gboolean
byzanz_applet_update (gpointer data)
{
  GTimeVal tv;
  guint elapsed;
  gchar *str;
  
  AppletPrivate *priv = data;

  g_get_current_time (&tv);
  elapsed = tv.tv_sec - priv->start.tv_sec;
  if (tv.tv_usec < priv->start.tv_usec)
    elapsed--;
  str = g_strdup_printf ("%u", elapsed);
  gtk_label_set_text (GTK_LABEL (priv->label), str);
  g_free (str);

  return TRUE;
}

static gboolean
byzanz_applet_is_recording (AppletPrivate *priv)
{
  return priv->rec != NULL;
}

static void
byzanz_applet_start_recording (AppletPrivate *priv)
{
  gboolean active;
  GdkWindow *window;
  GdkRectangle area;
  
  g_assert (!byzanz_applet_is_recording (priv));
  
  window = byzanz_select_method_select (0, &area); 
  if (window)
    priv->rec = byzanz_recorder_new ("/root/test.gif", window, area.x, area.y, 
	area.width, area.height, TRUE);
  if (priv->rec) {
    byzanz_recorder_prepare (priv->rec);
    byzanz_recorder_start (priv->rec);
    g_get_current_time (&priv->start);
    priv->idle_func = g_timeout_add (1000, byzanz_applet_update, priv);
  }

  active = (priv->rec != NULL);
  if (gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (priv->button)) != active)
    gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (priv->button), active);
}

static void
byzanz_applet_stop_recording (AppletPrivate *priv)
{
  gboolean active;
  
  g_assert (byzanz_applet_is_recording (priv));
  
  byzanz_recorder_stop (priv->rec);
  byzanz_recorder_destroy (priv->rec);
  priv->rec = NULL;
  g_source_remove (priv->idle_func);
  priv->idle_func = 0;
  gtk_label_set_text (GTK_LABEL (priv->label), _("OFF"));

  active = (priv->rec != NULL);
  if (gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (priv->button)) != active)
    gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (priv->button), active);
}

static void
button_clicked_cb (GtkToggleButton *button, AppletPrivate *priv)
{
  gboolean active = gtk_toggle_button_get_active (button);
  
  if (priv->rec && !active) {
    byzanz_applet_stop_recording (priv);
  } else if (!priv->rec && active) {
    byzanz_applet_start_recording (priv);
  }
}

static void
destroy_applet (GtkWidget *widget, AppletPrivate *priv)
{
  if (byzanz_applet_is_recording (priv))
    byzanz_applet_stop_recording (priv);
  g_free (priv);
}

static gboolean
byzanz_applet_fill (PanelApplet *applet, const gchar *iid, gpointer data)
{
  AppletPrivate *priv;
  GtkWidget *image, *stuffer;
  
  priv = g_new0 (AppletPrivate, 1);
  priv->applet = applet;
  g_signal_connect (applet, "destroy", G_CALLBACK (destroy_applet), priv);

  panel_applet_set_flags (applet, PANEL_APPLET_EXPAND_MINOR);
  /* create UI */
  stuffer = panel_stuffer_new (GTK_ORIENTATION_VERTICAL);
  gtk_container_add (GTK_CONTAINER (applet), stuffer);

  priv->button = gtk_toggle_button_new ();
  image = gtk_image_new_from_stock (GTK_STOCK_MEDIA_RECORD, 
      GTK_ICON_SIZE_SMALL_TOOLBAR);
  gtk_container_add (GTK_CONTAINER (priv->button), image);
  g_signal_connect (priv->button, "toggled", G_CALLBACK (button_clicked_cb), priv);
  panel_stuffer_add_full (PANEL_STUFFER (stuffer), priv->button, FALSE, TRUE);

  /* translators: the label advertises a width of 5 characters */
  priv->label = gtk_label_new (_("OFF"));
  gtk_label_set_width_chars (GTK_LABEL (priv->label), 5);
  gtk_label_set_justify (GTK_LABEL (priv->label), GTK_JUSTIFY_CENTER);
  panel_stuffer_add_full (PANEL_STUFFER (stuffer), priv->label, FALSE, FALSE);

  gtk_widget_show_all (GTK_WIDGET (applet));
  return TRUE;
}

PANEL_APPLET_BONOBO_FACTORY ("OAFIID:ByzanzApplet_Factory",
    PANEL_TYPE_APPLET, "ByzanzApplet", "0",
    byzanz_applet_fill, NULL);

