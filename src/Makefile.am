bin_PROGRAMS = record

record_SOURCES = \
	byzanzrecorder.c \
	record.c

noinst_HEADERS = \
	byzanzrecorder.h
		 
record_CFLAGS = $(BYZANZ_CFLAGS) -I$(top_srcdir)/gifenc
record_LDADD = $(BYZANZ_LIBS) $(top_builddir)/gifenc/libgifenc.la


libexec_PROGRAMS = byzanz-applet

byzanz_applet_SOURCES = \
	byzanzapplet.c \
	byzanzrecorder.c

byzanz_applet_CFLAGS = $(APPLET_CFLAGS) -I$(top_srcdir)/gifenc
byzanz_applet_LDADD = $(APPLET_LIBS) $(top_builddir)/gifenc/libgifenc.la

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
