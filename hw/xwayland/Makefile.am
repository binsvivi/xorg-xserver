bin_PROGRAMS = Xwayland

Xwayland_CFLAGS =				\
	-I$(top_srcdir)/glamor			\
	-I$(top_srcdir)/dri3			\
	-I/opt/libhybris/include/               \
	-I/usr/include/android-29/		\
	-DHAVE_DIX_CONFIG_H			\
	$(XWAYLANDMODULES_CFLAGS)		\
	$(DIX_CFLAGS)				\
	$(GLAMOR_CFLAGS)			\
	$(GBM_CFLAGS)

Xwayland_SOURCES =				\
	xwayland.c				\
	xwayland-input.c			\
	xwayland-cursor.c			\
	xwayland-shm.c				\
	xwayland-output.c			\
	xwayland-cvt.c				\
	xwayland-vidmode.c			\
	xwayland.h				\
	$(top_srcdir)/Xi/stubs.c		\
	$(top_srcdir)/mi/miinitext.c

Xwayland_LDADD =				\
	$(glamor_lib)				\
	$(drihybris_lib)				\
	$(XWAYLAND_LIBS)			\
	$(top_builddir)/glx/libglxvnd.la	\
	$(XWAYLAND_SYS_LIBS)			\
	$(top_builddir)/Xext/libXvidmode.la	\
	$(XSERVER_SYS_LIBS)
Xwayland_LDFLAGS = $(LD_EXPORT_SYMBOLS_FLAG)    \
		   -shared -export-dynamic

Xwayland_built_sources =

if GLAMOR_EGL
Xwayland_SOURCES += 				\
	xwayland-glamor.c			\
	xwayland-present.c
if XV
Xwayland_SOURCES += 				\
	xwayland-glamor-xv.c
endif

if XWAYLAND_EGLSTREAM
Xwayland_SOURCES +=				\
	xwayland-glamor-eglstream.c
endif
Xwayland_SOURCES +=				\
	xwayland-glamor-hybris.c

glamor_built_sources =				\
	drm-client-protocol.h			\
	drm-protocol.c

hybris_built_sources = wayland-android-protocol.c \
                wayland-android-client-protocol.h

Xwayland_built_sources += $(glamor_built_sources)
Xwayland_built_sources += $(hybris_built_sources)

#drihybris_lib = /usr/local/lib/xorg/modules/libdrihybris.la
drihybris_lib = /usr/lib/aarch64-linux-gnu/xorg/modules/libdrihybris.la
glamor_lib = $(top_builddir)/glamor/libglamor.la
#glamor_lib = /usr/lib/xorg/modules/libglamoreglhybris.la
hybris_lib = /opt/libhybris/lib/aarch64-linux-gnu/
Xwayland_LDADD += $(GLAMOR_LIBS) $(GBM_LIBS) -L$(hybris_lib) -lgralloc -lhybris-eglplatformcommon -L$(hybris_lib)libhybris-egl/ -lEGL -lGL
#Xwayland_LDADD += $(GLAMOR_LIBS) $(GBM_LIBS) -L/usr/lib/xorg/modules/ -lglamoreglhybris -L$(hybris_lib) -lgralloc -L$(hybris_lib)libhybris-egl/ -lEGL -lGL
Xwayland_DEPENDENCIES = $(drihybris_lib) $(glamor_lib) $(XWAYLAND_LIBS)
endif

Xwayland_built_sources +=					\
	relative-pointer-unstable-v1-client-protocol.h		\
	relative-pointer-unstable-v1-protocol.c			\
	pointer-constraints-unstable-v1-client-protocol.h	\
	pointer-constraints-unstable-v1-protocol.c		\
	tablet-unstable-v2-client-protocol.h			\
	tablet-unstable-v2-protocol.c				\
	xwayland-keyboard-grab-unstable-v1-protocol.c		\
	xwayland-keyboard-grab-unstable-v1-client-protocol.h	\
	xdg-output-unstable-v1-protocol.c			\
	xdg-output-unstable-v1-client-protocol.h		\
	linux-dmabuf-unstable-v1-client-protocol.h		\
	linux-dmabuf-unstable-v1-protocol.c

if XWAYLAND_EGLSTREAM
Xwayland_built_sources +=					\
	wayland-eglstream-client-protocol.h			\
	wayland-eglstream-protocol.c				\
	wayland-eglstream-controller-client-protocol.h		\
	wayland-eglstream-controller-protocol.c
endif

nodist_Xwayland_SOURCES = $(Xwayland_built_sources)
CLEANFILES = $(Xwayland_built_sources)

EXTRA_DIST = drm.xml

$(Xwayland_SOURCES): $(Xwayland_built_sources)

relink:
	$(AM_V_at)rm -f Xwayland$(EXEEXT) && $(MAKE) Xwayland$(EXEEXT)

