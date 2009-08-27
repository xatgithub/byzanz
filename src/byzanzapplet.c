/* desktop session
 * Copyright (C) 2005,2009 Benjamin Otte <otte@gnome.org>
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
#include "paneltogglebutton.h"
#include <glib/gi18n.h>

#include "byzanzencoder.h"
#include "byzanzselect.h"
#include "byzanzsession.h"

static GQuark index_quark = 0;

typedef struct {
  PanelApplet *		applet;		/* the applet we manage */

  GtkWidget *		button;		/* recording button */
  GtkWidget *		image;		/* image displayed in button */
  GtkTooltips *		tooltips;	/* our tooltips */
  GtkWidget *           dialog;         /* file chooser */
  
  ByzanzSession *	rec;		/* the session (if recording) */

  /* config */
  int			method;		/* recording method that was set */
} AppletPrivate;
#define APPLET_IS_RECORDING(applet) ((applet)->tmp_file != NULL)

/*** PENDING RECORDING ***/

static void
byzanz_applet_show_error (AppletPrivate *priv, const char *error, const char *details, ...)
{
  GtkWidget *dialog, *parent;
  gchar *msg;
  va_list args;

  g_return_if_fail (details != NULL);
  
  va_start (args, details);
  msg = g_strdup_vprintf (details, args);
  va_end (args);
  if (priv)
    parent = gtk_widget_get_toplevel (GTK_WIDGET (priv->applet));
  else
    parent = NULL;
  dialog = gtk_message_dialog_new (GTK_WINDOW (parent), GTK_DIALOG_DESTROY_WITH_PARENT, GTK_MESSAGE_ERROR,
      GTK_BUTTONS_CLOSE, "%s", error ? error : msg);
  if (parent == NULL)
    gtk_window_set_icon_name (GTK_WINDOW (dialog), "byzanz-record-desktop");
  if (error)
    gtk_message_dialog_format_secondary_text (GTK_MESSAGE_DIALOG (dialog), "%s", msg);
  g_free (msg);
  g_signal_connect (dialog, "response", G_CALLBACK (gtk_widget_destroy), NULL);
  gtk_widget_show_all (dialog);
}

/*** APPLET ***/

static gboolean
byzanz_applet_update (gpointer data)
{
  AppletPrivate *priv = data;

  if (priv->rec == NULL) {
    gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (priv->button), FALSE);
    gtk_image_set_from_icon_name (GTK_IMAGE (priv->image), 
	GTK_STOCK_MEDIA_RECORD, GTK_ICON_SIZE_LARGE_TOOLBAR);
    gtk_tooltips_set_tip (priv->tooltips, priv->button,
	_("Start a new recording"), NULL);
  } else {
    gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (priv->button), TRUE);
    if (byzanz_session_is_recording (priv->rec)) {
      gtk_image_set_from_icon_name (GTK_IMAGE (priv->image), 
          GTK_STOCK_MEDIA_STOP, GTK_ICON_SIZE_LARGE_TOOLBAR);
      gtk_tooltips_set_tip (priv->tooltips, priv->button,
          _("End current recording"), NULL);
    } else if (byzanz_session_is_encoding (priv->rec)) {
      gtk_image_set_from_icon_name (GTK_IMAGE (priv->image), 
          GTK_STOCK_CANCEL, GTK_ICON_SIZE_LARGE_TOOLBAR);
      gtk_tooltips_set_tip (priv->tooltips, priv->button,
          _("Abort encoding process"), NULL);
    } else {
      g_assert_not_reached ();
    }
  }
  
  return TRUE;
}

static void
byzanz_applet_set_default_method (AppletPrivate *priv, int id)
{
  if (priv->method == id)
    return;
  if (id >= (gint) byzanz_select_get_method_count ())
    return;

  priv->method = id;

  panel_applet_gconf_set_string (priv->applet, "method", 
      byzanz_select_method_get_name (id), NULL);
}

