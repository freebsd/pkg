/*-
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
	PP_PKG_OPTION_DEFAULT,
	PP_PKG_OPTION_DESCRIPTION,
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
	PP_LAST_FORMAT = PP_PKG_INT_CHECKSUM,
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
	xstring	*item_fmt;
	xstring	*sep_fmt;
	fmt_code_t	 fmt_code;
};

/* Format handler function prototypes */

_static xstring *format_annotation_name(xstring *, const void *, struct percent_esc *);
_static xstring *format_annotation_value(xstring *, const void *, struct percent_esc *);
_static xstring *format_annotations(xstring *, const void *, struct percent_esc *);
_static xstring *format_shlibs_required(xstring *, const void *, struct percent_esc *);
_static xstring *format_shlib_name(xstring *, const void *, struct percent_esc *);
_static xstring *format_categories(xstring *, const void *, struct percent_esc *);
_static xstring *format_category_name(xstring *, const void *, struct percent_esc *);
_static xstring *format_directories(xstring *, const void *, struct percent_esc *);
_static xstring *format_directory_fflags(xstring *, const void *, struct percent_esc *);
_static xstring *format_directory_group(xstring *, const void *, struct percent_esc *);
_static xstring *format_directory_path(xstring *, const void *, struct percent_esc *);
_static xstring *format_directory_perms(xstring *, const void *, struct percent_esc *);
_static xstring *format_directory_user(xstring *, const void *, struct percent_esc *);
_static xstring *format_files(xstring *, const void *, struct percent_esc *);
_static xstring *format_file_group(xstring *, const void *, struct percent_esc *);
_static xstring *format_file_path(xstring *, const void *, struct percent_esc *);
_static xstring *format_file_perms(xstring *, const void *, struct percent_esc *);
_static xstring *format_file_sha256(xstring *, const void *, struct percent_esc *);
_static xstring *format_file_user(xstring *, const void *, struct percent_esc *);
_static xstring *format_file_fflags(xstring *, const void *, struct percent_esc *);
_static xstring *format_file_symlink_target(xstring *, const void *, struct percent_esc *);
_static xstring *format_groups(xstring *, const void *, struct percent_esc *);
_static xstring *format_group_name(xstring *, const void *, struct percent_esc *);
_static xstring *format_row_counter(xstring *, const void *, struct percent_esc *);
_static xstring *format_licenses(xstring *, const void *, struct percent_esc *);
_static xstring *format_license_name(xstring *, const void *, struct percent_esc *);
_static xstring *format_message(xstring *, const void *, struct percent_esc *);
_static xstring *format_repo_ident(xstring *, const void *, struct percent_esc *);
_static xstring *format_options(xstring *, const void *, struct percent_esc *);
_static xstring *format_option_name(xstring *, const void *, struct percent_esc *);
_static xstring *format_option_value(xstring *, const void *, struct percent_esc *);
_static xstring *format_option_default(xstring *, const void *, struct percent_esc *);
_static xstring *format_option_description(xstring *, const void *, struct percent_esc *);
_static xstring *format_repo_path(xstring *, const void *, struct percent_esc *);
_static xstring *format_char_string(xstring *, const void *, struct percent_esc *);
_static xstring *format_users(xstring *, const void *, struct percent_esc *);
_static xstring *format_user_name(xstring *, const void *, struct percent_esc *);
_static xstring *format_old_version(xstring *, const void *, struct percent_esc *);
_static xstring *format_autoremove(xstring *, const void *, struct percent_esc *);
_static xstring *format_shlibs_provided(xstring *, const void *, struct percent_esc *);
_static xstring *format_comment(xstring *, const void *, struct percent_esc *);
_static xstring *format_dependencies(xstring *, const void *, struct percent_esc *);
_static xstring *format_dependency_lock(xstring *, const void *, struct percent_esc *);
_static xstring *format_dependency_name(xstring *, const void *, struct percent_esc *);
_static xstring *format_dependency_origin(xstring *, const void *, struct percent_esc *);
_static xstring *format_dependency_version(xstring *, const void *, struct percent_esc *);
_static xstring *format_description(xstring *, const void *, struct percent_esc *);
_static xstring *format_lock_status(xstring *, const void *, struct percent_esc *);
_static xstring *format_license_logic(xstring *, const void *, struct percent_esc *);
_static xstring *format_maintainer(xstring *, const void *, struct percent_esc *);
_static xstring *format_name(xstring *, const void *, struct percent_esc *);
_static xstring *format_origin(xstring *, const void *, struct percent_esc *);
_static xstring *format_prefix(xstring *, const void *, struct percent_esc *);
_static xstring *format_architecture(xstring *, const void *, struct percent_esc *);
_static xstring *format_altabi(xstring *, const void *, struct percent_esc *);
_static xstring *format_requirements(xstring *, const void *, struct percent_esc *);
_static xstring *format_flatsize(xstring *, const void *, struct percent_esc *);
_static xstring *format_install_tstamp(xstring *, const void *, struct percent_esc *);
_static xstring *format_checksum(xstring *, const void *, struct percent_esc *);
_static xstring *format_version(xstring *, const void *, struct percent_esc *);
_static xstring *format_home_url(xstring *, const void *, struct percent_esc *);
_static xstring *format_pkgsize(xstring *, const void *, struct percent_esc *);
_static xstring *format_short_checksum(xstring *, const void *, struct percent_esc *);
_static xstring *format_literal_percent(xstring *, __unused const void *, __unused struct percent_esc *);
_static xstring *format_unknown(xstring *, __unused const void *, __unused struct percent_esc *);
_static xstring *format_provided(xstring *, const void *, struct percent_esc *);
_static xstring *format_required(xstring *, const void *, struct percent_esc *);
_static xstring *format_provide_name(xstring *, const void *, struct percent_esc *);
_static xstring *format_int_checksum(xstring *, const void *, struct percent_esc *);

/* Other static function prototypes */

_static struct percent_esc *new_percent_esc(void);
_static struct percent_esc *clear_percent_esc(struct percent_esc *);
_static void free_percent_esc(struct percent_esc *);

_static char *gen_format(char *, size_t, unsigned, const char *);

_static xstring *human_number(xstring *, int64_t, struct percent_esc *);
_static xstring *string_val(xstring *, const char *,
			       struct percent_esc *);
_static xstring *int_val(xstring *, int64_t, struct percent_esc *);
_static xstring *bool_val(xstring *, bool, struct percent_esc *);
_static xstring *mode_val(xstring *, mode_t, struct percent_esc *);
_static xstring *liclog_val(xstring *, lic_t, struct percent_esc *);
_static xstring *list_count(xstring *, int64_t, struct percent_esc *);

_static struct percent_esc *set_list_defaults(struct percent_esc *,
					      const char *, const char *);

_static xstring *iterate_item(xstring *, const struct pkg *,
				  const char *, const void *, int, unsigned);

_static const char *field_modifier(const char *, struct percent_esc *);
_static const char *field_width(const char *, struct percent_esc *);
_static const char *format_code(const char *, unsigned , struct percent_esc *);
_static const char *format_trailer(const char *, struct percent_esc *);
_static const char *parse_format(const char *, unsigned, struct percent_esc *);

_static const char *maybe_read_hex_byte(xstring *, const char *);
_static const char *read_oct_byte(xstring *, const char *);
_static const char *process_escape(xstring *, const char *);

_static const char *process_format_trailer(xstring *, struct percent_esc *,
					   const char *, const struct pkg *,
					   const void *, int, unsigned);
_static const char *process_format_main(xstring *, struct percent_esc *,
					const char *, const char *, void *);

#endif

/*
 * That's All Folks!
 */
