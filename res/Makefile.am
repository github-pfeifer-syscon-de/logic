PKGCONFIG = pkgconf
GLIB_COMPILE_RESOURCES = $(shell $(PKGCONFIG) --variable=glib_compile_resources gio-2.0)

GRESOURCE_XML=$(srcdir)/gresource.xml
LOGIC_RESOURCES = $(shell $(GLIB_COMPILE_RESOURCES) --sourcedir=$(srcdir) --generate-dependencies $(GRESOURCE_XML))
RESOURCE_C=$(top_srcdir)/src/resources.c

all: $(RESOURCE_C)

$(RESOURCE_C): $(GRESOURCE_XML) $(LOGIC_RESOURCES)
	$(GLIB_COMPILE_RESOURCES) --target=$@ --sourcedir=$(srcdir) --generate-source $<

# The desktop files
desktopdir = $(datadir)/applications
desktop_DATA=logic.desktop

# application icon, using only png, as there is no meaningful svg representation
appicondir=$(datadir)/icons/hicolor/64x64/apps
appicon_DATA=logic.png

EXTRA_DIST = $(LOGIC_RESOURCES) $(GRESOURCE_XML) $(desktop_DATA) $(appicon_DATA)

clean-local:
	-rm -rf $(RESOURCE_C)