static void
byzanz_applet_session_notify (AppletPrivate *priv)
{
  const GError *error;

  g_assert (priv->rec != NULL);

  error = byzanz_session_get_error (priv->rec);
  if (error) {
    byzanz_applet_show_error (priv, error->message, NULL);
    g_object_unref (priv->rec);
    priv->rec = NULL;
  } else if (!byzanz_session_is_encoding (priv->rec)) {
    g_object_unref (priv->rec);
    priv->rec = NULL;
  }

  byzanz_applet_update (priv);
}

static int method_response_codes[] = { GTK_RESPONSE_ACCEPT, GTK_RESPONSE_APPLY, GTK_RESPONSE_OK, GTK_RESPONSE_YES };

static void
panel_applet_start_response (GtkWidget *dialog, int response, AppletPrivate *priv)
{
  GFile *file;
  guint i;
  GdkWindow *window;
  GdkRectangle area;
  GType encoder_type;
  GtkFileFilter *filter;

  file = gtk_file_chooser_get_file (GTK_FILE_CHOOSER (dialog));
  if (file == NULL)
    goto out;

  for (i = 0; i < byzanz_select_get_method_count (); i++) {
    if (response == method_response_codes[i]) {
      char *uri = g_file_get_uri (file);
      panel_applet_gconf_set_string (priv->applet, "save_filename", uri, NULL);
      g_free (uri);
      byzanz_applet_set_default_method (priv, i);
      break;
    }
  }
  if (i >= byzanz_select_get_method_count ())
    goto out;

  filter = gtk_file_chooser_get_filter (GTK_FILE_CHOOSER (priv->dialog));
  if (filter && gtk_file_filter_get_needed (filter) != 0) {
    encoder_type = byzanz_encoder_get_type_from_filter (filter);
  } else {
    /* It's the "All files" filter */
    encoder_type = byzanz_encoder_get_type_from_file (file);
  }

  gtk_widget_destroy (dialog);
  priv->dialog = NULL;
  byzanz_applet_update (priv);

  window = byzanz_select_method_select (priv->method, &area); 
  if (window == NULL)
    goto out2;

  priv->rec = byzanz_session_new (file, encoder_type, window, &area, TRUE);
  g_signal_connect_swapped (priv->rec, "notify", G_CALLBACK (byzanz_applet_session_notify), priv);
  
  byzanz_session_start (priv->rec);
  byzanz_applet_session_notify (priv);
  g_object_unref (file);
  return;

out:
  gtk_widget_destroy (dialog);
  priv->dialog = NULL;
out2:
  if (file)
    g_object_unref (file);
  if (priv->rec)
    byzanz_applet_session_notify (priv);
  else
    byzanz_applet_update (priv);
}

