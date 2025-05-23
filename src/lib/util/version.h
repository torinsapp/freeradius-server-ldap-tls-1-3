#pragma once
/*
 *   This library is free software; you can redistribute it and/or
 *   modify it under the terms of the GNU Lesser General Public
 *   License as published by the Free Software Foundation; either
 *   version 2.1 of the License, or (at your option) any later version.
 *
 *   This library is distributed in the hope that it will be useful,
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
 *   Lesser General Public License for more details.
 *
 *   You should have received a copy of the GNU Lesser General Public
 *   License along with this library; if not, write to the Free Software
 *   Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301, USA
 */

/** Version checking functions
 *
 * @file src/lib/util/version.h
 *
 * @copyright 2016 The FreeRADIUS server project
 * @copyright 2016 Arran Cudbard-Bell (a.cudbardb@freeradius.org)
 */
RCSIDH(version_h, "$Id$")

#ifdef __cplusplus
extern "C" {
#endif

#include <freeradius-devel/build.h>
#include <freeradius-devel/missing.h>

#include <stdint.h>

#ifndef NDEBUG
#  define RADIUSD_VERSION_DEVELOPER "DEVELOPER BUILD - "
#else
#  define RADIUSD_VERSION_DEVELOPER ""
#endif

#ifdef RADIUSD_VERSION_COMMIT
#  define RADIUSD_VERSION_COMMIT_STRING " (git #" STRINGIFY(RADIUSD_VERSION_COMMIT) ")"
#else
#  define RADIUSD_VERSION_COMMIT_STRING ""
#endif

#ifndef ENABLE_REPRODUCIBLE_BUILDS
#  define RADIUSD_VERSION_BUILD_TIMESTAMP ", built on " __DATE__ " at " __TIME__
#else
#  define RADIUSD_VERSION_BUILD_TIMESTAMP ""
#endif

/** Create a version string for a utility in the suite of FreeRADIUS utilities
 *
 * @param _x utility name
 */
#define RADIUSD_VERSION_BUILD(_x) \
	RADIUSD_VERSION_DEVELOPER \
	_x " version " \
	STRINGIFY(RADIUSD_VERSION_MAJOR) "." STRINGIFY(RADIUSD_VERSION_MINOR) "." STRINGIFY(RADIUSD_VERSION_INCRM) \
	RADIUSD_VERSION_COMMIT_STRING \
	", for host " HOSTINFO \
	RADIUSD_VERSION_BUILD_TIMESTAMP

#ifdef WITHOUT_VERSION_CHECK
#  define RADIUSD_MAGIC_NUMBER	((uint64_t) (0xf4ee4ad3f4ee4ad3))
#  define MAGIC_PREFIX(_x)	((uint8_t) 0x00)
#  define MAGIC_VERSION(_x)	((uint32_t) 0x00000000)
#else
/*
 *	Mismatch between debug builds between
 *	the modules and the server causes all
 *	kinds of strange issues.
 */
#  ifndef NDEBUG
#    define MAGIC_PREFIX_DEBUG	01
#  else
#    define MAGIC_PREFIX_DEBUG  00
#  endif
#  define RADIUSD_MAGIC_NUMBER ((uint64_t) HEXIFY2(MAGIC_PREFIX_DEBUG, RADIUSD_VERSION))
#  define MAGIC_PREFIX(_x)	((uint8_t) ((0xff00000000000000 & (_x)) >> 56))
#  define MAGIC_VERSION(_x)	((uint32_t)((0x00ffffff00000000 & (_x)) >> 32))
#  define MAGIC_COMMIT(_x)	((uint32_t)((0x00000000ffffffff & (_x))))
#endif

#define DOC_ROOT_URL "https://www.freeradius.org/documentation/freeradius-server/" STRINGIFY(RADIUSD_DOC_VERSION)
#define DOC_KEYWORD_URL(_x) DOC_ROOT_URL "/reference/unlang/" STRINGIFY(_x) ".html"
#define DOC_KEYWORD_REF(_x) "For more information, please see " DOC_KEYWORD_URL(_x)

/*
 *	Version check.
 */
int fr_check_lib_magic(uint64_t magic);

#ifdef __cplusplus
}
#endif
