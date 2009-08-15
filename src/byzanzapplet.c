/* desktop recorder
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

#ifdef HAVE_CONFIG_H
#  include "config.h"
#endif

#include <unistd.h>

#include <panel-applet.h>
#include <gio/gio.h>
#include <glib/gstdio.h>
#include <panel-applet-gconf.h>
#include "byzanzrecorder.h"
#include "byzanzselect.h"
#include "paneltogglebutton.h"
#include "paneldropdown.h"
#include "i18n.h"

static GQuark index_quark = 0;

typedef struct {
  PanelApplet *		applet;		/* the applet we manage */

  GtkWidget *		button;		/* recording button */
  GtkWidget *		image;		/* image displayed in button */
  GtkWidget *		dropdown;	/* dropdown button */
  GtkWidget *		menu;		/* the menu that's dropped down */
  GtkWidget *		record_cursor;	/* checkmenuitem for cursor recording */
  GtkTooltips *		tooltips;	/* our tooltips */
  
  ByzanzRecorder *	rec;		/* the recorder (if recording) */
  char *		tmp_file;	/* filename that's recorded to */
  GTimeVal		start;		/* time the recording started */

  /* config */
  int			method;		/* recording method that was set */
} AppletPrivate;
#define APPLET_IS_RECORDING(applet) ((applet)->tmp_file != NULL)

/*** PENDING RECORDING ***/

typedef struct {
  AppletPrivate *	priv;
  ByzanzRecorder *	rec;
  GFile *		source;
  GFile *		destination;
  GCancellable *	cancellable;
} PendingRecording;

static void
byzanz_applet_show_error (GtkWindow *parent, const char *error, const char *details, ...)
{
  GtkWidget *dialog;
  gchar *msg;
  va_list args;

  g_return_if_fail (details != NULL);
  
  va_start (args, details);
  msg = g_strdup_vprintf (details, args);
  va_end (args);
  dialog = gtk_message_dialog_new (parent, GTK_DIALOG_DESTROY_WITH_PARENT, GTK_MESSAGE_ERROR,
      GTK_BUTTONS_CLOSE, "%s", error ? error : msg);
  if (parent == NULL)
    gtk_window_set_icon_name (GTK_WINDOW (dialog), "byzanz-record-desktop");
  if (error)
    gtk_message_dialog_format_secondary_text (GTK_MESSAGE_DIALOG (dialog), "%s", msg);
  g_free (msg);
  g_signal_connect (dialog, "response", G_CALLBACK (gtk_widget_destroy), NULL);
  gtk_widget_show_all (dialog);
}

static void
pending_recording_destroy (PendingRecording *pending)
{
  g_assert (pending->rec == NULL);

  if (pending->source) {
    g_file_delete (pending->source, NULL, NULL);
    g_object_unref (pending->source);
  }
  if (pending->destination)
    g_object_unref (pending->destination);
  g_free (pending);
}

static void
done_saving_cb (GObject *file, GAsyncResult *res, gpointer data)
{
  PendingRecording *pending = data;
  GError *error = NULL;

  if (!g_file_copy_finish (G_FILE (file), res, &error)) {
    byzanz_applet_show_error (NULL, _("Failed to save file."), error->message);
    g_error_free (error);
  }

  pending_recording_destroy (pending);
}

static gboolean
check_done_saving_cb (gpointer data)
{
  PendingRecording *pending = data;

  if (byzanz_recorder_is_active (pending->rec))
    return TRUE;
  byzanz_recorder_destroy (pending->rec);
  pending->rec = NULL;

  if (pending->destination == NULL) {
    pending_recording_destroy (pending);
    return FALSE;
  }
  /* There's no g_file_move_async(), so use copy + delete */
  g_file_copy_async (pending->source, pending->destination,
      G_FILE_COPY_OVERWRITE, G_PRIORITY_DEFAULT_IDLE,
      NULL, NULL, NULL, done_saving_cb, pending);

  return FALSE;
}

