/*
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

#include "bsd_compat.h"
#include <sys/types.h>
#include <sys/sbuf.h>
#include <sys/stat.h>

#include <assert.h>
#include <ctype.h>
#include <inttypes.h>
#include <stdarg.h>
#define _WITH_DPRINTF
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <utlist.h>

#include "pkg.h"
#include <private/pkg_printf.h>
#include <private/pkg.h>

/*
 * Format codes
 *    Arg Type     What
 * A  pkg          Package annotations
 * An pkg_note     Annotation tag name
 * Av pkg_note     Annotation value
 *
 * B  pkg          List of required shared libraries
 * Bn pkg_shlib    Shared library name
 *
 * C  pkg          List of categories
 * Cn pkg_category Category name
 *
 * D  pkg          List of directories
 * Dg pkg_dir      Group owner of directory
 * Dk pkg_dir      Keep flag
 * Dn pkg_dir      Directory path name
 * Dp pkg_dir      Directory permissions
 * Dt pkg_dir      Try flag (@dirrmtry in plist)
 * Du pkg_dir      User owner of directory
 * 
 * E
 *
 * F  pkg          List of files
 * Fg pkg_file     Group owner of file
 * Fk pkg_file     Keep flag
 * Fn pkg_file     File path name
 * Fp pkg_file     File permissions
 * Fs pkg_file     File SHA256 checksum
 * Fu pkg_file     User owner of file 
 *
 * G  pkg          List of groups
 * Gg pkg_group    gidstr (parse this using gr_scan()?)
 * Gn pkg_group    Group name
 *
 * H
 *
 * I  int*         Row counter
 *
 * J
 * K
 *
 * L  pkg          List of licenses
 * Ln pkg_license  Licence name
 *
 * M  pkg          Message
 * N  pkg          Reponame
 *
 * O  pkg          List of options
 * On pkg_option   Option name (key)
 * Ov pkg_option   Option value
 * Od pkg_option   Option default value (if known)
 * OD pkg_option   Option description
 *
 * P
 * Q
 *
 * R  pkg          Repopath
 * S  char*        Arbitrary character string
 *
 * T
 *
 * U  pkg          List of users
 * Un pkg_user     User name
 * Uu pkg_user     uidstr (parse this using pw_scan()?)
 *
 * V  pkg          old version
 * W
 * X
 * Y
 * Z
 *
 * a  pkg          autoremove flag
 *
 * b  pkg          List of provided shared libraries
 * bn pkg_shlib    Shared library name
 *
 * c  pkg          comment
 *
 * d  pkg          List of dependencies
 * dk pkg_dep      dependency lock status
 * dn pkg_dep      dependency name
 * do pkg_dep      dependency origin
 * dv pkg_dep      dependency version
 *
 * e  pkg          Package description
 *
 * f
 * g
 * h
 * i
 * j
 *
 * k  pkg          lock status
 * l  pkg          license logic
 * m  pkg          maintainer
 * n  pkg          name
 * o  pkg          origin
 * p  pkg          prefix
 * q  pkg	   architecture / ABI
 * r  pkg          List of requirements
 * rk pkg_dep      requirement lock status
 * rn pkg_dep      requirement name
 * ro pkg_dep      requirement origin
 * rv pkg_dep      requirement version
 *
 * s  pkg          flatsize
 * t  pkg          install timestamp
 * u  pkg          checksum
 * v  pkg          version
 * w  pkg          home page URL
 *
 * x
 * y
 *
 * z  pkg          short checksum
 */

struct pkg_printf_fmt {
	char	         fmt_main;
	char		 fmt_sub;
	bool		 has_trailer;
	bool		 struct_pkg; /* or else a sub-type? */
	unsigned	 context;
	struct sbuf	*(*fmt_handler)(struct sbuf *, const void *,
					struct percent_esc *);
};

/*
 * These are in pkg_fmt_t order, which is necessary for the parsing
 * algorithm.
 */

static const struct pkg_printf_fmt	fmt[] = {
	[PP_PKG_ANNOTATION_NAME] =
	{
		'A',
		'n',
		false,
		false,
		PP_PKG|PP_A,
		&format_annotation_name,
	},
	[PP_PKG_ANNOTATION_VALUE] =
	{
		'A',
		'v',
		false,
		false,
		PP_PKG|PP_A,
		&format_annotation_value,
	},
	[PP_PKG_ANNOTATIONS] =
	{
		'A',
		'\0',
		true,
		true,
		PP_PKG,
		&format_annotations,
	},
	[PP_PKG_SHLIB_REQUIRED_NAME] =
	{
		'B',
		'n',
		false,
		false,
		PP_PKG|PP_B,
		&format_shlib_name,
	},
	[PP_PKG_SHLIBS_REQUIRED] =
	{
		'B',
		'\0',
		true,
		true,
		PP_PKG,
		&format_shlibs_required,
	},
        [PP_PKG_CATEGORY_NAME] =
	{
		'C',
		'n',
		false,
		false,
		PP_PKG|PP_C,
		&format_category_name,
	},
	[PP_PKG_CATEGORIES] =
	{
		'C',
		'\0',
		true,
		true,
		PP_PKG,
		&format_categories,
	},
        [PP_PKG_DIRECTORY_GROUP] =
	{
		'D',
		'g',
		false,
		false,
		PP_PKG|PP_D,
		&format_directory_group,
	},
	[PP_PKG_DIRECTORY_PATH] =
	{
		'D',
		'n',
		false,
		false,
		PP_PKG|PP_D,
		&format_directory_path,
	},
	[PP_PKG_DIRECTORY_PERMS] =
	{
		'D',
		'p',
		false,
		false,
		PP_PKG|PP_D,
		&format_directory_perms,
	},
	[PP_PKG_DIRECTORY_USER] =
	{
		'D',
		'u',
		false,
		false,
		PP_PKG|PP_D,
		&format_directory_user,
	},
	[PP_PKG_DIRECTORIES] =
	{
		'D',
		'\0',
		true,
		true,
		PP_PKG,
		&format_directories,
	},
	[PP_PKG_FILE_GROUP] =
	{
		'F',
		'g',
		false,
		false,
		PP_PKG|PP_F,
		&format_file_group,
	},
	[PP_PKG_FILE_PATH] =
	{
		'F',
		'n',
		false,
		false,
		PP_PKG|PP_F,
		&format_file_path,
	},
	[PP_PKG_FILE_PERMS] =
	{
		'F',
		'p',
		false,
		false,
		PP_PKG|PP_F,
		&format_file_perms,
	},
	[PP_PKG_FILE_SHA256] =
	{
		'F',
		's',
		false,
		false,
		PP_PKG|PP_F,
		&format_file_sha256,
	},
	[PP_PKG_FILE_USER] =
	{
		'F',
		'u',
		false,
		false,
		PP_PKG|PP_F,
		&format_file_user,
	},
	[PP_PKG_FILES] =
	{
		'F',
		'\0',
		true,
		true,
		PP_PKG,
		&format_files,
	},
	[PP_PKG_GROUP_GIDSTR] =
	{
		'G',
		'g',
		false,
		false,
		PP_PKG|PP_G,
		&format_group_gidstr,
	},
	[PP_PKG_GROUP_NAME] =
	{
		'G',
		'n',
		false,
		false,
		PP_PKG|PP_G,
		&format_group_name,
	},
	[PP_PKG_GROUPS] =
	{
		'G',
		'\0',
		true,
		true,
		PP_PKG,
		&format_groups,
	},
	[PP_ROW_COUNTER] =
	{
		'I',
		'\0',
		false,
		false,
		PP_TRAILER,
		&format_row_counter,
	},
	[PP_PKG_LICENSE_NAME] =
	{
		'L',
		'n',
		false,
		false,
		PP_PKG|PP_L,
		&format_license_name,
	},
	[PP_PKG_LICENSES] =
	{
		'L',
		'\0',
		true,
		true,
		PP_PKG,
		&format_licenses,
	},
	[PP_PKG_MESSAGE] =
	{
		'M',
		'\0',
		false,
		true,
		PP_ALL,
		&format_message,
	},
	[PP_PKG_REPO_IDENT] =
	{
		'N',
		'\0',
		false,
		true,
		PP_ALL,
		&format_repo_ident,
	},
	[PP_PKG_OPTION_NAME] =
	{
		'O',
		'n',
		false,
		false,
		PP_PKG|PP_O,
		&format_option_name,
	},
	[PP_PKG_OPTION_VALUE] =
	{
		'O',
		'v',
		false,
		false,
		PP_PKG|PP_O,
		&format_option_value,
	},
	[PP_PKG_OPTION_DEFAULT] =
	{
		'O',
		'd',
		false,
		false,
		PP_PKG|PP_O,
		&format_option_default,
	},
	[PP_PKG_OPTION_DESCRIPTION] =
	{
		'O',
		'D',
		false,
		false,
		PP_PKG|PP_O,
		&format_option_description,
	},
	[PP_PKG_OPTIONS] =
	{
		'O',
		'\0',
		true,
		true,
		PP_PKG,
		&format_options,
	},
	[PP_PKG_REPO_PATH] =
	{
		'R',
		'\0',
		false,
		true,
		PP_ALL,
		&format_repo_path,
	},
	[PP_PKG_CHAR_STRING] =
	{
		'S',
		'\0',
		false,
		false,
		PP_PKG,
		&format_char_string,
	},
	[PP_PKG_USER_NAME] =
	{
		'U',
		'n',
		false,
		false,
		PP_PKG|PP_U,
		&format_user_name,
	},
	[PP_PKG_USER_UIDSTR] =
	{
		'U',
		'u',
		false,
		false,
		PP_PKG|PP_U,
		&format_user_uidstr,
	},
	[PP_PKG_USERS] =
	{
		'U',
		'\0',
		true,
		true,
		PP_PKG,
		&format_users,
	},
	[PP_PKG_OLD_VERSION] =
	{
		'V',
		'\0',
		false,
		true,
		PP_ALL,
		&format_old_version,
	},
	[PP_PKG_AUTOREMOVE] =
	{
		'a',
		'\0',
		false,
		true,
		PP_ALL,
		&format_autoremove,
	},
	[PP_PKG_SHLIB_PROVIDED_NAME] =
	{
		'b',
		'n',
		false,
		false,
		PP_PKG|PP_b,
		&format_shlib_name,
	},
	[PP_PKG_SHLIBS_PROVIDED] =
	{
		'b',
		'\0',
		true,
		true,
		PP_PKG,
		&format_shlibs_provided,
	},
	[PP_PKG_COMMENT] =
	{
		'c',
		'\0',
		false,
		true,
		PP_ALL,
		&format_comment,
	},
	[PP_PKG_DEPENDENCY_LOCK] =
	{
		'd',
		'k',
		false,
		false,
		PP_PKG|PP_d,
		&format_dependency_lock,
	},
	[PP_PKG_DEPENDENCY_NAME] =
	{
		'd',
		'n',
		false,
		false,
		PP_PKG|PP_d,
		&format_dependency_name,
	},
	[PP_PKG_DEPENDENCY_ORIGIN] =
	{
		'd',
		'o',
		false,
		false,
		PP_PKG|PP_d,
		&format_dependency_origin,
	},
	[PP_PKG_DEPENDENCY_VERSION] =
	{
		'd',
		'v',
		false,
		false,
		PP_PKG|PP_d,
		&format_dependency_version,
	},
	[PP_PKG_DEPENDENCIES] =
	{
		'd',
		'\0',
		true,
		true,
		PP_PKG,
		&format_dependencies,
	},
	[PP_PKG_DESCRIPTION] =
	{
		'e',
		'\0',
		false,
		true,
		PP_ALL,
		&format_description,
	},
	[PP_PKG_LOCK_STATUS] =
	{
		'k',
		'\0',
		false,
		true,
		PP_ALL,
		&format_lock_status,
	},
	[PP_PKG_LICENSE_LOGIC] =
	{
		'l',
		'\0',
		false,
		true,
		PP_ALL,
		&format_license_logic,
	},
	[PP_PKG_MAINTAINER] =
	{
		'm',
		'\0',
		false,
		true,
		PP_ALL,
		&format_maintainer,
	},
	[PP_PKG_NAME] =
	{
		'n',
		'\0',
		false,
		true,
		PP_ALL,
		&format_name, },
	[PP_PKG_ORIGIN] =
	{
		'o',
		'\0',
		false,
		true,
		PP_ALL,
		&format_origin,
	},
	[PP_PKG_PREFIX] =
	{
		'p',
		'\0',
		false,
		true,
		PP_ALL,
		&format_prefix,
	},
	[PP_PKG_ARCHITECTURE] =
	{
		'q',
		'\0',
		false,
		true,
		PP_ALL,
		&format_architecture,
	},
	[PP_PKG_REQUIREMENT_LOCK] =
	{
		'r',
		'k',
		false,
		false,
		PP_PKG|PP_r,
		&format_dependency_lock,
	},
	[PP_PKG_REQUIREMENT_NAME] =
	{
		'r',
		'n',
		false,
		false,
		PP_PKG|PP_r,
		&format_dependency_name,
	},
	[PP_PKG_REQUIREMENT_ORIGIN] =
	{
		'r',
		'o',
		false,
		false,
		PP_PKG|PP_r,
		&format_dependency_origin,
	},
	[PP_PKG_REQUIREMENT_VERSION] =
	{
		'r',
		'v',
		false,
		false,
		PP_PKG|PP_r,
		&format_dependency_version,
	},
	[PP_PKG_REQUIREMENTS] =
	{
		'r',
		'\0',
		true,
		true,
		PP_PKG,
		&format_requirements,
	},
	[PP_PKG_FLATSIZE] =
	{
		's',
		'\0',
		false,
		true,
		PP_ALL,
		&format_flatsize,
	},
	[PP_PKG_INSTALL_TIMESTAMP] =
	{
		't',
		'\0',
		true,
		true,
		PP_ALL,
		&format_install_tstamp,
	},
	[PP_PKG_CHECKSUM] =
	{
		'u',
		'\0',
		false,
		true,
		PP_ALL,
		&format_checksum,
	},
	[PP_PKG_SHORT_CHECKSUM] =
	{
		'z',
		'\0',
		false,
		true,
		PP_ALL,
		&format_short_checksum,
	},
	[PP_PKG_VERSION] =
	{
		'v',
		'\0',
		false,
		true,
		PP_ALL,
		&format_version,
	},
	[PP_PKG_HOME_PAGE] =
	{
		'w',
		'\0',
		false,
		true,
		PP_ALL,
		&format_home_url,
	},
	[PP_LITERAL_PERCENT] =
	{
		'%',
		'\0',
		false,
		false,
		PP_ALL,
		&format_literal_percent,
	},
	[PP_UNKNOWN] =
	{
		'\0',
		'\0',
		false,
		false,
		PP_ALL,
		&format_unknown,
	},
	[PP_END_MARKER] =
	{
		'\0',
		'\0',
		false,
		false,
		0,
		NULL,
	},
};

