/*-
 * SPDX-License-Identifier: LicenseRef-scancode-bsd-unchanged
 *
 * Copyright (c) 2012-2013 Matthew Seaman <matthew@FreeBSD.org>
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer
 *    in this position and unchanged.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR(S) ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR(S) BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

/* This is a private header file for internal and testing use only */
#ifndef _PKG_PRINTF_H
#define _PKG_PRINTF_H

#include "bsd_compat.h"

#include <pkg.h>

#ifdef TESTING
#define _static
#else
#define _static	static
#endif

/* Format code modifiers */
#define PP_ALTERNATE_FORM1	(1U << 0) /* ? */
#define PP_ALTERNATE_FORM2	(1U << 1) /* # */
#define PP_LEFT_ALIGN		(1U << 2) /* - */
#define PP_EXPLICIT_PLUS	(1U << 3) /* + */
#define PP_SPACE_FOR_PLUS	(1U << 4) /* SPACE */
#define PP_ZERO_PAD		(1U << 5) /* 0 */
#define PP_THOUSANDS_SEP	(1U << 6) /* ' (locale dependent) */

/* Contexts for option parsing */
#define PP_PKG	(1U << 0)	/* Any pkg scalar value */
#define PP_A	(1U << 1)	/* annotations */
#define PP_B	(1U << 2)	/* shlib required */
#define PP_C	(1U << 3)	/* category */
#define PP_D	(1U << 4)	/* directory */
#define PP_F	(1U << 5)	/* file */
#define PP_G	(1U << 6)	/* group */
#define PP_L	(1U << 7)	/* licence */
#define PP_O	(1U << 8)	/* option */
#define PP_U	(1U << 9)	/* user */
#define PP_Y	(1U << 10)	/* required */
#define PP_b	(1U << 11)	/* shlib provided */
#define PP_d	(1U << 12)	/* dependency */
#define PP_r	(1U << 13)	/* requirement */
#define PP_y	(1U << 14)	/* provided */

#define _PP_last	PP_r
#define PP_ALL	((_PP_last << 1) - 1) /* All contexts */

/*  %{ %| %} trailer context */
#define PP_TRAILER	(PP_A|PP_B|PP_C|PP_D|PP_F|PP_G|PP_L|PP_O|PP_U|PP_b|PP_d|PP_r)

/* Licence logic types */
#define PP_LIC_SINGLE	0
#define PP_LIC_OR	1
#define PP_LIC_AND	2

/* These are in alphabetical order of format code with A-Z sorting
 * before a-z */
typedef enum _fmt_code_t {
	PP_PKG_ANNOTATION_NAME = 0,
	PP_PKG_ANNOTATION_VALUE,
	PP_PKG_ANNOTATIONS,
	PP_PKG_SHLIB_REQUIRED_NAME,
	PP_PKG_SHLIBS_REQUIRED,
	PP_PKG_CATEGORY_NAME,
	PP_PKG_CATEGORIES,
	PP_PKG_DIRECTORY_FFLAGS,
	PP_PKG_DIRECTORY_GROUP,
	PP_PKG_DIRECTORY_PATH,
	PP_PKG_DIRECTORY_PERMS,
	PP_PKG_DIRECTORY_USER,
	PP_PKG_DIRECTORIES,
	PP_PKG_FILE_FFLAGS,
	PP_PKG_FILE_GROUP,
	PP_PKG_FILE_MTIME,
	PP_PKG_FILE_PATH,
	PP_PKG_FILE_PERMS,
	PP_PKG_FILE_SHA256,
	PP_PKG_FILE_SYMLINK_TARGET,
	PP_PKG_FILE_USER,
	PP_PKG_FILES,
	PP_PKG_GROUP_NAME,
	PP_PKG_GROUPS,
	PP_ROW_COUNTER,
	PP_PKG_LICENSE_NAME,
	PP_PKG_LICENSES,
	PP_PKG_MESSAGE,
	PP_PKG_REPO_IDENT,
	PP_PKG_OPTION_NAME,
	PP_PKG_OPTION_VALUE,
	PP_PKG_OPTIONS,
	PP_PKG_ALTABI,
	PP_PKG_REPO_PATH,
	PP_PKG_CHAR_STRING,
	PP_PKG_USER_NAME,
	PP_PKG_USERS,
	PP_PKG_OLD_VERSION,
	PP_PKG_AUTOREMOVE,
	PP_PKG_SHLIB_PROVIDED_NAME,
	PP_PKG_SHLIBS_PROVIDED,
	PP_PKG_COMMENT,
	PP_PKG_DEPENDENCY_LOCK,
	PP_PKG_DEPENDENCY_NAME,
	PP_PKG_DEPENDENCY_ORIGIN,
	PP_PKG_DEPENDENCY_VERSION,
	PP_PKG_DEPENDENCIES,
	PP_PKG_DESCRIPTION,
	PP_PKG_LOCK_STATUS,
	PP_PKG_LICENSE_LOGIC,
	PP_PKG_MAINTAINER,
	PP_PKG_NAME,
	PP_PKG_ORIGIN,
	PP_PKG_PREFIX,
	PP_PKG_ARCHITECTURE,
	PP_PKG_REQUIREMENT_LOCK,
	PP_PKG_REQUIREMENT_NAME,
	PP_PKG_REQUIREMENT_ORIGIN,
	PP_PKG_REQUIREMENT_VERSION,
	PP_PKG_REQUIREMENTS,
	PP_PKG_FLATSIZE,
	PP_PKG_INSTALL_TIMESTAMP,
	PP_PKG_CHECKSUM,
	PP_PKG_VERSION,
	PP_PKG_HOME_PAGE,
	PP_PKG_PKGSIZE,
	PP_PKG_REQUIRED,
	PP_PKG_REQUIRED_NAME,
	PP_PKG_PROVIDED,
	PP_PKG_PROVIDED_NAME,
	PP_PKG_SHORT_CHECKSUM,
	PP_PKG_INT_CHECKSUM,
	PP_PKG_OPTION_KEY,		/* %Ok alias for %On (deprecated) */
	PP_PKG_ANNOTATION_TAG,		/* %At alias for %An (deprecated) */
	PP_LAST_FORMAT = PP_PKG_ANNOTATION_TAG,
	PP_LITERAL_PERCENT,
	PP_UNKNOWN,
	PP_END_MARKER
} fmt_code_t;