static void
pending_recording_response (GtkWidget *dialog, int response, PendingRecording *pending)
{
  if (response == GTK_RESPONSE_ACCEPT) {
    char *dir;
    pending->destination = gtk_file_chooser_get_file (GTK_FILE_CHOOSER (dialog));
    dir = gtk_file_chooser_get_current_folder_uri (GTK_FILE_CHOOSER (dialog));
    if (dir) {
      panel_applet_gconf_set_string (pending->priv->applet, "save_directory", dir, NULL);
      g_free (dir);
    }
  }
  gtk_widget_destroy (dialog);
  gdk_threads_add_timeout_seconds (1, check_done_saving_cb, pending);
}

static void
pending_recording_launch (AppletPrivate *priv, ByzanzRecorder *rec, const char *tmp_file)
{
  PendingRecording *pending;
  GtkWidget *dialog;
  char *start_dir;
  
  pending = g_new0 (PendingRecording, 1);
  pending->priv = priv;
  pending->rec = rec;
  pending->source = g_file_new_for_path (tmp_file);
  
  dialog = gtk_file_chooser_dialog_new (_("Save Recorded File"),
      NULL, GTK_FILE_CHOOSER_ACTION_SAVE,
      GTK_STOCK_CANCEL, GTK_RESPONSE_CANCEL,
      GTK_STOCK_SAVE, GTK_RESPONSE_ACCEPT,
      NULL);
  gtk_file_chooser_set_local_only (GTK_FILE_CHOOSER (dialog), FALSE);
  start_dir = panel_applet_gconf_get_string (priv->applet, "save_directory", NULL);
  if (!start_dir || start_dir[0] == '\0' ||
      !gtk_file_chooser_set_current_folder_uri (GTK_FILE_CHOOSER (dialog), start_dir)) {
    gtk_file_chooser_set_current_folder (GTK_FILE_CHOOSER (dialog), g_get_home_dir ());
  }
  g_free (start_dir);
  gtk_file_chooser_set_do_overwrite_confirmation (GTK_FILE_CHOOSER (dialog), TRUE);
  gtk_dialog_set_default_response (GTK_DIALOG (dialog), GTK_RESPONSE_ACCEPT);
  g_signal_connect (dialog, "response", G_CALLBACK (pending_recording_response), pending);
  gtk_widget_show_all (dialog);
}

/*** APPLET ***/

static gboolean
byzanz_applet_is_recording (AppletPrivate *priv)
{
  return priv->tmp_file != NULL;
}

static gboolean
byzanz_applet_update (gpointer data)
{
  AppletPrivate *priv = data;

  if (byzanz_applet_is_recording (priv)) {
    /* applet is still actively recording the screen */
    gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (priv->button), TRUE);
    gtk_image_set_from_icon_name (GTK_IMAGE (priv->image), 
	GTK_STOCK_MEDIA_RECORD, GTK_ICON_SIZE_LARGE_TOOLBAR);
    gtk_tooltips_set_tip (priv->tooltips, priv->button,
	_("Stop current recording"),
	NULL);
  } else {
    gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (priv->button), FALSE);
    gtk_image_set_from_icon_name (GTK_IMAGE (priv->image), 
	byzanz_select_method_get_icon_name (priv->method), GTK_ICON_SIZE_LARGE_TOOLBAR);
    gtk_tooltips_set_tip (priv->tooltips, priv->button,
	byzanz_select_method_describe (priv->method),
	NULL);
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
  /* check for correct bpp */
  window = gdk_get_default_root_window ();
  if (gdk_drawable_get_depth (window) != 24 && 
      gdk_drawable_get_depth (window) != 32) {
    byzanz_applet_show_error (NULL, _("Cannot start recording."),
	_("Byzanz requires a 24bpp or 32bpp depth for recording. "
	  "The current resolution is set to %dbpp."), gdk_drawable_get_depth (window));
    goto out;
  }
  
  priv->tmp_file = (char *) "SELECTING"; /* so the rest of the world thinks we're recording */
  byzanz_applet_update (priv);
  window = byzanz_select_method_select (priv->method, &area); 
  priv->tmp_file = NULL;
  if (window) {
    int fd = g_file_open_tmp ("byzanzXXXXXX", &priv->tmp_file, NULL);
    if (fd > 0) 
      priv->rec = byzanz_recorder_new_fd (fd, window, &area, TRUE, 
	  gtk_check_menu_item_get_active (
	    GTK_CHECK_MENU_ITEM (priv->record_cursor)));
    if (!priv->rec) {
      close (fd);
      g_unlink (priv->tmp_file);
      g_free (priv->tmp_file);
      priv->tmp_file = NULL;
    }
    g_object_unref (window);
  }
  if (priv->rec) {
    byzanz_recorder_prepare (priv->rec);
    byzanz_recorder_start (priv->rec);
    g_get_current_time (&priv->start);
  }

