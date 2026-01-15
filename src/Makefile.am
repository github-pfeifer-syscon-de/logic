## Process this file with automake to produce Makefile.in

## Created by Anjuta



AM_CPPFLAGS = \
	-DPACKAGE_LOCALE_DIR=\""$(localedir)"\" \
	-DPACKAGE_SRC_DIR=\""$(srcdir)"\" \
	-DPACKAGE_DATA_DIR=\""$(pkgdatadir)"\" \
	$(GTKMM_CFLAGS) \
	$(GLIBMM_CFLAGS) \
	$(GTHREAD_CLFAGS) 

AM_CFLAGS =\
	 -Wall\
	 -g

bin_PROGRAMS = logic

logic_SOURCES =  \
	Capture.cpp \
	Capture.hpp \
	DeviceCapture.cpp \
	DeviceCapture.hpp \
	LogicApp.cpp \
	LogicApp.hpp \
	LogicDrawing.cpp \
	LogicDrawing.hpp \
	LogicWindow.cpp \
	LogicWindow.hpp \
	SetupAssist.cpp \
	SetupAssist.hpp \
	Timescale.cpp \
	Timescale.hpp \
	UserCapture.cpp \
	UserCapture.hpp \
	bcm2835_gpio.c \
	bcm2835_gpio.h \
	resources.c \
	ModelColums.cpp \
	ModelColums.hpp \
	DisplayTree.cpp \
	DisplayTree.hpp

logic_CXXFLAGS = \
	 -Wall\
	 -g

logic_LDFLAGS = 

logic_LDADD = \
	$(GTKMM_LIBS) \
	$(GLIBMM_LIBS) \
	$(GTHREAD_LIBS) 

EXTRA_DIST = $(ui_DATA)

# Remove ui directory on uninstall
uninstall-local:
	-rm -r $(uidir)
	-rm -r $(pkgdatadir)