/*
 * Note: List values -- special behaviour with ? and # modifiers.
 * Affects %A %B %C %D %F %G %L %O %U %b %d %r
 *
 * With ? -- Flag values.  Boolean.  %?X returns 0 if the %X list is
 * empty, 1 otherwise.
 *
 * With # -- Count values.  Integer.  %#X returns the number of items in
 * the %X list.
 */

/*
 * %A -- Annotations.  Free-form tag+value text that can be added to
 * packages.  Optionally accepts per-field format in %{ %| %} Default
 * %{%An: %Av\n%|%}
 */  
struct sbuf *
format_annotations(struct sbuf *sbuf, const void *data, struct percent_esc *p)
{
	const struct pkg	*pkg = data;
	struct pkg_kv		*kv;
	int			count;

	if (p->flags & (PP_ALTERNATE_FORM1|PP_ALTERNATE_FORM2)) {
		LL_COUNT(pkg->annotations, kv, count)
		return (list_count(sbuf, count, p));
	} else {
		set_list_defaults(p, "%An: %Av\n", "");

		count = 1;
		LL_FOREACH(pkg->annotations, kv) {
			if (count > 1)
				iterate_item(sbuf, pkg, sbuf_data(p->sep_fmt),
					     kv, count, PP_A);

			iterate_item(sbuf, pkg, sbuf_data(p->item_fmt),
				     kv, count, PP_A);
			count++;
		}
	}
	return (sbuf);
}

/*
 * %An -- Annotation tag name.
 */
struct sbuf *
format_annotation_name(struct sbuf *sbuf, const void *data, struct percent_esc *p)
{
	const struct pkg_kv	*kv = data;

	return (string_val(sbuf, kv->key, p));
}

/*
 * %Av -- Annotation value.
 */
struct sbuf *
format_annotation_value(struct sbuf *sbuf, const void *data, struct percent_esc *p)
{
	const struct pkg_kv	*kv = data;

	return (string_val(sbuf, kv->value, p));
}

/*
 * %B -- Required Shared Libraries.  List of shlibs required by
 * binaries in the pkg.  Optionally accepts per-field format in %{ %|
 * %}.  Default %{%Bn\n%|%}
 */
struct sbuf *
format_shlibs_required(struct sbuf *sbuf, const void *data, struct percent_esc *p)
{
	const struct pkg	*pkg = data;

	if (p->flags & (PP_ALTERNATE_FORM1|PP_ALTERNATE_FORM2))
		return (list_count(sbuf, pkg_list_count(pkg, PKG_SHLIBS_REQUIRED), p));
	else {
		struct pkg_shlib	*shlib = NULL;
		int			 count;

		set_list_defaults(p, "%Bn\n", "");

		count = 1;
		while (pkg_shlibs_required(pkg, &shlib) == EPKG_OK) {
			if (count > 1)
				iterate_item(sbuf, pkg, sbuf_data(p->sep_fmt),
					     shlib, count, PP_B);

			iterate_item(sbuf, pkg, sbuf_data(p->item_fmt),
				     shlib, count, PP_B);
			count++;
		}
	}
	return (sbuf);
}

/*
 * %Bn -- Required Shared Library name or %bn -- Provided Shared
 * Library name
 */
struct sbuf *
format_shlib_name(struct sbuf *sbuf, const void *data, struct percent_esc *p)
{
	const struct pkg_shlib	*shlib = data;

	return (string_val(sbuf, shlib->name, p));
}

/*
 * %C -- Categories.  List of Category names (strings). 1ary category
 * is not distinguished -- look at the package origin for that.
 * Optionally accepts per-field format in %{ %| %}, where %n is
 * replaced by the category name.  Default %{%Cn%|, %}
 */
