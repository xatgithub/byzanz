noinst_LTLIBRARIES = libbyzanz.la
bin_PROGRAMS = byzanz-record
libexec_PROGRAMS = byzanz-applet

noinst_HEADERS = \
	byzanzrecorder.h \
	byzanzselect.h \
	paneldropdown.h \
	panelstuffer.h \
	paneltogglebutton.h

libbyzanz_la_SOURCES = \
	byzanzrecorder.c \
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
	panelstuffer.c \
	paneltogglebutton.c

byzanz_applet_CFLAGS = $(APPLET_CFLAGS) -I$(top_srcdir)/gifenc
byzanz_applet_LDADD = $(APPLET_LIBS) ./libbyzanz.la

serverdir       = $(libdir)/bonobo/servers
server_in_files = ByzanzApplet.server.in
server_DATA     = $(server_in_files:.server.in=.server)

$(server_in_files): $(server_in_files:.server.in=.server.in.in)
	sed -e "s|\@LIBEXECDIR\@|$(libexecdir)|" $< > $@

CLEANFILES = $(server_in_files) $(server_DATA) 

EXTRA_DIST = \
	ByzanzApplet.server.in.in \
	$(schemas_in_files)


@INTLTOOL_SERVER_RULE@
