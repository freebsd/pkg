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
#define PP_b	(1U << 10)	/* shlib provided */
#define PP_d	(1U << 11)	/* dependency */
#define PP_r	(1U << 12)	/* requirement */

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
	PP_PKG_DIRECTORY_GROUP,
	PP_PKG_DIRECTORY_KEEPFLAG,
	PP_PKG_DIRECTORY_PATH,
	PP_PKG_DIRECTORY_PERMS,
	PP_PKG_DIRECTORY_TRYFLAG,
	PP_PKG_DIRECTORY_USER,
	PP_PKG_DIRECTORIES,
	PP_PKG_FILE_GROUP,
	PP_PKG_FILE_KEEPFLAG,
	PP_PKG_FILE_PATH,
	PP_PKG_FILE_PERMS,
	PP_PKG_FILE_SHA256,
	PP_PKG_FILE_USER,
	PP_PKG_FILES,
	PP_PKG_GROUP_GIDSTR,
	PP_PKG_GROUP_NAME,
	PP_PKG_GROUPS,
	PP_ROW_COUNTER,
	PP_PKG_LICENSE_NAME,
	PP_PKG_LICENSES,
	PP_PKG_MESSAGE,
	PP_PKG_OPTION_NAME,
	PP_PKG_OPTION_VALUE,
	PP_PKG_OPTIONS,
	PP_PKG_REPO_PATH,
	PP_PKG_CHAR_STRING,
	PP_PKG_USER_NAME,
	PP_PKG_USER_UIDSTR,
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
	PP_LAST_FORMAT = PP_PKG_HOME_PAGE,
	PP_LITERAL_PERCENT,
	PP_UNKNOWN,
	PP_END_MARKER,
} fmt_code_t;

#define	ITEM_FMT_SET	(0x1U << 0)
#define SEP_FMT_SET	(0x1U << 1)

struct percent_esc {
	unsigned	 flags;
	int		 width;
	unsigned	 trailer_status;
	struct sbuf	*item_fmt;
	struct sbuf	*sep_fmt;
	fmt_code_t	 fmt_code;
};

/* Format handler function prototypes */