struct sbuf *
format_categories(struct sbuf *sbuf, const void *data, struct percent_esc *p)
{
	const struct pkg	*pkg = data;
	struct pkg_strel	*el;
	int			 count = 0;

	if (p->flags & (PP_ALTERNATE_FORM1|PP_ALTERNATE_FORM2)) {
		LL_COUNT(pkg->categories, el, count);
		return (list_count(sbuf, count, p));
	} else {
		set_list_defaults(p, "%Cn", ", ");

		count = 1;
		LL_FOREACH(pkg->categories, el) {
			if (count > 1)
				iterate_item(sbuf, pkg, sbuf_data(p->sep_fmt),
				    el, count, PP_C);

			iterate_item(sbuf, pkg, sbuf_data(p->item_fmt), el,
			    count, PP_C);
			count++;
		}
	}
	return (sbuf);
}

/*
 * %Cn -- Category name.
 */
struct sbuf *
format_category_name(struct sbuf *sbuf, const void *data, struct percent_esc *p)
{
	const struct pkg_strel	*el = data;

	return (string_val(sbuf, el->value, p));
}

/*
 * %D -- Directories.  List of directory names (strings) possibly with
 * other meta-data.  Optionally accepts following per-field format in
 * %{ %| %}, where %Dn is replaced by the directory name.  Default
 * %{%Dn\n%|%}
 */
struct sbuf *
format_directories(struct sbuf *sbuf, const void *data, struct percent_esc *p)
{
	const struct pkg	*pkg = data;

	if (p->flags & (PP_ALTERNATE_FORM1|PP_ALTERNATE_FORM2))
		return (list_count(sbuf, pkg_list_count(pkg, PKG_DIRS), p));
	else {
		struct pkg_dir	*dir = NULL;
		int		 count;

		set_list_defaults(p, "%Dn\n", "");

		count = 1;
		while (pkg_dirs(pkg, &dir) == EPKG_OK) {
			if (count > 1)
				iterate_item(sbuf, pkg, sbuf_data(p->sep_fmt),
					     dir, count, PP_D);

			iterate_item(sbuf, pkg, sbuf_data(p->item_fmt),
				     dir, count, PP_D);
			count++;
		}
	}
	return (sbuf);
}

/*
 * %Dg -- Directory group. TODO: numeric gid
 */
struct sbuf *
format_directory_group(struct sbuf *sbuf, const void *data,
		       struct percent_esc *p)
{
	const struct pkg_dir	*dir = data;

	return (string_val(sbuf, dir->gname, p));
}

/*
 * %Dn -- Directory path name.
 */
struct sbuf *
format_directory_path(struct sbuf *sbuf, const void *data, struct percent_esc *p)
{
	const struct pkg_dir	*dir = data;

	return (string_val(sbuf, dir->path, p));
}

/*
 * %Dp -- Directory permissions.
 */
struct sbuf *
format_directory_perms(struct sbuf *sbuf, const void *data,
		       struct percent_esc *p)
{
	const struct pkg_dir	*dir = data;

	return (mode_val(sbuf, dir->perm, p));
}

/*
 * %Du -- Directory user. TODO: numeric UID
 */
struct sbuf *
format_directory_user(struct sbuf *sbuf, const void *data,
		      struct percent_esc *p)
{
	const struct pkg_dir	*dir = data;

	return (string_val(sbuf, dir->uname, p));
}

/*
 * %F -- Files.  List of filenames (strings) possibly with other
 * meta-data.  Optionally accepts following per-field format in %{ %|
 * %}, where %n is replaced by the filename, %s by the checksum, etc.
 * Default %{%Fn\n%|%}
 */
struct sbuf *
format_files(struct sbuf *sbuf, const void *data, struct percent_esc *p)
{
	const struct pkg	*pkg = data;

	if (p->flags & (PP_ALTERNATE_FORM1|PP_ALTERNATE_FORM2))
		return (list_count(sbuf, pkg_list_count(pkg, PKG_FILES), p));
	else {
		struct pkg_file	*file = NULL;
		int		 count;

		set_list_defaults(p, "%Fn\n", "");

		count = 1;
		while (pkg_files(pkg, &file) == EPKG_OK) {
			if (count > 1)
				iterate_item(sbuf, pkg, sbuf_data(p->sep_fmt),
					     file, count, PP_F);

			iterate_item(sbuf, pkg, sbuf_data(p->item_fmt),
				     file, count, PP_F);
			count++;
		}
	}
	return (sbuf);
}

/*
 * %Fg -- File group.
 */
struct sbuf *
format_file_group(struct sbuf *sbuf, const void *data, struct percent_esc *p)
{
	const struct pkg_file	*file = data;

	return (string_val(sbuf, file->gname, p));
}

/*
 * %Fn -- File path name.
 */
struct sbuf *
format_file_path(struct sbuf *sbuf, const void *data, struct percent_esc *p)
{
	const struct pkg_file	*file = data;

	return (string_val(sbuf, file->path, p));
}

/*
 * %Fp -- File permissions.
 */
struct sbuf *
format_file_perms(struct sbuf *sbuf, const void *data, struct percent_esc *p)
{
	const struct pkg_file	*file = data;

	return (mode_val(sbuf, file->perm, p));
}

/*
 * %Fs -- File SHA256 Checksum.
 */
struct sbuf *
format_file_sha256(struct sbuf *sbuf, const void *data, struct percent_esc *p)
{
	const struct pkg_file	*file = data;

	return (string_val(sbuf, file->sum, p));
}

/*
 * %Fu -- File user.
 */
struct sbuf *
format_file_user(struct sbuf *sbuf, const void *data, struct percent_esc *p)
{
	const struct pkg_file	*file = data;

	return (string_val(sbuf, file->uname, p));
}

/*
 * %G -- Groups. list of string values.  Optionally accepts following
 * per-field format in %{ %| %} where %Gn will be replaced by each
 * groupname or %#Gn by the gid or %Gg by the "gidstr" -- a line from
 * /etc/group. Default %{%Gn\n%|%}
 */
struct sbuf *
format_groups(struct sbuf *sbuf, const void *data, struct percent_esc *p)
{
	const struct pkg	*pkg = data;

	if (p->flags & (PP_ALTERNATE_FORM1|PP_ALTERNATE_FORM2))
		return (list_count(sbuf, pkg_list_count(pkg, PKG_GROUPS), p));
	else {
		struct pkg_group	*group = NULL;
		int			 count;

		set_list_defaults(p, "%Gn\n", "");

		count = 1;
		while(pkg_groups(pkg, &group) == EPKG_OK) {
			if (count > 1)
				iterate_item(sbuf, pkg, sbuf_data(p->sep_fmt),
					     group, count, PP_G);

			iterate_item(sbuf, pkg, sbuf_data(p->item_fmt),
				     group, count, PP_G);
			count++;
		}
	}
	return (sbuf);
}

/*
 * %Gg -- Group 'gidstr' (one line from /etc/group).
 */
struct sbuf *
format_group_gidstr(struct sbuf *sbuf, const void *data, struct percent_esc *p)
{
	const struct pkg_group	*group = data;

	return (string_val(sbuf, group->gidstr, p));
}

/*
 * %Gn -- Group name.
 */
struct sbuf *
format_group_name(struct sbuf *sbuf, const void *data, struct percent_esc *p)
{
	const struct pkg_group	*group = data;

	return (string_val(sbuf, group->name, p));
}

/*
 * %I -- Row counter (integer*). Usually used only in per-field format.
 */
struct sbuf *
format_row_counter(struct sbuf *sbuf, const void *data, struct percent_esc *p)
{
	const int *counter = data;

	return (int_val(sbuf, *counter, p));
}

/*
 * %L -- Licences. List of string values.  Optionally accepts
 * following per-field format in %{ %| %} where %Ln is replaced by the
 * license name and %l by the license logic.  Default %{%n%| %l %}
 */
struct sbuf *
format_licenses(struct sbuf *sbuf, const void *data, struct percent_esc *p)
{
	const struct pkg	*pkg = data;
	struct pkg_strel	*el;
	int			 count = 0;

	if (p->flags & (PP_ALTERNATE_FORM1|PP_ALTERNATE_FORM2)) {
		LL_COUNT(pkg->licenses, el, count);
		return (list_count(sbuf, count, p));
	} else {
		set_list_defaults(p, "%Ln", " %l ");

		count = 1;
		LL_FOREACH(pkg->licenses, el) {
			if (count > 1)
				iterate_item(sbuf, pkg, sbuf_data(p->sep_fmt),
				    el, count, PP_L);

			iterate_item(sbuf, pkg, sbuf_data(p->item_fmt), el,
			    count, PP_L);
			count++;
		}
	}
	return (sbuf);
}

/*
 * %Ln -- License name.
 */
struct sbuf *
format_license_name(struct sbuf *sbuf, const void *data, struct percent_esc *p)
{
	const struct pkg_strel	*el = data;

	return (string_val(sbuf, el->value, p));
}

/*
 * %M -- Pkg message. string.  Accepts field-width, left-align
 */
struct sbuf *
format_message(struct sbuf *sbuf, const void *data, struct percent_esc *p)
{
	const struct pkg	*pkg = data;

	return (string_val(sbuf, pkg->message, p));
}

/*
 * %N -- Repository identity. string.  Accepts field-width, left-align
 */
struct sbuf *
format_repo_ident(struct sbuf *sbuf, const void *data, struct percent_esc *p)
{
	const struct pkg	*pkg = data;
	const char		*reponame;

	reponame = pkg->reponame;
	if (reponame == NULL) {
		reponame = pkg_kv_get(&pkg->annotations, "repository");
		if (reponame == NULL)
			reponame = "unknown-repository";
	}
	return (string_val(sbuf, reponame, p));
}

/*
 * %O -- Options. list of {option,value} tuples. Optionally accepts
 * following per-field format in %{ %| %}, where %On is replaced by the
 * option name and %Ov by the value.  Default %{%On %Ov\n%|%}
 */ 