static void
byzanz_applet_start_recording (AppletPrivate *priv)
{
  if (priv->rec)
    goto out;
  
  if (priv->dialog == NULL) {
    char *uri;
    guint i;
    GType type;
    ByzanzEncoderIter iter;
    GtkFileFilter *filter;

    priv->dialog = gtk_file_chooser_dialog_new (_("Record your desktop"),
        GTK_WINDOW (gtk_widget_get_toplevel (GTK_WIDGET (priv->applet))),
        GTK_FILE_CHOOSER_ACTION_SAVE, GTK_STOCK_CANCEL, GTK_RESPONSE_CANCEL,
        NULL);
    g_assert (G_N_ELEMENTS (method_response_codes) >= byzanz_select_get_method_count ());
    for (i = 0; i < byzanz_select_get_method_count (); i++) {
      gtk_dialog_add_button (GTK_DIALOG (priv->dialog),
          byzanz_select_method_get_mnemonic (i), method_response_codes[i]);
    }
    filter = gtk_file_filter_new ();
    gtk_file_filter_set_name (filter, _("All files"));
    gtk_file_filter_add_custom (filter, 0, (GtkFileFilterFunc) gtk_true, NULL, NULL);
    gtk_file_chooser_add_filter (GTK_FILE_CHOOSER (priv->dialog), filter);
    for (type = byzanz_encoder_type_iter_init (&iter);
         type != G_TYPE_NONE;
         type = byzanz_encoder_type_iter_next (&iter)) {
      filter = byzanz_encoder_type_get_filter (type);
      if (filter) {
        gtk_file_chooser_add_filter (GTK_FILE_CHOOSER (priv->dialog), filter);
        g_object_unref (filter);
      }
    }
    gtk_file_chooser_set_local_only (GTK_FILE_CHOOSER (priv->dialog), FALSE);
    uri = panel_applet_gconf_get_string (priv->applet, "save_filename", NULL);
    if (!uri || uri[0] == '\0' ||
        !gtk_file_chooser_set_uri (GTK_FILE_CHOOSER (priv->dialog), uri)) {
      g_free (uri);
      /* Try the key used by old versions. Maybe it's still set. */
      uri = panel_applet_gconf_get_string (priv->applet, "save_directory", NULL);
      if (!uri || uri[0] == '\0' ||
	  !gtk_file_chooser_set_current_folder_uri (GTK_FILE_CHOOSER (priv->dialog), uri)) {
        gtk_file_chooser_set_current_folder (GTK_FILE_CHOOSER (priv->dialog), g_get_home_dir ());
      }
    }
    g_free (uri);
    gtk_file_chooser_set_do_overwrite_confirmation (GTK_FILE_CHOOSER (priv->dialog), TRUE);
    g_signal_connect (priv->dialog, "response", G_CALLBACK (panel_applet_start_response), priv);

    gtk_widget_show_all (priv->dialog);

    /* need to show before setting the default response, otherwise the filechooser reshuffles it */
    gtk_dialog_set_default_response (GTK_DIALOG (priv->dialog), method_response_codes[priv->method]);
  }
  gtk_window_present (GTK_WINDOW (priv->dialog));

out:
  byzanz_applet_update (priv);
}

static void
button_clicked_cb (GtkToggleButton *button, AppletPrivate *priv)
{
  gboolean active = gtk_toggle_button_get_active (button);
  
  if (priv->rec && !active) {
    if (byzanz_session_is_recording (priv->rec))
      byzanz_session_stop (priv->rec);
    else
      byzanz_session_abort (priv->rec);
  } else if (priv->rec == NULL && active) {
    byzanz_applet_start_recording (priv);
  }
}

static void
destroy_applet (GtkWidget *widget, AppletPrivate *priv)
{
  if (priv->rec)
    g_object_unref (priv->rec);
  g_free (priv);
}

static void 
byzanz_about_cb (BonoboUIComponent *uic, AppletPrivate *priv, const char *verb)
{
  const gchar *authors[] = {
    "Benjamin Otte <otte@gnome.org>", 
    NULL
   };

  gtk_show_about_dialog( NULL,
    "name",                _("Desktop Session"), 
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
  char *method;
  
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

  method = panel_applet_gconf_get_string (priv->applet, "method", NULL);
  priv->method = byzanz_select_method_lookup (method);
  g_free (method);
  if (priv->method < 0)
    priv->method = 0;

  priv->button = panel_toggle_button_new ();
  priv->image = gtk_image_new ();
  gtk_container_add (GTK_CONTAINER (priv->button), priv->image);
  g_signal_connect (priv->button, "toggled", G_CALLBACK (button_clicked_cb), priv);
  gtk_container_add (GTK_CONTAINER (priv->applet), priv->button);

  byzanz_applet_update (priv);
  gtk_widget_show_all (GTK_WIDGET (applet));
  
  return TRUE;
}

PANEL_APPLET_BONOBO_FACTORY ("OAFIID:ByzanzApplet_Factory",
    PANEL_TYPE_APPLET, "ByzanzApplet", "0",
    byzanz_applet_fill, NULL);