#define	ITEM_FMT_SET	(0x1U << 0)
#define SEP_FMT_SET	(0x1U << 1)

struct percent_esc {
	unsigned	 flags;
	int		 width;
	unsigned	 trailer_status;
	sb_t item_fmt;
	sb_t sep_fmt;
	fmt_code_t	 fmt_code;
};

/* Format handler function prototypes */

_static sb_t *format_annotation_name(sb_t *, const void *, struct percent_esc *);
_static sb_t *format_annotation_value(sb_t *, const void *, struct percent_esc *);
_static sb_t *format_annotations(sb_t *, const void *, struct percent_esc *);
_static sb_t *format_shlibs_required(sb_t *, const void *, struct percent_esc *);
_static sb_t *format_shlib_name(sb_t *, const void *, struct percent_esc *);
_static sb_t *format_categories(sb_t *, const void *, struct percent_esc *);
_static sb_t *format_category_name(sb_t *, const void *, struct percent_esc *);
_static sb_t *format_directories(sb_t *, const void *, struct percent_esc *);
_static sb_t *format_directory_fflags(sb_t *, const void *, struct percent_esc *);
_static sb_t *format_directory_group(sb_t *, const void *, struct percent_esc *);
_static sb_t *format_directory_path(sb_t *, const void *, struct percent_esc *);
_static sb_t *format_directory_perms(sb_t *, const void *, struct percent_esc *);
_static sb_t *format_directory_user(sb_t *, const void *, struct percent_esc *);
_static sb_t *format_files(sb_t *, const void *, struct percent_esc *);
_static sb_t *format_file_mtime(sb_t *, const void *, struct percent_esc *);
_static sb_t *format_file_group(sb_t *, const void *, struct percent_esc *);
_static sb_t *format_file_path(sb_t *, const void *, struct percent_esc *);
_static sb_t *format_file_perms(sb_t *, const void *, struct percent_esc *);
_static sb_t *format_file_sha256(sb_t *, const void *, struct percent_esc *);
_static sb_t *format_file_user(sb_t *, const void *, struct percent_esc *);
_static sb_t *format_file_fflags(sb_t *, const void *, struct percent_esc *);
_static sb_t *format_file_symlink_target(sb_t *, const void *, struct percent_esc *);
_static sb_t *format_groups(sb_t *, const void *, struct percent_esc *);
_static sb_t *format_group_name(sb_t *, const void *, struct percent_esc *);
_static sb_t *format_row_counter(sb_t *, const void *, struct percent_esc *);
_static sb_t *format_licenses(sb_t *, const void *, struct percent_esc *);
_static sb_t *format_license_name(sb_t *, const void *, struct percent_esc *);
_static sb_t *format_message(sb_t *, const void *, struct percent_esc *);
_static sb_t *format_repo_ident(sb_t *, const void *, struct percent_esc *);
_static sb_t *format_options(sb_t *, const void *, struct percent_esc *);
_static sb_t *format_option_name(sb_t *, const void *, struct percent_esc *);
_static sb_t *format_option_value(sb_t *, const void *, struct percent_esc *);
_static sb_t *format_repo_path(sb_t *, const void *, struct percent_esc *);
_static sb_t *format_char_string(sb_t *, const void *, struct percent_esc *);
_static sb_t *format_users(sb_t *, const void *, struct percent_esc *);
_static sb_t *format_user_name(sb_t *, const void *, struct percent_esc *);
_static sb_t *format_old_version(sb_t *, const void *, struct percent_esc *);
_static sb_t *format_autoremove(sb_t *, const void *, struct percent_esc *);
_static sb_t *format_shlibs_provided(sb_t *, const void *, struct percent_esc *);
_static sb_t *format_comment(sb_t *, const void *, struct percent_esc *);
_static sb_t *format_dependencies(sb_t *, const void *, struct percent_esc *);
_static sb_t *format_dependency_lock(sb_t *, const void *, struct percent_esc *);
_static sb_t *format_dependency_name(sb_t *, const void *, struct percent_esc *);
_static sb_t *format_dependency_origin(sb_t *, const void *, struct percent_esc *);
_static sb_t *format_dependency_version(sb_t *, const void *, struct percent_esc *);
_static sb_t *format_description(sb_t *, const void *, struct percent_esc *);
_static sb_t *format_lock_status(sb_t *, const void *, struct percent_esc *);
_static sb_t *format_license_logic(sb_t *, const void *, struct percent_esc *);
_static sb_t *format_maintainer(sb_t *, const void *, struct percent_esc *);
_static sb_t *format_name(sb_t *, const void *, struct percent_esc *);
_static sb_t *format_origin(sb_t *, const void *, struct percent_esc *);
_static sb_t *format_prefix(sb_t *, const void *, struct percent_esc *);
_static sb_t *format_architecture(sb_t *, const void *, struct percent_esc *);
_static sb_t *format_altabi(sb_t *, const void *, struct percent_esc *);
_static sb_t *format_requirements(sb_t *, const void *, struct percent_esc *);
_static sb_t *format_flatsize(sb_t *, const void *, struct percent_esc *);
_static sb_t *format_install_tstamp(sb_t *, const void *, struct percent_esc *);
_static sb_t *format_checksum(sb_t *, const void *, struct percent_esc *);
_static sb_t *format_version(sb_t *, const void *, struct percent_esc *);
_static sb_t *format_home_url(sb_t *, const void *, struct percent_esc *);
_static sb_t *format_pkgsize(sb_t *, const void *, struct percent_esc *);
_static sb_t *format_short_checksum(sb_t *, const void *, struct percent_esc *);
_static sb_t *format_literal_percent(sb_t *, __unused const void *, __unused struct percent_esc *);
_static sb_t *format_unknown(sb_t *, __unused const void *, __unused struct percent_esc *);
_static sb_t *format_provided(sb_t *, const void *, struct percent_esc *);
_static sb_t *format_required(sb_t *, const void *, struct percent_esc *);
_static sb_t *format_provide_name(sb_t *, const void *, struct percent_esc *);
_static sb_t *format_int_checksum(sb_t *, const void *, struct percent_esc *);