struct sbuf *
format_options(struct sbuf *sbuf, const void *data, struct percent_esc *p)
{
	const struct pkg	*pkg = data;

	if (p->flags & (PP_ALTERNATE_FORM1|PP_ALTERNATE_FORM2))
		return (list_count(sbuf, pkg_list_count(pkg, PKG_OPTIONS), p));
	else {
		struct pkg_option	*opt = NULL;
		int			 count;

		set_list_defaults(p, "%On %Ov\n", "");

		count = 1;
		while (pkg_options(pkg, &opt) == EPKG_OK) {
			if (count > 1)
				iterate_item(sbuf, pkg, sbuf_data(p->sep_fmt),
					     opt, count, PP_O);

			iterate_item(sbuf, pkg, sbuf_data(p->item_fmt),
				     opt, count, PP_O);
			count++;
		}
	}
	return (sbuf);
}

/*
 * %On -- Option name.
 */
struct sbuf *
format_option_name(struct sbuf *sbuf, const void *data, struct percent_esc *p)
{
	const struct pkg_option	*option = data;

	return (string_val(sbuf, option->key, p));
}

/*
 * %Ov -- Option value.
 */
struct sbuf *
format_option_value(struct sbuf *sbuf, const void *data, struct percent_esc *p)
{
	const struct pkg_option	*option = data;

	return (string_val(sbuf, option->value, p));
}

/*
 * %Od -- Option default value.
 */
struct sbuf *
format_option_default(struct sbuf *sbuf, const void *data, struct percent_esc *p)
{
	const struct pkg_option	*option = data;

	return (string_val(sbuf, option->value, p));
}

/*
 * %OD -- Option description
 */
struct sbuf *
format_option_description(struct sbuf *sbuf, const void *data, struct percent_esc *p)
{
	const struct pkg_option	*option = data;

	return (string_val(sbuf, option->description, p));
}

/*
 * %R -- Repo path. string.
 */
struct sbuf *
format_repo_path(struct sbuf *sbuf, const void *data, struct percent_esc *p)
{
	const struct pkg	*pkg = data;

	return (string_val(sbuf, pkg->repopath, p));
}

/*
 * %S -- Character string.
 */
struct sbuf *
format_char_string(struct sbuf *sbuf, const void *data, struct percent_esc *p)
{
	const char	*charstring = data;

	return (string_val(sbuf, charstring, p));
}

/*
 * %U -- Users. list of string values.  Optionally accepts following
 * per-field format in %{ %| %} where %Un will be replaced by each
 * username or %#Un by the uid or %Uu by the uidstr -- a line from
 * /etc/passwd. Default %{%Un\n%|%}
 */
struct sbuf *
format_users(struct sbuf *sbuf, const void *data, struct percent_esc *p)
{
	const struct pkg	*pkg = data;

	if (p->flags & (PP_ALTERNATE_FORM1|PP_ALTERNATE_FORM2))
		return (list_count(sbuf, pkg_list_count(pkg, PKG_USERS), p));
	else {
		struct pkg_user	*user = NULL;
		int		 count;

		set_list_defaults(p, "%Un\n", "");

		count = 1;
		while (pkg_users(pkg, &user) == EPKG_OK) {
			if (count > 1)
				iterate_item(sbuf, pkg, sbuf_data(p->sep_fmt),
					     user, count, PP_U);

			iterate_item(sbuf, pkg, sbuf_data(p->item_fmt),
				     user, count, PP_U);
			count++;
		}
	}
	return (sbuf);
}

/*
 * %Un -- User name.
 */
struct sbuf *
format_user_name(struct sbuf *sbuf, const void *data, struct percent_esc *p)
{
	const struct pkg_user	*user = data;

	return (string_val(sbuf, user->name, p));
}

/*
 * %Uu -- User uidstr (one line from /etc/passwd).
 */
struct sbuf *
format_user_uidstr(struct sbuf *sbuf, const void *data, struct percent_esc *p)
{
	const struct pkg_user	*user = data;

	return (string_val(sbuf, user->uidstr, p));
}

/*
 * %V -- Old package version. string. Accepts field width, left align
 */
struct sbuf *
format_old_version(struct sbuf *sbuf, const void *data, struct percent_esc *p)
{
	const struct pkg	*pkg = data;

	return (string_val(sbuf, pkg->old_version, p));
}

/*
 * %a -- Autoremove flag. boolean.  Accepts field-width, left-align.
 * Standard form: 0, 1.  Alternate form1: no, yes.  Alternate form2:
 * false, true
 */
struct sbuf *
format_autoremove(struct sbuf *sbuf, const void *data, struct percent_esc *p)
{
	const struct pkg	*pkg = data;

	return (bool_val(sbuf, pkg->automatic, p));
}


/*
 * %b -- Provided Shared Libraries.  List of shlibs provided by
 * binaries in the pkg.  Optionally accepts per-field format in %{ %|
 * %}, where %n is replaced by the shlib name.  Default %{%bn\n%|%}
 */
struct sbuf *
format_shlibs_provided(struct sbuf *sbuf, const void *data, struct percent_esc *p)
{
	const struct pkg	*pkg = data;

	if (p->flags & (PP_ALTERNATE_FORM1|PP_ALTERNATE_FORM2))
		return (list_count(sbuf, pkg_list_count(pkg, PKG_SHLIBS_PROVIDED), p));
	else {
		struct pkg_shlib	*shlib = NULL;
		int			 count;

		set_list_defaults(p, "%bn\n", "");

		count = 1;
		while (pkg_shlibs_provided(pkg, &shlib) == EPKG_OK) {
			if (count > 1)
				iterate_item(sbuf, pkg, sbuf_data(p->sep_fmt),
					     shlib, count, PP_b);

			iterate_item(sbuf, pkg, sbuf_data(p->item_fmt),
				     shlib, count, PP_b);
			count++;
		}
	}
	return (sbuf);
}

/*
 * %c -- Comment. string.  Accepts field-width, left-align
 */
struct sbuf *
format_comment(struct sbuf *sbuf, const void *data, struct percent_esc *p)
{
	const struct pkg	*pkg = data;

	return (string_val(sbuf, pkg->comment, p));
}

/*
 * %d -- Dependencies. List of pkgs. Can be optionally followed by
 * per-field format string in %{ %| %} using any pkg_printf() *scalar*
 * formats. Defaults to printing "%dn-%dv\n" for each dependency.
 */
struct sbuf *
format_dependencies(struct sbuf *sbuf, const void *data, struct percent_esc *p)
{
	const struct pkg	*pkg = data;

	if (p->flags & (PP_ALTERNATE_FORM1|PP_ALTERNATE_FORM2))
		return (list_count(sbuf, pkg_list_count(pkg, PKG_DEPS), p));
	else {
		struct pkg_dep	*dep = NULL;
		int		 count;

		set_list_defaults(p, "%dn-%dv\n", "");

		count = 1;
		while (pkg_deps(pkg, &dep) == EPKG_OK) {
			if (count > 1)
				iterate_item(sbuf, pkg, sbuf_data(p->sep_fmt),
					     dep, count, PP_d);

			iterate_item(sbuf, pkg, sbuf_data(p->item_fmt),
				     dep, count, PP_d);
			count++;
		}
	}
	return (sbuf);
}

/*
 * %dk -- Dependency lock status or %rk -- Requirement lock status.
 */
struct sbuf *
format_dependency_lock(struct sbuf *sbuf, const void *data,
		       struct percent_esc *p)
{
	const struct pkg_dep	*dep = data;

	return (bool_val(sbuf, pkg_dep_is_locked(dep), p));
}

/*
 * %dn -- Dependency name or %rn -- Requirement name.
 */
struct sbuf *
format_dependency_name(struct sbuf *sbuf, const void *data,
		       struct percent_esc *p)
{
	const struct pkg_dep	*dep = data;

	return (string_val(sbuf, dep->name, p));
}

/*
 * %do -- Dependency origin or %ro -- Requirement origin.
 */
struct sbuf *
format_dependency_origin(struct sbuf *sbuf, const void *data,
			 struct percent_esc *p)
{
	const struct pkg_dep	*dep = data;

	return (string_val(sbuf, dep->origin, p));
}

/*
 * %dv -- Dependency version or %rv -- Requirement version.
 */
struct sbuf *
format_dependency_version(struct sbuf *sbuf, const void *data,
			  struct percent_esc *p)
{
	const struct pkg_dep	*dep = data;

	return (string_val(sbuf, dep->version, p));
}

/*
 * %e -- Description. string. Accepts field-width, left-align
 */
struct sbuf *
format_description(struct sbuf *sbuf, const void *data, struct percent_esc *p)
{
	const struct pkg	*pkg = data;

	return (string_val(sbuf, pkg->desc, p));
}

/*
 * %k -- Locked flag. boolean.  Accepts field-width, left-align.
 * Standard form: 0, 1.  Alternate form1: no, yes.  Alternate form2:
 * false, true
 */
struct sbuf *
format_lock_status(struct sbuf *sbuf, const void *data, struct percent_esc *p)
{
	const struct pkg	*pkg = data;

	return (bool_val(sbuf, pkg->locked, p));
}

/*
 * %l -- Licence logic. string.  Accepts field-width, left-align.
 * Standard form: and, or, single. Alternate form 1: &, |, ''.
 * Alternate form 2: &&, ||, ==
 */
struct sbuf *
format_license_logic(struct sbuf *sbuf, const void *data, struct percent_esc *p)
{
	const struct pkg	*pkg = data;

	return (liclog_val(sbuf, pkg->licenselogic, p));
}