out:
  byzanz_applet_update (priv);
}

static void
byzanz_applet_stop_recording (AppletPrivate *priv)
{
  char *tmp_file;
  ByzanzRecorder *rec;
  
  g_assert (byzanz_applet_is_recording (priv));
  
  byzanz_recorder_stop (priv->rec);
  tmp_file = priv->tmp_file;
  priv->tmp_file = NULL;
  rec = priv->rec;
  priv->rec = NULL;
  byzanz_applet_update (priv);
  pending_recording_launch (priv, rec, tmp_file);
  g_free (tmp_file);
}

static void
button_clicked_cb (GtkToggleButton *button, AppletPrivate *priv)
{
  gboolean active = gtk_toggle_button_get_active (button);
  
  if (byzanz_applet_is_recording (priv) && !active) {
    byzanz_applet_stop_recording (priv);
  } else if (!byzanz_applet_is_recording (priv) && active) {
    byzanz_applet_start_recording (priv);
  }
}

static void
destroy_applet (GtkWidget *widget, AppletPrivate *priv)
{
  if (byzanz_applet_is_recording (priv))
    byzanz_applet_stop_recording (priv);
  g_assert (!priv->rec); 
  g_free (priv);
}

static void
byzanz_applet_set_default_method (AppletPrivate *priv, int id, gboolean update_gconf)
{
  if (priv->method == id)
    return;
  if (id >= (gint) byzanz_select_get_method_count ())
    return;

  priv->method = id;
  byzanz_applet_update (priv);

  if (update_gconf)
    panel_applet_gconf_set_string (priv->applet, "method", 
	byzanz_select_method_get_name (id), NULL);
}

static void
byzanz_applet_method_selected_cb (GtkMenuItem *item, AppletPrivate *priv)
{
  gpointer tmp;
  int id;

  tmp = g_object_get_qdata (G_OBJECT (item), index_quark);
  id = GPOINTER_TO_INT (tmp);
  byzanz_applet_set_default_method (priv, id, TRUE);

  if (!byzanz_applet_is_recording (priv))
    byzanz_applet_start_recording (priv);
}

static void 
byzanz_about_cb (BonoboUIComponent *uic, AppletPrivate *priv, const char *verb)
{
  const gchar *authors[] = {
    "Benjamin Otte <otte@gnome.org>", 
    NULL
   };

  gtk_show_about_dialog( NULL,
    "name",                _("Desktop Recorder"), 
    "version",             VERSION,
    "copyright",           "\xC2\xA9 2005-2006 Benjamin Otte",
    "comments",            _("Record what's happening on your desktop"),
    "authors",             authors,
    "translator-credits",  _("translator-credits"),
    NULL );
}

static void
record_cursor_toggled_cb (GtkWidget *menuitem, AppletPrivate *priv)
{
  gboolean active = gtk_check_menu_item_get_active (
      GTK_CHECK_MENU_ITEM (priv->record_cursor));

  panel_applet_gconf_set_bool (priv->applet, "record_cursor", active, NULL);
}

static const BonoboUIVerb byzanz_menu_verbs [] = {
	BONOBO_UI_UNSAFE_VERB ("ByzanzAbout",      byzanz_about_cb),
        BONOBO_UI_VERB_END
};

