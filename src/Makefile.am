## Process this file with automake to produce Makefile.in

AM_CFLAGS = \
	-DPACKAGE_DATA_DIR=\""$(datadir)"\" \
	-DPACKAGE_LOCALE_DIR=\""$(prefix)/$(DATADIRNAME)/locale"\" \
	@PACKAGE_CFLAGS@ @GCONF_CFLAGS@ @LENSFUN_CFLAGS@ @LIBGPHOTO2_CFLAGS@ @DBUS_CFLAGS@ \
	-I$(top_srcdir)/librawstudio/ \
	-I$(top_srcdir)/

AM_CFLAGS +=\
	-Wall -fno-strict-aliasing\
	-DWITH_GCONF\
	-O4

AM_CXXFLAGS = $(AM_CFLAGS)

uidir = $(datadir)/rawstudio/
ui_DATA = ui.xml ui-client.xml

bin_PROGRAMS = rawstudio

EXTRA_DIST = \
	$(ui_DATA)

rawstudio_SOURCES = \
	application.c application.h \
	rs-actions.c rs-actions.h \
	gtk-helper.c gtk-helper.h \
	gtk-interface.c gtk-interface.h \
	rs-save-dialog.c rs-save-dialog.h \
	gtk-progress.c gtk-progress.h \
	rs-camera-db.c rs-camera-db.h \
	rs-cache.c rs-cache.h \
	rs-batch.c rs-batch.h \
	rs-toolbox.c rs-toolbox.h \
	rs-navigator.c rs-navigator.h \
	rs-photo.c rs-photo.h \
	filename.c filename.h \
	rs-store.c rs-store.h \
	rs-preview-widget.c rs-preview-widget.h \
	rs-loupe.c rs-loupe.h \
	rs-histogram.c rs-histogram.h \
	rs-pixbuf.c rs-pixbuf.h \
	rs-external-editor.c rs-external-editor.h \
	rs-dir-selector.c rs-dir-selector.h \
	rs-tag-gui.c rs-tag-gui.h\
	rs-tethered-shooting.c rs-tethered-shooting.h \
	rs-enfuse.c rs-enfuse.h

rawstudio_LDADD = ../librawstudio/librawstudio.la @PACKAGE_LIBS@ @GCONF_LIBS@ @LENSFUN_LIBS@ @LIBGPHOTO2_LIBS@ @DBUS_LIBS@ @SQLITE3_LIBS@ $(INTLLIBS)