_static struct sbuf *format_annotation_name(struct sbuf *, const void *, struct percent_esc *);
_static struct sbuf *format_annotation_value(struct sbuf *, const void *, struct percent_esc *);
_static struct sbuf *format_annotations(struct sbuf *, const void *, struct percent_esc *);
_static struct sbuf *format_shlibs_required(struct sbuf *, const void *, struct percent_esc *);
_static struct sbuf *format_shlib_name(struct sbuf *, const void *, struct percent_esc *);
_static struct sbuf *format_categories(struct sbuf *, const void *, struct percent_esc *);
_static struct sbuf *format_category_name(struct sbuf *, const void *, struct percent_esc *);
_static struct sbuf *format_directories(struct sbuf *, const void *, struct percent_esc *);
_static struct sbuf *format_directory_group(struct sbuf *, const void *, struct percent_esc *);
_static struct sbuf *format_directory_keepflag(struct sbuf *, const void *, struct percent_esc *);
_static struct sbuf *format_directory_path(struct sbuf *, const void *, struct percent_esc *);
_static struct sbuf *format_directory_perms(struct sbuf *, const void *, struct percent_esc *);
_static struct sbuf *format_directory_tryflag(struct sbuf *, const void *, struct percent_esc *);
_static struct sbuf *format_directory_user(struct sbuf *, const void *, struct percent_esc *);
_static struct sbuf *format_files(struct sbuf *, const void *, struct percent_esc *);
_static struct sbuf *format_file_group(struct sbuf *, const void *, struct percent_esc *);
_static struct sbuf *format_file_keepflag(struct sbuf *, const void *, struct percent_esc *);
_static struct sbuf *format_file_path(struct sbuf *, const void *, struct percent_esc *);
_static struct sbuf *format_file_perms(struct sbuf *, const void *, struct percent_esc *);
_static struct sbuf *format_file_sha256(struct sbuf *, const void *, struct percent_esc *);
_static struct sbuf *format_file_user(struct sbuf *, const void *, struct percent_esc *);
_static struct sbuf *format_groups(struct sbuf *, const void *, struct percent_esc *);
_static struct sbuf *format_group_gidstr(struct sbuf *, const void *, struct percent_esc *);
_static struct sbuf *format_group_name(struct sbuf *, const void *, struct percent_esc *);
_static struct sbuf *format_row_counter(struct sbuf *, const void *, struct percent_esc *);
_static struct sbuf *format_licenses(struct sbuf *, const void *, struct percent_esc *);
_static struct sbuf *format_license_name(struct sbuf *, const void *, struct percent_esc *);
_static struct sbuf *format_message(struct sbuf *, const void *, struct percent_esc *);
_static struct sbuf *format_options(struct sbuf *, const void *, struct percent_esc *);
_static struct sbuf *format_option_name(struct sbuf *, const void *, struct percent_esc *);
_static struct sbuf *format_option_value(struct sbuf *, const void *, struct percent_esc *);
_static struct sbuf *format_repo_path(struct sbuf *, const void *, struct percent_esc *);
_static struct sbuf *format_char_string(struct sbuf *, const void *, struct percent_esc *);
_static struct sbuf *format_users(struct sbuf *, const void *, struct percent_esc *);
_static struct sbuf *format_user_name(struct sbuf *, const void *, struct percent_esc *);
_static struct sbuf *format_user_uidstr(struct sbuf *, const void *, struct percent_esc *);
_static struct sbuf *format_old_version(struct sbuf *, const void *, struct percent_esc *);
_static struct sbuf *format_autoremove(struct sbuf *, const void *, struct percent_esc *);
_static struct sbuf *format_shlibs_provided(struct sbuf *, const void *, struct percent_esc *);
_static struct sbuf *format_comment(struct sbuf *, const void *, struct percent_esc *);
_static struct sbuf *format_dependencies(struct sbuf *, const void *, struct percent_esc *);
_static struct sbuf *format_dependency_lock(struct sbuf *, const void *, struct percent_esc *);
_static struct sbuf *format_dependency_name(struct sbuf *, const void *, struct percent_esc *);
_static struct sbuf *format_dependency_origin(struct sbuf *, const void *, struct percent_esc *);
_static struct sbuf *format_dependency_version(struct sbuf *, const void *, struct percent_esc *);
_static struct sbuf *format_description(struct sbuf *, const void *, struct percent_esc *);
_static struct sbuf *format_lock_status(struct sbuf *, const void *, struct percent_esc *);
_static struct sbuf *format_license_logic(struct sbuf *, const void *, struct percent_esc *);
_static struct sbuf *format_maintainer(struct sbuf *, const void *, struct percent_esc *);
_static struct sbuf *format_name(struct sbuf *, const void *, struct percent_esc *);
_static struct sbuf *format_origin(struct sbuf *, const void *, struct percent_esc *);
_static struct sbuf *format_prefix(struct sbuf *, const void *, struct percent_esc *);
_static struct sbuf *format_architecture(struct sbuf *, const void *, struct percent_esc *);
_static struct sbuf *format_requirements(struct sbuf *, const void *, struct percent_esc *);
_static struct sbuf *format_flatsize(struct sbuf *, const void *, struct percent_esc *);
_static struct sbuf *format_install_tstamp(struct sbuf *, const void *, struct percent_esc *);
_static struct sbuf *format_checksum(struct sbuf *, const void *, struct percent_esc *);
_static struct sbuf *format_version(struct sbuf *, const void *, struct percent_esc *);
_static struct sbuf *format_home_url(struct sbuf *, const void *, struct percent_esc *);
_static struct sbuf *format_literal_percent(struct sbuf *, __unused const void *, __unused struct percent_esc *);
_static struct sbuf *format_unknown(struct sbuf *, __unused const void *, __unused struct percent_esc *);

/* Other static function prototypes */

_static struct percent_esc *new_percent_esc(void);
_static struct percent_esc *clear_percent_esc(struct percent_esc *);
_static void free_percent_esc(struct percent_esc *);

_static char *gen_format(char *, size_t, unsigned, const char *);

_static struct sbuf *human_number(struct sbuf *, int64_t, struct percent_esc *);
_static struct sbuf *string_val(struct sbuf *, const char *,
			       struct percent_esc *);
_static struct sbuf *int_val(struct sbuf *, int64_t, struct percent_esc *);
_static struct sbuf *bool_val(struct sbuf *, bool, struct percent_esc *);
_static struct sbuf *mode_val(struct sbuf *, mode_t, struct percent_esc *);
_static struct sbuf *liclog_val(struct sbuf *, lic_t, struct percent_esc *);
_static struct sbuf *list_count(struct sbuf *, int64_t, struct percent_esc *);

_static struct percent_esc *set_list_defaults(struct percent_esc *,
					      const char *, const char *);

_static struct sbuf *iterate_item(struct sbuf *, const struct pkg *,
				  const char *, const void *, int, unsigned);

_static const char *field_modifier(const char *, struct percent_esc *);
_static const char *field_width(const char *, struct percent_esc *);
_static const char *format_code(const char *, unsigned , struct percent_esc *);
_static const char *format_trailer(const char *, struct percent_esc *);
_static const char *parse_format(const char *, unsigned, struct percent_esc *);

_static const char *maybe_read_hex_byte(struct sbuf *, const char *);
_static const char *read_oct_byte(struct sbuf *, const char *);
_static const char *process_escape(struct sbuf *, const char *);

_static const char *process_format_trailer(struct sbuf *, struct percent_esc *,
					   const char *, const struct pkg *,
					   const void *, int, unsigned);
_static const char *process_format_main(struct sbuf *, struct percent_esc *,
					const char *, va_list);

#endif

/*
 * That's All Folks!
 */
