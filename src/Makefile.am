noinst_LTLIBRARIES = libbyzanz.la
bin_PROGRAMS = byzanz-record byzanz-playback
libexec_PROGRAMS = byzanz-applet
man_MANS = byzanz-record.1 byzanz-playback.1

BUILT_SOURCES = \
	byzanzmarshal.h \
	byzanzmarshal.c

noinst_HEADERS = \
	byzanzencoder.h \
	byzanzencoderbyzanz.h \
	byzanzencoderflv.h \
	byzanzencodergif.h \
	byzanzencodergstreamer.h \
	byzanzencoderogv.h \
	byzanzencoderwebm.h \
	byzanzlayer.h \
	byzanzlayercursor.h \
	byzanzlayerwindow.h \
	byzanzqueue.h \
	byzanzqueueinputstream.h \
	byzanzqueueoutputstream.h \
	byzanzrecorder.h \
	byzanzsession.h \
	byzanzselect.h \
	byzanzserialize.h \
	paneltogglebutton.h \
	screenshot-utils.h

libbyzanz_la_SOURCES = \
	byzanzencoder.c \
	byzanzencoderbyzanz.c \
	byzanzencoderflv.c \
	byzanzencodergif.c \
	byzanzencodergstreamer.c \
	byzanzencoderogv.c \
	byzanzencoderwebm.c \
	byzanzlayer.c \
	byzanzlayercursor.c \
	byzanzlayerwindow.c \
	byzanzmarshal.c \
	byzanzqueue.c \
	byzanzqueueinputstream.c \
	byzanzqueueoutputstream.c \
	byzanzrecorder.c \
	byzanzsession.c \
	byzanzselect.c \
	byzanzserialize.c

libbyzanz_la_CFLAGS = $(BYZANZ_CFLAGS) -I$(top_srcdir)/gifenc
libbyzanz_la_LIBADD = $(BYZANZ_LIBS) $(top_builddir)/gifenc/libgifenc.la
		 
byzanz_playback_SOURCES = \
	playback.c

byzanz_playback_CFLAGS = $(BYZANZ_CFLAGS)
byzanz_playback_LDADD = $(BYZANZ_LIBS) ./libbyzanz.la


byzanz_record_SOURCES = \
	record.c

byzanz_record_CFLAGS = $(BYZANZ_CFLAGS)
byzanz_record_LDADD = $(BYZANZ_LIBS) ./libbyzanz.la


byzanz_applet_SOURCES = \
	byzanzapplet.c \
	paneltogglebutton.c \
	screenshot-utils.c

byzanz_applet_CFLAGS = $(APPLET_CFLAGS)
byzanz_applet_LDADD = $(APPLET_LIBS) ./libbyzanz.la


byzanzmarshal.h: byzanzmarshal.list
	$(GLIB_GENMARSHAL) --prefix=byzanz_marshal $(srcdir)/byzanzmarshal.list --header > byzanzmarshal.h

byzanzmarshal.c: byzanzmarshal.list
	(echo "#include \"byzanzmarshal.h\""; \
	 $(GLIB_GENMARSHAL) --prefix=byzanz_marshal $(srcdir)/byzanzmarshal.list --body) > byzanzmarshal.c


schemasdir   = @GCONF_SCHEMA_FILE_DIR@
schemas_in_files = byzanz.schemas.in
schemas_DATA = $(schemas_in_files:.schemas.in=.schemas)

@INTLTOOL_SCHEMAS_RULE@

if GCONF_SCHEMAS_INSTALL
install-data-local:
	GCONF_CONFIG_SOURCE=$(GCONF_SCHEMA_CONFIG_SOURCE) $(GCONFTOOL) --makefile-install-rule $(schemas_DATA) ;
endif

serverdir       = $(libdir)/bonobo/servers
server_in_files = ByzanzApplet.server.in
server_DATA     = $(server_in_files:.server.in=.server)

$(server_in_files): $(server_in_files:.server.in=.server.in.in)
	sed -e "s|\@LIBEXECDIR\@|$(libexecdir)|" $< > $@

uidir = $(datadir)/gnome-2.0/ui
ui_DATA = byzanzapplet.xml

CLEANFILES = $(server_in_files) $(server_DATA) $(schemas_DATA) $(BUILT_SOURCES)

EXTRA_DIST = \
	byzanzmarshal.list \
	ByzanzApplet.server.in.in \
	$(man_MANS) \
	$(ui_DATA) \
	$(schemas_in_files)


@INTLTOOL_SERVER_RULE@