/*
 * %m -- Maintainer e-mail address. string.  Accepts field-width, left-align
 */
struct sbuf *
format_maintainer(struct sbuf *sbuf, const void *data, struct percent_esc *p)
{
	const struct pkg	*pkg = data;

	return (string_val(sbuf, pkg->maintainer, p));
}

/*
 * %n -- Package name. string.  Accepts field-width, left-align
 */
struct sbuf *
format_name(struct sbuf *sbuf, const void *data, struct percent_esc *p)
{
	const struct pkg	*pkg = data;

	return (string_val(sbuf, pkg->name, p));
}

/*
 * %o -- Package origin. string.  Accepts field-width, left-align
 */
struct sbuf *
format_origin(struct sbuf *sbuf, const void *data, struct percent_esc *p)
{
	const struct pkg	*pkg = data;

	return (string_val(sbuf, pkg->origin, p));
}

/*
 * %p -- Installation prefix. string. Accepts field-width, left-align
 */
struct sbuf *
format_prefix(struct sbuf *sbuf, const void *data, struct percent_esc *p)
{
	const struct pkg	*pkg = data;

	return (string_val(sbuf, pkg->prefix, p));
}

/*
 * %q -- pkg architecture a.k.a ABI string.  Accepts field-width, left-align
 */
struct sbuf *
format_architecture(struct sbuf *sbuf, const void *data, struct percent_esc *p)
{
	const struct pkg	*pkg = data;

	return (string_val(sbuf, pkg->arch, p));
}

/*
 * %r -- Requirements. List of pkgs. Can be optionally followed by
 * per-field format string in %{ %| %} using any pkg_printf() *scalar*
 * formats. Defaults to printing "%{%rn-%rv\n%|%}" for each dependency.
 */
struct sbuf *
format_requirements(struct sbuf *sbuf, const void *data, struct percent_esc *p)
{
	const struct pkg	*pkg = data;

	if (p->flags & (PP_ALTERNATE_FORM1|PP_ALTERNATE_FORM2))
		return(list_count(sbuf, pkg_list_count(pkg, PKG_RDEPS), p));
	else {
		struct pkg_dep	*req = NULL;
		int		 count;

		set_list_defaults(p, "%rn-%rv\n", "");

		count = 1;
		while (pkg_rdeps(pkg, &req) == EPKG_OK) {
			if (count > 1)
				iterate_item(sbuf, pkg, sbuf_data(p->sep_fmt),
					     req, count, PP_r);

			iterate_item(sbuf, pkg, sbuf_data(p->item_fmt),
				     req, count, PP_r);
			count++;
		}
	}
	return (sbuf);
}

/*
 * %s -- Size of installed package. integer.  Accepts field-width,
 * left-align, zero-fill, space-for-plus, explicit-plus and
 * alternate-form.  Alternate form is a humanized number using decimal
 * exponents (k, M, G).  Alternate form 2, ditto, but using binary
 * scale prefixes (ki, Mi, Gi etc.)
 */
struct sbuf *
format_flatsize(struct sbuf *sbuf, const void *data, struct percent_esc *p)
{
	const struct pkg	*pkg = data;

	return (int_val(sbuf, pkg->flatsize, p));
}

/*
 * %t -- Installation timestamp (Unix time). integer.  Accepts
 * field-width, left-align.  Can be followed by optional strftime
 * format string in %{ %}.  Default is to print seconds-since-epoch as
 * an integer applying our integer format modifiers.
 */
struct sbuf *
format_install_tstamp(struct sbuf *sbuf, const void *data, struct percent_esc *p)
{
	const struct pkg	*pkg = data;

	if (sbuf_len(p->item_fmt) == 0)
		return (int_val(sbuf, pkg->timestamp, p));
	else {
		char	 buf[1024];
		time_t	 tsv;

		tsv = (time_t)pkg->timestamp;
		strftime(buf, sizeof(buf), sbuf_data(p->item_fmt),
			 localtime(&tsv));
		sbuf_cat(sbuf, buf); 
	}
	return (sbuf);
}

/*
 * %v -- Package version. string. Accepts field width, left align
 */
struct sbuf *
format_version(struct sbuf *sbuf, const void *data, struct percent_esc *p)
{
	const struct pkg	*pkg = data;

	return (string_val(sbuf, pkg->version, p));
}

/*
 * %u -- Package checksum. string. Accepts field width, left align
 */
struct sbuf *
format_checksum(struct sbuf *sbuf, const void *data, struct percent_esc *p)
{
	const struct pkg	*pkg = data;

	return (string_val(sbuf, pkg->sum, p));
}

/*
 * %z -- Package short checksum. string. Accepts field width, left align
 */
struct sbuf *
format_short_checksum(struct sbuf *sbuf, const void *data, struct percent_esc *p)
{
	const struct pkg	*pkg = data;
	char	 csum[PKG_FILE_CKSUM_CHARS + 1];
	int slen;

	if (pkg->sum != NULL)
		slen = MIN(PKG_FILE_CKSUM_CHARS, strlen(pkg->sum));
	else
		slen = 0;
	memcpy(csum, pkg->sum, slen);
	csum[slen] = '\0';

	return (string_val(sbuf, csum, p));
}

/*
 * %w -- Home page URL.  string.  Accepts field width, left align
 */
struct sbuf *
format_home_url(struct sbuf *sbuf, const void *data, struct percent_esc *p)
{
	const struct pkg	*pkg = data;

	return (string_val(sbuf, pkg->www, p));
}

/*
 * %% -- Output a literal '%' character
 */
struct sbuf *
format_literal_percent(struct sbuf *sbuf, __unused const void *data,
		       __unused struct percent_esc *p)
{
	sbuf_putc(sbuf, '%');
	return (sbuf);
}

/*
 * Unknown format code -- return NULL to signal upper layers to pass
 * the text through unchanged.
 */
struct sbuf *
format_unknown(struct sbuf *sbuf, __unused const void *data,
		       __unused struct percent_esc *p)
{
	sbuf_putc(sbuf, '%');
	return (NULL);
}

/* -------------------------------------------------------------- */

struct percent_esc *
new_percent_esc(void)
{
	struct percent_esc	*p; 

	p = calloc(1, sizeof(struct percent_esc));
	if (p != NULL) {
		p->item_fmt = sbuf_new_auto();
		p->sep_fmt = sbuf_new_auto();
	}
	if (p == NULL || p->item_fmt == NULL || p->sep_fmt == NULL) {
		/* out of memory */
		free_percent_esc(p);
		return NULL;
	}
	return (p);
}

struct percent_esc *
clear_percent_esc(struct percent_esc *p)
{
	p->flags = 0;
	p->width = 0;
	p->trailer_status = 0;
	sbuf_clear(p->item_fmt);
	sbuf_finish(p->item_fmt);

	sbuf_clear(p->sep_fmt);
	sbuf_finish(p->sep_fmt);

	p->fmt_code = '\0';

	return (p);
}

void
free_percent_esc(struct percent_esc *p)
{
	if (p) {
		if (p->item_fmt)
			sbuf_delete(p->item_fmt);
		if (p->sep_fmt)
			sbuf_delete(p->sep_fmt);
		free(p);
	}
	return;
}

char *
gen_format(char *buf, size_t buflen, unsigned flags, const char *tail)
{
	int	bp = 0;
	size_t	tlen;

	/* We need the length of tail plus at least 3 characters '%'
	   '*' '\0' but maybe as many as 7 '%' '#' '-' '+' '\'' '*'
	   '\0' */

	tlen = strlen(tail);

	if (buflen - bp < tlen + 3)
		return (NULL);

	buf[bp++] = '%';

	/* PP_ALTERNATE_FORM1 is not used by regular printf(3) */

	/* If PP_EXPLICIT_PLUS and PP_SPACE_FOR_PLUS are both set,
	   the result is formatted according to PP_EXPLICIT_PLUS */

	if ((flags & (PP_EXPLICIT_PLUS|PP_SPACE_FOR_PLUS)) ==
	    (PP_EXPLICIT_PLUS|PP_SPACE_FOR_PLUS))
		flags &= ~(PP_SPACE_FOR_PLUS);

	/* If PP_LEFT_ALIGN and PP_ZERO_PAD are given together,
	   PP_LEFT_ALIGN applies */

	if ((flags & (PP_LEFT_ALIGN|PP_ZERO_PAD)) ==
	    (PP_LEFT_ALIGN|PP_ZERO_PAD))
		flags &= ~(PP_ZERO_PAD);

	if (flags & PP_ALTERNATE_FORM2)
		buf[bp++] = '#';

	if (flags & PP_LEFT_ALIGN)
		buf[bp++] = '-';

	if (flags & PP_ZERO_PAD)
		buf[bp++] = '0';

	if (buflen - bp < tlen + 2)
		return (NULL);
	
	if (flags & PP_EXPLICIT_PLUS)
		buf[bp++] = '+';

	if (flags & PP_SPACE_FOR_PLUS)
		buf[bp++] = ' ';

	if (flags & PP_THOUSANDS_SEP)
		buf[bp++] = '\'';

	if (buflen - bp < tlen + 2)
		return (NULL);

	/* The effect of 0 meaning 'zero fill' is indisinguishable
	   from 0 meaning 'a field width of zero' */

	buf[bp++] = '*';
	buf[bp] = '\0';

	strlcat(buf, tail, buflen);

	return (buf);
}


