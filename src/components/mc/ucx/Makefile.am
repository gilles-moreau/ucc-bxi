#
# Copyright (c) 2020-2022, NVIDIA CORPORATION & AFFILIATES. All rights reserved.
#

if HAVE_UCX

sources =    \
	mc_ucx.h \
	mc_ucx.c

module_LTLIBRARIES        = libucc_mc_ucx.la
libucc_mc_ucx_la_SOURCES  = $(sources)
libucc_mc_ucx_la_CPPFLAGS = $(AM_CPPFLAGS) $(BASE_CPPFLAGS) $(UCX_CPPFLAGS)
libucc_mc_ucx_la_CFLAGS   = $(BASE_CFLAGS)
libucc_mc_ucx_la_LDFLAGS  = -version-info $(SOVERSION) --as-needed $(UCX_LDFLAGS)
libucc_mc_ucx_la_LIBADD   = $(UCX_LIBADD) $(UCC_TOP_BUILDDIR)/src/libucc.la

include $(top_srcdir)/config/module.am

endif