relative-pointer-unstable-v1-protocol.c : $(WAYLAND_PROTOCOLS_DATADIR)/unstable/relative-pointer/relative-pointer-unstable-v1.xml
	$(AM_V_GEN)$(WAYLAND_SCANNER) @SCANNER_ARG@ < $< > $@
relative-pointer-unstable-v1-client-protocol.h : $(WAYLAND_PROTOCOLS_DATADIR)/unstable/relative-pointer/relative-pointer-unstable-v1.xml
	$(AM_V_GEN)$(WAYLAND_SCANNER) client-header < $< > $@

pointer-constraints-unstable-v1-protocol.c : $(WAYLAND_PROTOCOLS_DATADIR)/unstable/pointer-constraints/pointer-constraints-unstable-v1.xml
	$(AM_V_GEN)$(WAYLAND_SCANNER) @SCANNER_ARG@ < $< > $@
pointer-constraints-unstable-v1-client-protocol.h : $(WAYLAND_PROTOCOLS_DATADIR)/unstable/pointer-constraints/pointer-constraints-unstable-v1.xml
	$(AM_V_GEN)$(WAYLAND_SCANNER) client-header < $< > $@

tablet-unstable-v2-protocol.c: $(WAYLAND_PROTOCOLS_DATADIR)/unstable/tablet/tablet-unstable-v2.xml
	$(AM_V_GEN)$(WAYLAND_SCANNER) @SCANNER_ARG@ < $< > $@
tablet-unstable-v2-client-protocol.h: $(WAYLAND_PROTOCOLS_DATADIR)/unstable/tablet/tablet-unstable-v2.xml
	$(AM_V_GEN)$(WAYLAND_SCANNER) client-header < $< > $@

xwayland-keyboard-grab-unstable-v1-protocol.c : $(WAYLAND_PROTOCOLS_DATADIR)/unstable/xwayland-keyboard-grab/xwayland-keyboard-grab-unstable-v1.xml
	$(AM_V_GEN)$(WAYLAND_SCANNER) @SCANNER_ARG@ < $< > $@
xwayland-keyboard-grab-unstable-v1-client-protocol.h : $(WAYLAND_PROTOCOLS_DATADIR)/unstable/xwayland-keyboard-grab/xwayland-keyboard-grab-unstable-v1.xml
	$(AM_V_GEN)$(WAYLAND_SCANNER) client-header < $< > $@
xdg-output-unstable-v1-protocol.c : $(WAYLAND_PROTOCOLS_DATADIR)/unstable/xdg-output/xdg-output-unstable-v1.xml
	$(AM_V_GEN)$(WAYLAND_SCANNER) @SCANNER_ARG@ < $< > $@
xdg-output-unstable-v1-client-protocol.h : $(WAYLAND_PROTOCOLS_DATADIR)/unstable/xdg-output/xdg-output-unstable-v1.xml
	$(AM_V_GEN)$(WAYLAND_SCANNER) client-header < $< > $@

linux-dmabuf-unstable-v1-protocol.c : $(WAYLAND_PROTOCOLS_DATADIR)/unstable/linux-dmabuf/linux-dmabuf-unstable-v1.xml
	$(AM_V_GEN)$(WAYLAND_SCANNER) @SCANNER_ARG@ < $< > $@
linux-dmabuf-unstable-v1-client-protocol.h : $(WAYLAND_PROTOCOLS_DATADIR)/unstable/linux-dmabuf/linux-dmabuf-unstable-v1.xml
	$(AM_V_GEN)$(WAYLAND_SCANNER) client-header < $< > $@

wayland-eglstream-client-protocol.h : $(WAYLAND_EGLSTREAM_DATADIR)/wayland-eglstream.xml
	$(AM_V_GEN)$(WAYLAND_SCANNER) client-header < $< > $@
wayland-eglstream-controller-client-protocol.h : $(WAYLAND_EGLSTREAM_DATADIR)/wayland-eglstream-controller.xml
	$(AM_V_GEN)$(WAYLAND_SCANNER) client-header < $< > $@

wayland-eglstream-protocol.c : $(WAYLAND_EGLSTREAM_DATADIR)/wayland-eglstream.xml
	$(AM_V_GEN)$(WAYLAND_SCANNER) code < $< > $@
wayland-eglstream-controller-protocol.c : $(WAYLAND_EGLSTREAM_DATADIR)/wayland-eglstream-controller.xml
	$(AM_V_GEN)$(WAYLAND_SCANNER) code < $< > $@

%-protocol.c : %.xml
	$(AM_V_GEN)$(WAYLAND_SCANNER) @SCANNER_ARG@ < $< > $@

%-client-protocol.h : %.xml
	$(AM_V_GEN)$(WAYLAND_SCANNER) client-header < $< > $@
