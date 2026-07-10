/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef CL_EXPORT_H
#define CL_EXPORT_H

/*
 * Symbol export/visibility control.
 *
 * The build system defines:
 *   COPAL_SHARED   - when copal is built/consumed as a shared library;
 *   COPAL_BUILDING - only while compiling the copal library itself (PRIVATE).
 *
 * Consumers of a static library, or of a shared library on ELF targets, get an
 * empty CL_API and rely on normal linkage / default visibility.
 */
#if defined(_WIN32) || defined(__CYGWIN__)
#  if defined(COPAL_SHARED)
#    if defined(COPAL_BUILDING)
#      define CL_API __declspec(dllexport)
#    else
#      define CL_API __declspec(dllimport)
#    endif
#  else
#    define CL_API
#  endif
#else
#  if defined(COPAL_SHARED) && defined(COPAL_BUILDING)
#    define CL_API __attribute__((visibility("default")))
#  else
#    define CL_API
#  endif
#endif

#endif /* CL_EXPORT_H */
