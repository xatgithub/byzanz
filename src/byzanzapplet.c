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
#include <glib/gstdio.h>
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
  char *		tmp_file;	/* filename that's recorded to */
  GTimeVal		start;		/* time the recording started */
  guint			update_func;	/* id of idle function that updates state */
} AppletPrivate;
#define APPLET_IS_RECORDING(applet) ((applet)->tmp_file != NULL)

static void
byzanz_applet_show_error (AppletPrivate *priv, GtkWindow *parent, const char *error)
{
  GtkWidget *dialog;

  if (parent == NULL)
    parent = GTK_WINDOW (gtk_widget_get_toplevel (GTK_WIDGET (priv->applet)));
  dialog = gtk_message_dialog_new (parent, GTK_DIALOG_MODAL, GTK_MESSAGE_ERROR,
      GTK_BUTTONS_CLOSE, error);
  gtk_dialog_run (GTK_DIALOG (dialog));
  gtk_widget_destroy (dialog);
}

static void
byzanz_applet_destroy_recorder (AppletPrivate *priv)
{
  g_assert (priv->rec);

  byzanz_recorder_destroy (priv->rec);
  priv->rec = NULL;
  gtk_label_set_text (GTK_LABEL (priv->label), _("OFF"));
  g_source_remove (priv->update_func);
  priv->update_func = 0;
}

static gboolean
byzanz_applet_is_recording (AppletPrivate *priv)
{
  return priv->tmp_file != NULL;
}

static void
byzanz_applet_ensure_text (AppletPrivate *priv, const char *text)
{
  const char *current;

  current = gtk_label_get_text (GTK_LABEL (priv->label));
  if (g_str_equal (current, text))
    return;
  gtk_label_set_text (GTK_LABEL (priv->label), text);
}

static gboolean
byzanz_applet_update (gpointer data)
{
  AppletPrivate *priv = data;

  if (byzanz_applet_is_recording (priv)) {
    /* applet is still actively recording the screen */
    GTimeVal tv;
    guint elapsed;
    gchar *str;
    
    g_get_current_time (&tv);
    elapsed = tv.tv_sec - priv->start.tv_sec;
    if (tv.tv_usec < priv->start.tv_usec)
      elapsed--;
    str = g_strdup_printf ("%u", elapsed);
    byzanz_applet_ensure_text (priv, str);
    g_free (str);
    gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (priv->button), TRUE);
    gtk_widget_set_sensitive (priv->button, TRUE);
  } else {
    gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (priv->button), FALSE);
    if (priv->rec && !byzanz_recorder_is_active (priv->rec))
      byzanz_applet_destroy_recorder (priv);
    if (priv->rec) {
      /* applet is not recording, but still saving content */
      byzanz_applet_ensure_text (priv, _("SAVE"));
      gtk_widget_set_sensitive (priv->button, FALSE);
    } else {
      /* applet is idle */
      byzanz_applet_ensure_text (priv, _("OFF"));
      gtk_widget_set_sensitive (priv->button, TRUE);
    }
  }
  
  return TRUE;
}

static void
byzanz_applet_start_recording (AppletPrivate *priv)
{
  GdkWindow *window;
  GdkRectangle area;
  
  g_assert (!byzanz_applet_is_recording (priv));
  
  if (byzanz_applet_is_recording (priv))
    goto out;
  if (priv->rec) {
    if (byzanz_recorder_is_active (priv->rec))
      goto out;
    byzanz_recorder_destroy (priv->rec);
    priv->rec = NULL;
  }
  
  window = byzanz_select_method_select (0, &area); 
  if (window) {
    int fd = g_file_open_tmp ("byzanzXXXXXX", &priv->tmp_file, NULL);
    if (fd > 0) 
      priv->rec = byzanz_recorder_new_fd (fd, window, &area, TRUE);
  }
  if (priv->rec) {
    byzanz_recorder_prepare (priv->rec);
    byzanz_recorder_start (priv->rec);
    g_get_current_time (&priv->start);
    priv->update_func = g_timeout_add (1000, byzanz_applet_update, priv);
  }

out:
  byzanz_applet_update (priv);
}

static void
byzanz_applet_stop_recording (AppletPrivate *priv)
{
  GtkWidget *dialog;
  char *tmp_file;
  
  g_assert (byzanz_applet_is_recording (priv));
  
  byzanz_recorder_stop (priv->rec);
  tmp_file = priv->tmp_file;
  priv->tmp_file = NULL;
  byzanz_applet_update (priv);
  dialog = gtk_file_chooser_dialog_new (_("Save Recorded File"),
      GTK_WINDOW (gtk_widget_get_toplevel (GTK_WIDGET (priv->applet))), 
      GTK_FILE_CHOOSER_ACTION_SAVE,
      GTK_STOCK_CANCEL, GTK_RESPONSE_CANCEL,
      GTK_STOCK_SAVE, GTK_RESPONSE_ACCEPT,
      NULL);
  gtk_file_chooser_set_current_folder (GTK_FILE_CHOOSER (dialog), g_get_home_dir ());
  gtk_dialog_set_default_response (GTK_DIALOG (dialog), GTK_RESPONSE_ACCEPT);
repeat:
  if (GTK_RESPONSE_ACCEPT == gtk_dialog_run (GTK_DIALOG (dialog))) {
    gchar *filename = gtk_file_chooser_get_filename (GTK_FILE_CHOOSER (dialog));
    errno = 0;
    if (g_rename (tmp_file, filename) != 0) {
      int save_errno = errno;
      char *display_name = g_filename_display_name (filename);
      char *error = g_strdup_printf (_("The file could not be saved to %s.\n"
	    "The error given was: %s.\n"
	    "Please try a different file."), display_name, g_strerror (save_errno));

      byzanz_applet_show_error (priv, GTK_WINDOW (dialog), error);
      goto repeat;
    }
    g_free (filename);
  } else {
    g_unlink (tmp_file);
  }
  gtk_widget_destroy (dialog);
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
  if (priv->rec) 
    byzanz_applet_destroy_recorder (priv);
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

