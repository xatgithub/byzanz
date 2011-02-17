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

byzanz_applet_CFLAGS = -DBYZANZ_MENU_UI_DIR=\""$(uidir)"\" $(APPLET_CFLAGS)
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

applet_in_files = org.gnome.ByzanzApplet.panel-applet.in
applet_DATA     = $(applet_in_files:.panel-applet.in=.panel-applet)

$(applet_in_files): $(applet_in_files).in Makefile
	sed \
            -e "s|\@LIBEXECDIR\@|$(libexecdir)|" \
            -e "s|\@VERSION\@|$(PACKAGE_VERSION)|" \
            $< > $@

%.panel-applet: %.panel-applet.in $(INTLTOOL_MERGE) $(wildcard $(top_srcdir)/po/*po) ; $(INTLTOOL_MERGE) $(top_srcdir)/po $< $@ -d -u -c $(top_builddir)/po/.intltool-merge-cache

servicedir       = $(datadir)/dbus-1/services
service_in_files = org.gnome.panel.applet.ByzanzAppletFactory.service.in
service_DATA     = $(service_in_files:.service.in=.service)

org.gnome.panel.applet.ByzanzAppletFactory.service: $(service_in_files)
	sed \
	    -e "s|\@LIBEXECDIR\@|$(libexecdir)|" \
	    $< > $@

uidir = $(datadir)/gnome-2.0/ui
ui_DATA = byzanzapplet.xml

CLEANFILES = $(applet_DATA) $(applet_DATA).in $(service_DATA) $(schemas_DATA) $(BUILT_SOURCES)

EXTRA_DIST = \
	byzanzmarshal.list \
	org.gnome.ByzanzApplet.panel-applet.in.in \
	$(man_MANS) \
	$(ui_DATA) \
	$(schemas_in_files)


