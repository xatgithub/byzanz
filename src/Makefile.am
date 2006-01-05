bin_PROGRAMS = record

record_SOURCES = \
	byzanzrecorder.c \
	record.c

noinst_HEADERS = \
	byzanzrecorder.h
		 
record_CFLAGS = $(BYZANZ_CFLAGS) -I$(top_srcdir)/gifenc
record_LDADD = $(BYZANZ_LIBS) $(top_builddir)/gifenc/libgifenc.la