/* Other static function prototypes */

_static struct percent_esc *new_percent_esc(void);
_static struct percent_esc *clear_percent_esc(struct percent_esc *);
_static void free_percent_esc(struct percent_esc *);

_static char *gen_format(char *, size_t, unsigned, const char *);

_static sb_t *human_number(sb_t *, int64_t, struct percent_esc *);
_static sb_t *string_val(sb_t *, const char *,
			       struct percent_esc *);
_static sb_t *int_val(sb_t *, int64_t, struct percent_esc *);
_static sb_t *bool_val(sb_t *, bool, struct percent_esc *);
_static sb_t *mode_val(sb_t *, mode_t, struct percent_esc *);
_static sb_t *liclog_val(sb_t *, lic_t, struct percent_esc *);
_static sb_t *list_count(sb_t *, int64_t, struct percent_esc *);

_static struct percent_esc *set_list_defaults(struct percent_esc *,
					      const char *, const char *);

_static sb_t *iterate_item(sb_t *, const struct pkg *,
				  const char *, const void *, int, unsigned);

_static const char *field_modifier(const char *, struct percent_esc *);
_static const char *field_width(const char *, struct percent_esc *);
_static const char *format_code(const char *, unsigned , struct percent_esc *);
_static const char *format_trailer(const char *, struct percent_esc *);
_static const char *parse_format(const char *, unsigned, struct percent_esc *);

_static const char *maybe_read_hex_byte(sb_t *, const char *);
_static const char *read_oct_byte(sb_t *, const char *);
_static const char *process_escape(sb_t *, const char *);

_static const char *process_format_trailer(sb_t *, struct percent_esc *,
					   const char *, const struct pkg *,
					   const void *, int, unsigned);
_static const char *process_format_main(sb_t *, struct percent_esc *,
					const char *, const char *, void *);

#endif

/*
 * That's All Folks!
 */
