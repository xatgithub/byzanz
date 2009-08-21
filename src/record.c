/* desktop session
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

#include <glib/gi18n.h>

#include "byzanzsession.h"

static int duration = 10;
static int delay = 1;
static gboolean loop = FALSE;
static gboolean cursor = FALSE;
static gboolean verbose = FALSE;
static GdkRectangle area = { 0, 0, G_MAXINT / 2, G_MAXINT / 2 };

static GOptionEntry entries[] = 
{
  { "duration", 'd', 0, G_OPTION_ARG_INT, &duration, N_("Duration of animation (default: 10 seconds)"), N_("SECS") },
  { "delay", 0, 0, G_OPTION_ARG_INT, &delay, N_("Delay before start (default: 1 second)"), N_("SECS") },
  { "loop", 'l', 0, G_OPTION_ARG_NONE, &loop, N_("Let the animation loop"), NULL },
  { "cursor", 'c', 0, G_OPTION_ARG_NONE, &cursor, N_("Record mouse cursor"), NULL },
  { "x", 'x', 0, G_OPTION_ARG_INT, &area.x, N_("X coordinate of rectangle to record"), N_("PIXEL") },
  { "y", 'y', 0, G_OPTION_ARG_INT, &area.y, N_("Y coordinate of rectangle to record"), N_("PIXEL") },
  { "width", 'w', 0, G_OPTION_ARG_INT, &area.width, N_("Width of recording rectangle"), N_("PIXEL") },
  { "height", 'h', 0, G_OPTION_ARG_INT, &area.height, N_("Height of recording rectangle"), N_("PIXEL") },
  { "verbose", 'v', 0, G_OPTION_ARG_NONE, &verbose, N_("Be verbose"), NULL },
  { NULL }
};

static void
verbose_print (const gchar *format, ...)
{
  gchar *buffer;
  va_list args;

  if (!verbose)
    return;
  
  va_start (args, format);
  buffer = g_strdup_vprintf (format, args);
  va_end (args);
  g_print ("%s", buffer);
}

static void
usage (void)
{
  g_print (_("usage: %s [OPTIONS] filename\n"), g_get_prgname ());
  g_print (_("       %s --help\n"), g_get_prgname ());
}

static gboolean
stop_recording (gpointer session)
{
  verbose_print (_("Recording done. Cleaning up...\n"));
  byzanz_session_stop (session);
  gtk_main_quit ();
  
  return FALSE;
}

static gboolean
start_recording (gpointer session)
{
  verbose_print (_("Recording starts. Will record %d seconds...\n"), duration / 1000);
  byzanz_session_start (session);
  g_timeout_add (duration, stop_recording, session);
  
  return FALSE;
}

int
main (int argc, char **argv)
{
  ByzanzSession *rec;
  GOptionContext* context;
  GError *error = NULL;
  GFile *file;
  
  g_set_prgname (argv[0]);
#ifdef GETTEXT_PACKAGE
  bindtextdomain (GETTEXT_PACKAGE, LOCALEDIR);
  bind_textdomain_codeset (GETTEXT_PACKAGE, "UTF-8");
  textdomain (GETTEXT_PACKAGE);
#endif

  g_thread_init (NULL);
  context = g_option_context_new (_("record your current desktop session"));
#ifdef GETTEXT_PACKAGE
  g_option_context_set_translation_domain(context, GETTEXT_PACKAGE);
#endif

  g_option_context_add_main_entries (context, entries, GETTEXT_PACKAGE);
  g_option_context_add_group (context, gtk_get_option_group (TRUE));
  if (!g_option_context_parse (context, &argc, &argv, &error)) {
    g_print (_("Wrong option: %s\n"), error->message);
    usage ();
    return 1;
  }
  if (argc != 2) {
    usage ();
    return 0;
  }
  file = g_file_new_for_commandline_arg (argv[1]);
  rec = byzanz_session_new (file, gdk_get_default_root_window (),
      &area, loop, cursor);
  g_object_unref (file);
  if (rec == NULL) {
    g_print (_("Could not prepare recording.\n"
	  "Most likely the Damage extension is not available on the X server "
	  "or the file \"%s\" is not writable.\n"), 
	argv[1]);
    return 2;
  }
  delay = MAX (delay, 1);
  delay = (delay - 1) * 1000;
  duration = MAX (duration, 0);
  duration *= 1000;
  g_timeout_add (delay, start_recording, rec);
  
  gtk_main ();

  g_object_unref (rec);
  return 0;
}
