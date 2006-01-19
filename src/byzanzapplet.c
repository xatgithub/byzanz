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
#include <glib/gstdio.h>
#include <libgnomevfs/gnome-vfs.h>
#include <libgnomeui/libgnomeui.h>
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
  ByzanzRecorder *	rec;
  GnomeVFSAsyncHandle *	handle;
  char *		tmp_file;
  GnomeVFSURI *		target;
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
  dialog = gtk_message_dialog_new (parent, GTK_DIALOG_MODAL, GTK_MESSAGE_ERROR,
      GTK_BUTTONS_CLOSE, error ? error : msg);
  if (error)
    gtk_message_dialog_format_secondary_text (GTK_MESSAGE_DIALOG (dialog), msg);
  gtk_dialog_run (GTK_DIALOG (dialog));
  gtk_widget_destroy (dialog);
  g_free (msg);
}

static void
pending_recording_destroy (PendingRecording *pending)
{
  g_assert (pending->rec == NULL);

  if (pending->tmp_file) {
    g_unlink (pending->tmp_file);
    g_free (pending->tmp_file);
  }
  if (pending->target)
    gnome_vfs_uri_unref (pending->target);
  g_free (pending);
  gtk_main_quit ();
}

static int
transfer_progress_cb (GnomeVFSAsyncHandle *handle, GnomeVFSXferProgressInfo *info, gpointer data)
{
  PendingRecording *pending = data;
  char *target_uri;

  switch (info->status) {
    case GNOME_VFS_XFER_PROGRESS_STATUS_VFSERROR:
      target_uri = gnome_vfs_uri_to_string (pending->target, GNOME_VFS_URI_HIDE_PASSWORD | GNOME_VFS_URI_HIDE_FRAGMENT_IDENTIFIER);
      byzanz_applet_show_error (NULL, _("A file could not be saved."),
	  _("\"%s\" could not be saved.\nThe error that occured was: %s"), 
	    target_uri, gnome_vfs_result_to_string (info->vfs_status));
      g_free (target_uri);
      return 0;
    case GNOME_VFS_XFER_PROGRESS_STATUS_OK:
      if (info->phase == GNOME_VFS_XFER_PHASE_COMPLETED) {
	g_free (pending->tmp_file);
	pending->tmp_file = NULL;
	pending_recording_destroy (pending);
	gtk_main_quit ();
	return 0;
      }
      return 1;
    case GNOME_VFS_XFER_PROGRESS_STATUS_OVERWRITE:
    case GNOME_VFS_XFER_PROGRESS_STATUS_DUPLICATE:
    default:
      g_assert_not_reached ();
      return 0;
  }
}

static gboolean
check_done_saving_cb (gpointer data)
{
  PendingRecording *pending = data;
  GList *src, *target;
  GnomeVFSURI *src_uri;

  if (byzanz_recorder_is_active (pending->rec))
    return TRUE;
  byzanz_recorder_destroy (pending->rec);
  pending->rec = NULL;

  if (pending->target == NULL) {
    pending_recording_destroy (pending);
    return FALSE;
  }
  src_uri = gnome_vfs_uri_new (pending->tmp_file);
  src = g_list_prepend (NULL, src_uri);
  target = g_list_prepend (NULL, pending->target);
  gnome_vfs_async_xfer (&pending->handle, src, target,
      GNOME_VFS_XFER_REMOVESOURCE | GNOME_VFS_XFER_TARGET_DEFAULT_PERMS,
      GNOME_VFS_XFER_ERROR_MODE_QUERY,
      GNOME_VFS_XFER_OVERWRITE_MODE_REPLACE, /* we do overwrite confirmation in the dialog */
      GNOME_VFS_PRIORITY_DEFAULT,
      transfer_progress_cb, pending, NULL, NULL);

  g_list_free (src);
  g_list_free (target);
  gnome_vfs_uri_unref (src_uri);
  return FALSE;
}

static void
pending_recording_launch (AppletPrivate *priv, ByzanzRecorder *rec, char *tmp_file)
{
  PendingRecording *pending;
  GtkWidget *dialog;
  char *start_dir, *end_dir;
  
  pending = g_new0 (PendingRecording, 1);
  pending->rec = rec;
  pending->tmp_file = tmp_file;
  
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
  gtk_file_chooser_set_do_overwrite_confirmation (GTK_FILE_CHOOSER (dialog), TRUE);
  gtk_dialog_set_default_response (GTK_DIALOG (dialog), GTK_RESPONSE_ACCEPT);
  if (GTK_RESPONSE_ACCEPT == gtk_dialog_run (GTK_DIALOG (dialog))) {
    char *target_uri = gtk_file_chooser_get_uri (GTK_FILE_CHOOSER (dialog));
    pending->target = gnome_vfs_uri_new (target_uri);
    end_dir = gtk_file_chooser_get_current_folder_uri (GTK_FILE_CHOOSER (dialog));
    if (end_dir && 
	(!start_dir || !g_str_equal (end_dir, start_dir))) {
      panel_applet_gconf_set_string (priv->applet, "save_directory", end_dir, NULL);
    }
    g_free (end_dir);
  }
  g_free (start_dir);
  gtk_widget_destroy (dialog);
  g_timeout_add (1000, check_done_saving_cb, pending);
  gtk_main ();
  return;
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
  
  priv->tmp_file = "SELECTING"; /* so the rest of the world thinks we're recording */
  byzanz_applet_update (priv);
  window = byzanz_select_method_select (priv->method, &area); 
  priv->tmp_file = NULL;
  if (window) {
    int fd = g_file_open_tmp ("byzanzXXXXXX", &priv->tmp_file, NULL);
    if (fd > 0) 
      priv->rec = byzanz_recorder_new_fd (fd, window, &area, TRUE);
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
  int id;

  id = GPOINTER_TO_INT (g_object_get_qdata (G_OBJECT (item), index_quark));
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
  
  gnome_vfs_init ();
  gnome_authentication_manager_init ();
  if (!index_quark)
    index_quark = g_quark_from_static_string ("Byzanz-Index");

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