struct sbuf *
human_number(struct sbuf *sbuf, int64_t number, struct percent_esc *p)
{
	double		 num;
	int		 sign;
	int		 width;
	int		 scale_width;
	int		 divisor;
	int		 scale;
	int		 precision;
	bool		 bin_scale;

#define MAXSCALE	7

	const char	 *bin_pfx[MAXSCALE] =
		{ "", "Ki", "Mi", "Gi", "Ti", "Pi", "Ei" }; 
	const char	 *si_pfx[MAXSCALE] =
		{ "", "k", "M", "G", "T", "P", "E" };
	char		 format[16];

	bin_scale = ((p->flags & PP_ALTERNATE_FORM2) != 0);

	p->flags &= ~(PP_ALTERNATE_FORM1|PP_ALTERNATE_FORM2);

	if (gen_format(format, sizeof(format), p->flags, ".*f") == NULL)
		return (NULL);

	if (number >= 0) {
		num = number;
		sign = 1;
	} else {
		num = -number;
		sign = -1;
	}

	divisor = bin_scale ? 1024 : 1000;

	for (scale = 0; scale < MAXSCALE; scale++) {
		if (num < divisor)
			break;
		num /= divisor;
	}

	if (scale == 0)
		scale_width = 0;
	else if (bin_scale)
		scale_width = 2;
	else
		scale_width = 1;

	if (p->width == 0)
		width = 0;
	else if (p->width <= scale_width)
		width = 1;
	else
		width = p->width - scale_width;

	if (num >= 100)
		precision = 0;
	else if (num >= 10) {
		if (width == 0 || width > 3)
			precision = 1;
		else
			precision = 0;
	} else {
		if (width == 0 || width > 3)
			precision = 2;
		else if (width == 3)
			precision = 1;
		else
			precision = 0;
	}

	sbuf_printf(sbuf, format, width, precision, num * sign);

	if (scale > 0)
		sbuf_printf(sbuf, "%s",
		    bin_scale ? bin_pfx[scale] : si_pfx[scale]);

	return (sbuf);
}

struct sbuf *
string_val(struct sbuf *sbuf, const char *str, struct percent_esc *p)
{
	char	format[16];

	/* The '#' '?' '+' ' ' and '\'' modifiers have no meaning for
	   strings */

	p->flags &= ~(PP_ALTERNATE_FORM1 |
		      PP_ALTERNATE_FORM2 |
		      PP_EXPLICIT_PLUS   |
		      PP_SPACE_FOR_PLUS  |
		      PP_THOUSANDS_SEP);

	if (gen_format(format, sizeof(format), p->flags, "s") == NULL)
		return (NULL);

	sbuf_printf(sbuf, format, p->width, str);
	return (sbuf);
}

struct sbuf *
int_val(struct sbuf *sbuf, int64_t value, struct percent_esc *p)
{
	if (p->flags & (PP_ALTERNATE_FORM1|PP_ALTERNATE_FORM2))
		return (human_number(sbuf, value, p));
	else {
		char	 format[16];

		if (gen_format(format, sizeof(format), p->flags, PRId64)
		    == NULL)
			return (NULL);

		sbuf_printf(sbuf, format, p->width, value);
	}
	return (sbuf);
}

struct sbuf *
bool_val(struct sbuf *sbuf, bool value, struct percent_esc *p)
{
	static const char	*boolean_str[2][3] = {
		[false]	= { "false", "no",  ""    },
		[true]  = { "true",  "yes", "(*)" },
	};
	int	alternate;

	if (p->flags & PP_ALTERNATE_FORM2)
		alternate = 2;
	else if (p->flags & PP_ALTERNATE_FORM1)
		alternate = 1;
	else
		alternate = 0;

	p->flags &= ~(PP_ALTERNATE_FORM1|PP_ALTERNATE_FORM2);

	return (string_val(sbuf, boolean_str[value][alternate], p));
}

struct sbuf *
mode_val(struct sbuf *sbuf, mode_t mode, struct percent_esc *p)
{
	/*
         * Print mode as an octal integer '%o' by default.
	 * PP_ALTERNATE_FORM2 generates '%#o' pased to regular
	 * printf(). PP_ALTERNATE_FORM1 will generate 'drwxr-x--- '
	 * style from strmode(3).
	 */

	if (p->flags & PP_ALTERNATE_FORM1) {
		char	modebuf[12];

		strmode(mode, modebuf);

		return (string_val(sbuf, modebuf, p));
	} else {
		char	format[16];

		/*
		 * Should the mode when expressed as a numeric value
		 * in octal include the bits that indicate the inode
		 * type?  Generally no, but since mode is
		 * intrinsically an unsigned type, overload
		 * PP_EXPLICIT_PLUS to mean 'show bits for the inode
		 * type'
		 */

		if ( (p->flags & PP_EXPLICIT_PLUS) == 0 )
			mode &= ALLPERMS;

		p->flags &= ~(PP_ALTERNATE_FORM1|PP_EXPLICIT_PLUS);

		if (gen_format(format, sizeof(format), p->flags, PRIo16)
		    == NULL)
			return (NULL);

		sbuf_printf(sbuf, format, p->width, mode);
	}
	return (sbuf);
}

struct sbuf *
liclog_val(struct sbuf *sbuf, lic_t licenselogic, struct percent_esc *p)
{
	int			 alternate;
	int			 llogic = PP_LIC_SINGLE;

	static const char	*liclog_str[3][3] = {
		[PP_LIC_SINGLE] = { "single", "",  "==" },
		[PP_LIC_OR]     = { "or",     "|", "||" },
		[PP_LIC_AND]    = { "and",    "&", "&&" },
	};

	switch (licenselogic) {
	case LICENSE_SINGLE:
		llogic = PP_LIC_SINGLE;
		break;
	case LICENSE_OR:
		llogic = PP_LIC_OR;
		break;
	case LICENSE_AND:
		llogic = PP_LIC_AND;
		break;
	}

	if (p->flags & PP_ALTERNATE_FORM2)
		alternate = 2;
	else if (p->flags & PP_ALTERNATE_FORM1)
		alternate = 1;
	else
		alternate = 0;

	p->flags &= ~(PP_ALTERNATE_FORM1|PP_ALTERNATE_FORM2);

	return (string_val(sbuf, liclog_str[llogic][alternate], p));
}

struct sbuf *
list_count(struct sbuf *sbuf, int64_t count, struct percent_esc *p)
{
	/* Convert to 0 or 1 for %?X */
	if (p->flags & PP_ALTERNATE_FORM1)
		count = (count > 0);

	/* Turn off %#X and %?X flags, then print as a normal integer */
	p->flags &= ~(PP_ALTERNATE_FORM1|PP_ALTERNATE_FORM2);

	return (int_val(sbuf, count, p));
}

struct percent_esc *
set_list_defaults(struct percent_esc *p, const char *item_fmt,
		  const char *sep_fmt)
{
	if ((p->trailer_status & ITEM_FMT_SET) != ITEM_FMT_SET) {
		sbuf_cat(p->item_fmt, item_fmt);
		sbuf_finish(p->item_fmt);
		p->trailer_status |= ITEM_FMT_SET;
	}
	if ((p->trailer_status & SEP_FMT_SET) != SEP_FMT_SET) {
		sbuf_cat(p->sep_fmt, sep_fmt);
		sbuf_finish(p->sep_fmt);
		p->trailer_status |= SEP_FMT_SET;
	}
	return (p);
}

struct sbuf *
iterate_item(struct sbuf *sbuf, const struct pkg *pkg, const char *format,
	     const void *data, int count, unsigned context)
{
	const char		*f;
	struct percent_esc	*p;

	/* Scan the format string and interpret any escapes */

	f = format;
	p = new_percent_esc();

	if (p == NULL) {
		sbuf_clear(sbuf);
		return (sbuf);	/* Out of memory */
	}

	while ( *f != '\0' ) {
		switch(*f) {
		case '%':
			f = process_format_trailer(sbuf, p, f, pkg, data, count, context);
			break;
		case '\\':
			f = process_escape(sbuf, f);
			break;
		default:
			sbuf_putc(sbuf, *f);
			f++;
			break;
		}
		if (f == NULL) {
			sbuf_clear(sbuf);
			break;	/* Out of memory */
		}
	}

	free_percent_esc(p);
	return (sbuf);
}

const char *
field_modifier(const char *f, struct percent_esc *p)
{
	bool	done;

	/* Field modifiers, if any:
	   '?' alternate form 1
	   '#' alternate form 2
	   '-' left align
	   '+' explicit plus sign (numerics only)
	   ' ' space instead of plus sign (numerics only)
	   '0' pad with zeroes (numerics only)
           '\'' use thousands separator (numerics only)
	   Note '*' (dynamic field width) is not supported */

	done = false;
	while (!done) {
		switch (*f) {
		case '?':
			p->flags |= PP_ALTERNATE_FORM1;
			break;
		case '#':
			p->flags |= PP_ALTERNATE_FORM2;
			break;
		case '-':
			p->flags |= PP_LEFT_ALIGN;
			break;
		case '+':
			p->flags |= PP_EXPLICIT_PLUS;
			break;
		case ' ':
			p->flags |= PP_SPACE_FOR_PLUS;
			break;
		case '0':
			p->flags |= PP_ZERO_PAD;
			break;
		case '\'':
			p->flags |= PP_THOUSANDS_SEP;
			break;
		default:
			done = true;
			break;
		}
		if (!done)
			f++;
	}
	return (f);
}

