if TL_UCP_ENABLED

sources =    \
	ec_ucp.h \
	ec_ucp.c

module_LTLIBRARIES        = libucc_ec_ucp.la
libucc_ec_ucp_la_SOURCES  = $(sources)
libucc_ec_ucp_la_CPPFLAGS = $(AM_CPPFLAGS) $(BASE_CPPFLAGS) $(UCX_CPPFLAGS)
libucc_ec_ucp_la_CFLAGS   = $(BASE_CFLAGS)
libucc_ec_ucp_la_LDFLAGS  = -version-info $(SOVERSION) --as-needed $(UCX_LDFLAGS)
libucc_ec_ucp_la_LIBADD   = $(UCX_LIBADD) $(UCC_TOP_BUILDDIR)/src/libucc.la

include $(top_srcdir)/config/module.am

endif
