noinst_LTLIBRARIES = libbyzanz.la
bin_PROGRAMS = byzanz-record
libexec_PROGRAMS = byzanz-applet
man_MANS = byzanz-record.1

noinst_HEADERS = \
	byzanzsession.h \
	byzanzselect.h \
	paneldropdown.h \
	paneltogglebutton.h \
	screenshot-utils.h

libbyzanz_la_SOURCES = \
	byzanzsession.c \
	byzanzselect.c

libbyzanz_la_CFLAGS = $(BYZANZ_CFLAGS) -I$(top_srcdir)/gifenc
libbyzanz_la_LIBADD = $(BYZANZ_LIBS) $(top_builddir)/gifenc/libgifenc.la
		 
byzanz_record_SOURCES = \
	record.c

byzanz_record_CFLAGS = $(BYZANZ_CFLAGS) -I$(top_srcdir)/gifenc
byzanz_record_LDADD = $(BYZANZ_LIBS) ./libbyzanz.la


byzanz_applet_SOURCES = \
	byzanzapplet.c \
	paneldropdown.c \
	paneltogglebutton.c \
	screenshot-utils.c

byzanz_applet_CFLAGS = $(APPLET_CFLAGS) -I$(top_srcdir)/gifenc
byzanz_applet_LDADD = $(APPLET_LIBS) ./libbyzanz.la


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

CLEANFILES = $(server_in_files) $(server_DATA) $(schemas_DATA)

EXTRA_DIST = \
	ByzanzApplet.server.in.in \
	$(man_MANS) \
	$(ui_DATA) \
	$(schemas_in_files)


@INTLTOOL_SERVER_RULE@