const char *
field_width(const char *f, struct percent_esc *p)
{
	bool	done;

	/* Field width, if any -- some number of decimal digits.
	   Note: field width set to zero could be interpreted as using
	   0 to request zero padding: it doesn't matter which -- the
	   result on output is exactly the same. */

	done = false;
	while (!done) {
		switch(*f) {
		case '0':
			p->width = p->width * 10 + 0;
			break;
		case '1':
			p->width = p->width * 10 + 1;
			break;
		case '2':
			p->width = p->width * 10 + 2;
			break;
		case '3':
			p->width = p->width * 10 + 3;
			break;
		case '4':
			p->width = p->width * 10 + 4;
			break;
		case '5':
			p->width = p->width * 10 + 5;
			break;
		case '6':
			p->width = p->width * 10 + 6;
			break;
		case '7':
			p->width = p->width * 10 + 7;
			break;
		case '8':
			p->width = p->width * 10 + 8;
			break;
		case '9':
			p->width = p->width * 10 + 9;
			break;
		default:
			done = true;
			break;
		}
		if (!done)
			f++;
	}
	return (f);
}

const char *
format_trailer(const char *f, struct percent_esc *p)
{

	/* is the trailer even present? */

	if (f[0] == '%' && f[1] == '{') {
		bool		 sep = false;
		bool		 done = false;
		const char	*f1;
		const char	*f2;

		p->trailer_status |= ITEM_FMT_SET;
		f1 = f + 2;

		for (f2 = f1; *f2 != '\0'; f2++) {
			if (f2[0] == '%' && ( f2[1] == '}' || f2[1] == '|')) {
				if (f2[1] == '|')
					sep = true;
				else
					done = true;
				f1 = f2 + 2;
				break;
			}
			sbuf_putc(p->item_fmt, *f2);
		}


		if (sep) {
			p->trailer_status |= SEP_FMT_SET;
			done = false;

			for (f2 = f1; *f2 != '\0'; f2++) {
				if (f2[0] == '%' && f2[1] == '}') {
					done = true;
					f1 = f2 + 2;
					break;
				}
				sbuf_putc(p->sep_fmt, *f2);
			}
			
		}
		
		if (done) {
			f = f1;
		} else {
			sbuf_clear(p->item_fmt);
			sbuf_clear(p->sep_fmt);
		}
		sbuf_finish(p->item_fmt);
		sbuf_finish(p->sep_fmt);
	}

	return (f);
}

const char *
format_code(const char *f, unsigned context, struct percent_esc *p)
{
	fmt_code_t	fmt_code;

	p->fmt_code = PP_UNKNOWN; /* Assume unknown, for contradiction */

	/* The next character or two will be a format code -- look
	   these up in the fmt table to make sure they are allowed in
	   context.  This could be optimized since the format codes
	   are arranged alphabetically in the fmt[] array. */

	for (fmt_code = 0; fmt_code < PP_END_MARKER; fmt_code++) {
		if ((fmt[fmt_code].context & context) != context)
			continue;
		if (fmt[fmt_code].fmt_main != f[0])
			continue;
		if (fmt[fmt_code].fmt_sub == f[1] && f[1] != '\0') {
			p->fmt_code = fmt_code;
			f += 2;
			break;
		}
		if (fmt[fmt_code].fmt_sub == '\0') {
			p->fmt_code = fmt_code;
			f++;
			break;
		}
	}

	return (f);
}

const char *
parse_format(const char *f, unsigned context, struct percent_esc *p)
{
	f++;			/* Eat the % */

	f = field_modifier(f, p);

	f = field_width(f, p);

	f = format_code(f, context, p);

	/* Does this format take a trailing list item/separator format
	   like %{...%|...%} ?  It's only the list-valued items that
	   do, and they can only take it at the top level (context ==
	   PP_PKG).  Also, they only take the trailing stuff in the
	   absence of %?X or %#X modifiers. */

	if ((context & PP_PKG) == PP_PKG &&
	    fmt[p->fmt_code].has_trailer &&
	    (p->flags & (PP_ALTERNATE_FORM1|PP_ALTERNATE_FORM2)) == 0)
		f = format_trailer(f, p);

	return (f);
}

const char*
maybe_read_hex_byte(struct sbuf *sbuf, const char *f)
{
	/* Hex escapes are of the form \xNN -- always two hex digits */

	f++;			/* eat the x */

	if (isxdigit(f[0]) && isxdigit(f[1])) {
		int	val;

		switch(*f) {
		case '0':
			val = 0x0;
			break;
		case '1':
			val = 0x10;
			break;
		case '2':
			val = 0x20;
			break;
		case '3':
			val = 0x30;
			break;
		case '4':
			val = 0x40;
			break;
		case '5':
			val = 0x50;
			break;
		case '6':
			val = 0x60;
			break;
		case '7':
			val = 0x70;
			break;
		case '8':
			val = 0x80;
			break;
		case '9':
			val = 0x90;
			break;
		case 'a':
		case 'A':
			val = 0xa0;
			break;
		case 'b':
		case 'B':
			val = 0xb0;
			break;
		case 'c':
		case 'C':
			val = 0xc0;
			break;
		case 'd':
		case 'D':
			val = 0xd0;
			break;
		case 'e':
		case 'E':
			val = 0xe0;
			break;
		case 'f':
		case 'F':
			val = 0xf0;
			break;
		default:
			/* This case is to shut up the over-picky
			 * compiler warnings about use of an
			 * uninitialised value. It can't actually
			 * be reached.  */
			val = 0x0;
			break;
		}

		f++;

		switch(*f) {
		case '0':
			val += 0x0;
			break;
		case '1':
			val += 0x1;
			break;
		case '2':
			val += 0x2;
			break;
		case '3':
			val += 0x3;
			break;
		case '4':
			val += 0x4;
			break;
		case '5':
			val += 0x5;
			break;
		case '6':
			val += 0x6;
			break;
		case '7':
			val += 0x7;
			break;
		case '8':
			val += 0x8;
			break;
		case '9':
			val += 0x9;
			break;
		case 'a':
		case 'A':
			val += 0xa;
			break;
		case 'b':
		case 'B':
			val += 0xb;
			break;
		case 'c':
		case 'C':
			val += 0xc;
			break;
		case 'd':
		case 'D':
			val += 0xd;
			break;
		case 'e':
		case 'E':
			val += 0xe;
			break;
		case 'f':
		case 'F':
			val += 0xf;
			break;
		}

		sbuf_putc(sbuf, val);
		f++;
	} else {
		/* Pass through unchanged if it's not a recognizable
		   hex byte. */
		sbuf_putc(sbuf, '\\');
		sbuf_putc(sbuf, 'x');
	}
	return (f);
}

const char*
read_oct_byte(struct sbuf *sbuf, const char *f)
{
	int	val = 0;
	int	count = 0;

	/* Octal escapes are upto three octal digits: \N, \NN or \NNN
	   up to a max of \377.  Note: this treats \400 as \40
	   followed by character 0 passed through unchanged. */

	while (val < 32 && count++ < 3) {
		switch (*f) {
		case '0':
			val = val * 8 + 0;
			break;
		case '1':
			val = val * 8 + 1;
			break;
		case '2':
			val = val * 8 + 2;
			break;
		case '3':
			val = val * 8 + 3;
			break;
		case '4':
			val = val * 8 + 4;
			break;
		case '5':
			val = val * 8 + 5;
			break;
		case '6':
			val = val * 8 + 6;
			break;
		case '7':
			val = val * 8 + 7;
			break;
		default:	/* Non-octal digit */
			goto done;
		}

		f++;
	} 
done:
	sbuf_putc(sbuf, val);

	return (f);
}

const char *
process_escape(struct sbuf *sbuf, const char *f)
{
	f++;			/* Eat the \ */

	switch (*f) {
	case 'a':
		sbuf_putc(sbuf, '\a');
		f++;
		break;
	case 'b':
		sbuf_putc(sbuf, '\b');
		f++;
		break;
	case 'f':
		sbuf_putc(sbuf, '\f');
		f++;
		break;
	case 'n':
		sbuf_putc(sbuf, '\n');
		f++;
		break;
	case 't':
		sbuf_putc(sbuf, '\t');
		f++;
		break;
	case 'v':
		sbuf_putc(sbuf, '\v');
		f++;
		break;
	case '\'':
		sbuf_putc(sbuf, '\'');
		f++;
		break;
	case '"':
		sbuf_putc(sbuf, '"');
		f++;
		break;
	case '\\':
		sbuf_putc(sbuf, '\\');
		f++;
		break;
	case 'x':		/* Hex escape: \xNN */
		f = maybe_read_hex_byte(sbuf, f);
		break;
	case '0':
	case '1':
	case '2':
	case '3':
	case '4':
	case '5':
	case '6':
	case '7':		/* Oct escape: all fall through */
		f = read_oct_byte(sbuf, f);
		break;
	default:		/* If it's not a recognised escape,
				   leave f pointing at the escaped
				   character */
		sbuf_putc(sbuf, '\\');
		break;
	}

	return (f);
}

const char *
process_format_trailer(struct sbuf *sbuf, struct percent_esc *p,
		       const char *f, const struct pkg *pkg, 
		       const void *data, int count, unsigned context)
{
	const char		*fstart;
	struct sbuf		*s;

	fstart = f;
	f = parse_format(f, context, p);

	if (p->fmt_code == PP_ROW_COUNTER)
		s = fmt[p->fmt_code].fmt_handler(sbuf, &count, p);
	else if (p->fmt_code > PP_LAST_FORMAT)
		s = fmt[p->fmt_code].fmt_handler(sbuf, NULL, p);
	else if (fmt[p->fmt_code].struct_pkg)
		s = fmt[p->fmt_code].fmt_handler(sbuf, pkg, p);
	else
		s = fmt[p->fmt_code].fmt_handler(sbuf, data, p);


	if (s == NULL) {
		f = fstart + 1;	/* Eat just the % on error */
	}

	clear_percent_esc(p);

	return (f);
}