static gboolean
byzanz_applet_fill (PanelApplet *applet, const gchar *iid, gpointer data)
{
  AppletPrivate *priv;
  guint i;
  char *method;
  GtkWidget *tmp;
  
  if (!index_quark)
    index_quark = g_quark_from_static_string ("Byzanz-Index");
#ifdef GETTEXT_PACKAGE
  bindtextdomain (GETTEXT_PACKAGE, LOCALEDIR);
  bind_textdomain_codeset (GETTEXT_PACKAGE, "UTF-8");
  textdomain (GETTEXT_PACKAGE);
#endif

  priv = g_new0 (AppletPrivate, 1);
  priv->applet = applet;
  g_signal_connect (applet, "destroy", G_CALLBACK (destroy_applet), priv);
  panel_applet_add_preferences (applet, "/schemas/apps/byzanz-applet/prefs",
      NULL);
  panel_applet_set_flags (applet, PANEL_APPLET_EXPAND_MINOR);
  panel_applet_setup_menu_from_file (PANEL_APPLET (applet), 
      DATADIR, "byzanzapplet.xml", NULL, byzanz_menu_verbs, priv);

  priv->tooltips = gtk_tooltips_new ();
  /* build menu */
  priv->menu = gtk_menu_new ();
  for (i = 0; i < byzanz_select_get_method_count (); i++) {
    GtkWidget *menuitem, *image;

    menuitem = gtk_image_menu_item_new_with_mnemonic (
	byzanz_select_method_get_mnemonic (i));
    image = gtk_image_new_from_icon_name (
	byzanz_select_method_get_icon_name (i), GTK_ICON_SIZE_MENU);
    gtk_image_menu_item_set_image (GTK_IMAGE_MENU_ITEM (menuitem), image);
    g_object_set_qdata (G_OBJECT (menuitem), index_quark, GINT_TO_POINTER (i));
    g_signal_connect (menuitem, "activate", 
	G_CALLBACK (byzanz_applet_method_selected_cb), priv);
    gtk_menu_shell_append (GTK_MENU_SHELL (priv->menu), menuitem);
    gtk_widget_show (menuitem);
  }
  tmp = gtk_separator_menu_item_new ();
  gtk_widget_show (tmp);
  gtk_menu_shell_append (GTK_MENU_SHELL (priv->menu), tmp);
  /* translators: keep the mnemonic here different from the selection methods */
  priv->record_cursor = gtk_check_menu_item_new_with_mnemonic (
      _("Record _Mouse Cursor"));
  gtk_menu_shell_append (GTK_MENU_SHELL (priv->menu), priv->record_cursor);
  gtk_check_menu_item_set_active (GTK_CHECK_MENU_ITEM (priv->record_cursor),
      panel_applet_gconf_get_bool (priv->applet, "record_cursor", NULL));
  g_signal_connect (priv->record_cursor, "toggled", 
      G_CALLBACK (record_cursor_toggled_cb), priv);
  gtk_widget_show (priv->record_cursor);
  
  /* create UI */
  priv->dropdown = panel_dropdown_new ();
  gtk_container_add (GTK_CONTAINER (applet), priv->dropdown);
  panel_dropdown_set_popup_widget (PANEL_DROPDOWN (priv->dropdown), priv->menu);
  panel_dropdown_set_applet (PANEL_DROPDOWN (priv->dropdown), priv->applet);

  method = panel_applet_gconf_get_string (priv->applet, "method", NULL);
  priv->method = byzanz_select_method_lookup (method);
  g_free (method);
  if (priv->method < 0)
    priv->method = 0;

  priv->button = panel_toggle_button_new ();
  priv->image = gtk_image_new ();
  gtk_container_add (GTK_CONTAINER (priv->button), priv->image);
  g_signal_connect (priv->button, "toggled", G_CALLBACK (button_clicked_cb), priv);
  gtk_container_add (GTK_CONTAINER (priv->dropdown), priv->button);

  byzanz_applet_update (priv);
  gtk_widget_show_all (GTK_WIDGET (applet));
  
  return TRUE;
}

PANEL_APPLET_BONOBO_FACTORY ("OAFIID:ByzanzApplet_Factory",
    PANEL_TYPE_APPLET, "ByzanzApplet", "0",
    byzanz_applet_fill, NULL);

