## Process this file with automake to produce Makefile.in

noinst_LTLIBRARIES = libbase.la

noinst_HEADERS = coda_largefile.h coda_string.h coda_wait.h coda_offsetof.h
libbase_la_SOURCES = base64.h base64.c coda_assert.h coda_assert.c \
    codaconf.c codaconf.h coda_flock.c coda_flock.h coda_hash.h md5c.c sha1.c \
    coda_getaddrinfo.h coda_getaddrinfo.c coda_getservbyname.h \
    coda_getservbyname.c coda_malloc.c copyfile.h copyfile.c dllist.h dllist.c \
    getpeereid.h getpeereid.c \
    parse_realms.h parse_realms.c parser.h parser.c urlquote.h urlquote.c

if WANT_FTS
libbase_la_SOURCES += coda_fts.h coda_fts.c
endif

AM_CPPFLAGS = $(RPC2_CFLAGS)

EXTRA_DIST = README.parser