const char *
process_format_main(struct sbuf *sbuf, struct percent_esc *p,
		const char *fstart, const char *fend, void *data)
{
	struct sbuf		*s;

	s = fmt[p->fmt_code].fmt_handler(sbuf, data, p);

	clear_percent_esc(p);

	/* Pass through unprocessed on error */
	return (s == NULL ? fstart : fend);
}

/**
 * print to stdout data from pkg as indicated by the format code format
 * @param ... Varargs list of struct pkg etc. supplying the data
 * @param format String with embedded %-escapes indicating what to print
 * @return count of the number of characters printed
 */
int
pkg_printf(const char * restrict format, ...)
{
	int		 count;
	va_list		 ap;

	va_start(ap, format);
	count = pkg_vprintf(format, ap);
	va_end(ap);

	return (count);
}

/**
 * print to stdout data from pkg as indicated by the format code format
 * @param ap Varargs list of struct pkg etc. supplying the data
 * @param format String with embedded %-escapes indicating what to print
 * @return count of the number of characters printed
 */
int
pkg_vprintf(const char * restrict format, va_list ap)
{
	struct sbuf	*sbuf;
	int		 count;

	sbuf  = sbuf_new_auto();

	if (sbuf)
		sbuf = pkg_sbuf_vprintf(sbuf, format, ap);
	if (sbuf && sbuf_len(sbuf) >= 0) {
		sbuf_finish(sbuf);
		count = printf("%s", sbuf_data(sbuf));
	} else
		count = -1;
	if (sbuf)
		sbuf_delete(sbuf);
	return (count);
}

/**
 * print to named stream from pkg as indicated by the format code format
 * @param ... Varargs list of struct pkg etc. supplying the data
 * @param format String with embedded %-escapes indicating what to output
 * @return count of the number of characters printed
 */
int
pkg_fprintf(FILE * restrict stream, const char * restrict format, ...)
{
	int		 count;
	va_list		 ap;

	va_start(ap, format);
	count = pkg_vfprintf(stream, format, ap);
	va_end(ap);

	return (count);
}

/**
 * print to named stream from pkg as indicated by the format code format
 * @param ap Varargs list of struct pkg etc. supplying the data
 * @param format String with embedded %-escapes indicating what to output
 * @return count of the number of characters printed
 */
int
pkg_vfprintf(FILE * restrict stream, const char * restrict format, va_list ap)
{
	struct sbuf	*sbuf;
	int		 count;

	sbuf  = sbuf_new_auto();

	if (sbuf)
		sbuf = pkg_sbuf_vprintf(sbuf, format, ap);
	if (sbuf && sbuf_len(sbuf) >= 0) {
		sbuf_finish(sbuf);
		count = fprintf(stream, "%s", sbuf_data(sbuf));
	} else
		count = -1;
	if (sbuf)
		sbuf_delete(sbuf);
	return (count);
}

/**
 * print to file descriptor fd data from pkg as indicated by the format
 * code format
 * @param fd Previously opened file descriptor to print to
 * @param ... Varargs list of struct pkg etc. supplying the data
 * @param format String with embedded %-escapes indicating what to print
 * @return count of the number of characters printed
 */
int
pkg_dprintf(int fd, const char * restrict format, ...)
{
	int		 count;
	va_list		 ap;

	va_start(ap, format);
	count = pkg_vdprintf(fd, format, ap);
	va_end(ap);

	return (count);
}

/**
 * print to file descriptor fd data from pkg as indicated by the format
 * code format
 * @param fd Previously opened file descriptor to print to
 * @param ap Varargs list of struct pkg etc. supplying the data
 * @param format String with embedded %-escapes indicating what to print
 * @return count of the number of characters printed
 */
int
pkg_vdprintf(int fd, const char * restrict format, va_list ap)
{
	struct sbuf	*sbuf;
	int		 count;

	sbuf  = sbuf_new_auto();

	if (sbuf)
		sbuf = pkg_sbuf_vprintf(sbuf, format, ap);
	if (sbuf && sbuf_len(sbuf) >= 0) {
		sbuf_finish(sbuf);
		count = dprintf(fd, "%s", sbuf_data(sbuf));
	} else 
		count = -1;
	if (sbuf)
		sbuf_delete(sbuf);
	return (count);
}

/**
 * print to buffer str of given size data from pkg as indicated by the
 * format code format as a NULL-terminated string
 * @param str Character array buffer to receive output
 * @param size Length of the buffer str
 * @param ... Varargs list of struct pkg etc. supplying the data
 * @param format String with embedded %-escapes indicating what to output
 * @return count of the number of characters that would have been output
 * disregarding truncation to fit size
 */
int
pkg_snprintf(char * restrict str, size_t size, const char * restrict format, ...)
{
	int		 count;
	va_list		 ap;

	va_start(ap, format);
	count = pkg_vsnprintf(str, size, format, ap);
	va_end(ap);

	return (count);
}

/**
 * print to buffer str of given size data from pkg as indicated by the
 * format code format as a NULL-terminated string
 * @param str Character array buffer to receive output
 * @param size Length of the buffer str
 * @param ap Varargs list of struct pkg etc. supplying the data
 * @param format String with embedded %-escapes indicating what to output
 * @return count of the number of characters that would have been output
 * disregarding truncation to fit size
 */
int
pkg_vsnprintf(char * restrict str, size_t size, const char * restrict format,
	     va_list ap)
{
	struct sbuf	*sbuf;
	int		 count;

	sbuf  = sbuf_new_auto();

	if (sbuf)
		sbuf = pkg_sbuf_vprintf(sbuf, format, ap);
	if (sbuf && sbuf_len(sbuf) >= 0) {
		sbuf_finish(sbuf);
		count = snprintf(str, size, "%s", sbuf_data(sbuf));
	} else
		count = -1;
	if (sbuf)
		sbuf_delete(sbuf);

	return (count);
}

/**
 * Allocate a string buffer ret sufficiently big to contain formatted
 * data data from pkg as indicated by the format code format
 * @param ret location of pointer to be set to point to buffer containing
 * result 
 * @param ... Varargs list of struct pkg etc. supplying the data
 * @param format String with embedded %-escapes indicating what to output
 * @return count of the number of characters printed
 */
int
pkg_asprintf(char **ret, const char * restrict format, ...)
{
	int		 count;
	va_list		 ap;

	va_start(ap, format);
	count = pkg_vasprintf(ret, format, ap);
	va_end(ap);

	return (count);
}

/**
 * Allocate a string buffer ret sufficiently big to contain formatted
 * data data from pkg as indicated by the format code format
 * @param ret location of pointer to be set to point to buffer containing
 * result 
 * @param ap Varargs list of struct pkg etc. supplying the data
 * @param format String with embedded %-escapes indicating what to output
 * @return count of the number of characters printed
 */
int
pkg_vasprintf(char **ret, const char * restrict format, va_list ap)
{
	struct sbuf	*sbuf;
	int		 count;

	sbuf  = sbuf_new_auto();

	if (sbuf)
		sbuf = pkg_sbuf_vprintf(sbuf, format, ap);
	if (sbuf && sbuf_len(sbuf) >= 0) {
		sbuf_finish(sbuf);
		count = asprintf(ret, "%s", sbuf_data(sbuf));
	} else {
		count = -1;
		*ret = NULL;
	}
	if (sbuf)
		sbuf_delete(sbuf);
	return (count);
}

/**
 * store data from pkg into sbuf as indicated by the format code format.
 * @param sbuf contains the result
 * @param ... Varargs list of struct pkg etc. supplying the data
 * @param format String with embedded %-escapes indicating what to output
 * @return count of the number of characters in the result
 */
struct sbuf *
pkg_sbuf_printf(struct sbuf * restrict sbuf, const char *restrict format, ...)
{
	va_list		 ap;

	va_start(ap, format);
	sbuf = pkg_sbuf_vprintf(sbuf, format, ap);
	va_end(ap);

	return (sbuf);
}

/**
 * store data from pkg into sbuf as indicated by the format code format.
 * This is the core function called by all the other pkg_printf() family.
 * @param sbuf contains the result
 * @param ap Arglist with struct pkg etc. supplying the data
 * @param format String with embedded %-escapes indicating what to output
 * @return count of the number of characters in the result
 */
struct sbuf *
pkg_sbuf_vprintf(struct sbuf * restrict sbuf, const char * restrict format,
		 va_list ap)
{
	const char		*f, *fend;
	struct percent_esc	*p;
	void		*data;

	assert(sbuf != NULL);
	assert(format != NULL);

	f = format;
	p = new_percent_esc();

	if (p == NULL) {
		sbuf_clear(sbuf);
		return (sbuf);	/* Out of memory */
	}

	while ( *f != '\0' ) {
		switch(*f) {
		case '%':
			fend = parse_format(f, PP_PKG, p);

			if (p->fmt_code <= PP_LAST_FORMAT)
				data = va_arg(ap, void *);
			else
				data = NULL;
			f = process_format_main(sbuf, p, f, fend, data);
			break;
		case '\\':
			f = process_escape(sbuf, f);
			break;
		default:
			sbuf_putc(sbuf, *f);
			f++;
			break;
		}
		if (f == NULL) {
			sbuf_clear(sbuf);
			break;	/* Error: out of memory */
		}
	}

	free_percent_esc(p);
	return (sbuf);
}
/*
 * That's All Folks!
 */
