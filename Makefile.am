AM_CFLAGS = \
	$(WARN_CFLAGS)	\
	-I./include		\
	-I${includedir}

libnx_allocator_la_LTLIBRARIES = libnx_allocator.la
libnx_allocator_ladir = ${libdir}
libnx_allocator_la_LDFLAGS = -L${libdir}

libnx_allocator_la_SOURCES = \
	nx-alloc-ion.cpp

libnx_allocatorincludedir = ${includedir}
libnx_allocatorinclude_HEADERS = \
	include/linux/videodev2_nxp_media.h	\
	nx-alloc.h
