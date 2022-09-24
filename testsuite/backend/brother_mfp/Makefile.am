##  Makefile.am -- an automake template for Makefile.in file
##  Copyright (C) 2022 Ralph Little <skelband@gmail.com>
##
##  This file is part of the "Sane" build infra-structure.  See
##  included LICENSE file for license information.

TEST_LDADD = \
  ../../../sanei/libsanei.la \
  ../../../sanei/sanei_usb.lo \
  ../../../sanei/sanei_magic.lo \
  ../../../lib/liblib.la \
  ../../../backend/libbrother_mfp.la \
  ../../../backend/sane_strstatus.lo \
  $(USB_LIBS) $(XML_LIBS) $(PTHREAD_LIBS)

check_PROGRAMS = brother_mfp_unit_tests 
TESTS = brother_mfp_unit_tests

AM_CPPFLAGS += -I. -I$(srcdir) -I$(top_builddir)/include -I$(top_srcdir)/include $(USB_CFLAGS) \
    -DBACKEND_NAME=brother_mfp -DTESTSUITE_BACKEND_BROTHER_MFP_SRCDIR=$(srcdir)

brother_mfp_unit_tests_SOURCES = brother_mfp_tests.cpp brother_mfp_tests.h

brother_mfp_unit_tests_LDADD = $(TEST_LDADD)
