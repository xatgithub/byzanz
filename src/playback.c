/* desktop session recorder
 * Copyright (C) 2009 Benjamin Otte <otte@gnome.org>
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

#include "byzanzencoder.h"
#include "byzanzserialize.h"

static GOptionEntry entries[] = 
{
  { NULL }
};

static void
usage (void)
{
  g_print (_("usage: %s [OPTIONS] INFILE OUTFILE\n"), g_get_prgname ());
  g_print (_("       %s --help\n"), g_get_prgname ());
}

typedef struct {
  GFile *               infile;
  GFile *               outfile;
  GInputStream *        instream;
  GOutputStream *       outstream;
  GMainLoop *           loop;
  ByzanzEncoder *       encoder;
} Operation;

static void
encoder_notify (ByzanzEncoder *encoder, GParamSpec *pspec, Operation *op)
{
  const GError *error;

  error = byzanz_encoder_get_error (encoder);
  if (error) {
    g_print ("%s\n", error->message);
    g_main_loop_quit (op->loop);
  } else if (!byzanz_encoder_is_running (encoder)) {
    g_main_loop_quit (op->loop);
  }
}

int
main (int argc, char **argv)
{
  GOptionContext* context;
  GError *error = NULL;
  Operation op;
  
  g_set_prgname (argv[0]);
#ifdef GETTEXT_PACKAGE
  bindtextdomain (GETTEXT_PACKAGE, GNOMELOCALEDIR);
  bind_textdomain_codeset (GETTEXT_PACKAGE, "UTF-8");
  textdomain (GETTEXT_PACKAGE);
#endif

  g_thread_init (NULL);
  g_type_init ();

  context = g_option_context_new (_("Process a byzanz debug file"));
#ifdef GETTEXT_PACKAGE
  g_option_context_set_translation_domain(context, GETTEXT_PACKAGE);
#endif

  g_option_context_add_main_entries (context, entries, GETTEXT_PACKAGE);
  if (!g_option_context_parse (context, &argc, &argv, &error)) {
    g_print (_("Wrong option: %s\n"), error->message);
    usage ();
    g_error_free (error);
    return 1;
  }
  if (argc != 3) {
    usage ();
    return 0;
  }

  op.infile = g_file_new_for_commandline_arg (argv[1]);
  op.outfile = g_file_new_for_commandline_arg (argv[2]);
  op.loop = g_main_loop_new (NULL, FALSE);

  op.instream = G_INPUT_STREAM (g_file_read (op.infile, NULL, &error));
  if (op.instream == NULL) {
    g_print ("%s\n", error->message);
    g_error_free (error);
    return 1;
  }
  op.outstream = G_OUTPUT_STREAM (g_file_replace (op.outfile, NULL, 
        FALSE, G_FILE_CREATE_REPLACE_DESTINATION, NULL, &error));
  if (op.outstream == NULL) {
    g_print ("%s\n", error->message);
    g_error_free (error);
    return 1;
  }
  op.encoder = byzanz_encoder_new (byzanz_encoder_get_type_from_file (op.outfile),
      op.instream, op.outstream, NULL);
  
  g_signal_connect (op.encoder, "notify", G_CALLBACK (encoder_notify), &op);
  
  g_main_loop_run (op.loop);

  g_main_loop_unref (op.loop);
  g_object_unref (op.instream);
  g_object_unref (op.outstream);
  g_object_unref (op.infile);
  g_object_unref (op.outfile);

  return 0;
}
