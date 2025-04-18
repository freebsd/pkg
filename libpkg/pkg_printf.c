/*
 * Copyright (c) 2012-2015 Matthew Seaman <matthew@FreeBSD.org>
 * Copyright (c) 2014-2020 Baptiste Daroussin <bapt@FreeBSD.org>
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
#include <sys/stat.h>

/* musl libc apparently does not have ALLPERMS */
#ifndef ALLPERMS
#define     ALLPERMS        (S_ISUID|S_ISGID|S_ISTXT|S_IRWXU|S_IRWXG|S_IRWXO)
#endif

#include <assert.h>
#include <ctype.h>
#include <inttypes.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <utlist.h>

#include "pkg.h"
#include <xstring.h>
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
 * P pkg
 * Q
 *
 * R  pkg          Repopath
 * S  char*        Arbitrary character string
 *
 * T
 *
 * U  pkg          List of users
 * Un pkg_user     User name
 *
 * V  pkg          old version
 * W
 * X  pkg          Internal Checksum
 * Y  pkg          List of requires
 * Yn pkg_provide  Name of the require
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
 * x  pkg          pkg tarball size
 * y  pkg          List of provides
 * yn pkg_provide  name of the provide
 *
 * z  pkg          short checksum
 */
static xstring *pkg_xstring_vprintf(xstring * restrict buf, const char * restrict format, va_list ap);

struct pkg_printf_fmt {
	char	         fmt_main;
	char		 fmt_sub;
	bool		 has_trailer;
	bool		 struct_pkg; /* or else a sub-type? */
	unsigned	 context;
	xstring	*(*fmt_handler)(xstring *, const void *,
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
	[PP_PKG_ALTABI] =
	{
		'Q',
		'\0',
		false,
		true,
		PP_ALL,
		&format_altabi,
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
	[PP_PKG_REQUIRED_NAME] = {
		'Y',
		'n',
		false,
		false,
		PP_PKG|PP_Y,
		&format_provide_name,
	},
	[PP_PKG_REQUIRED] = {
		'Y',
		'\0',
		true,
		true,
		PP_PKG,
		&format_required,
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
	[PP_PKG_PKGSIZE] =
	{
		'x',
		'\0',
		false,
		true,
		PP_ALL,
		&format_pkgsize,
	},
	[PP_PKG_PROVIDED_NAME] = {
		'y',
		'n',
		false,
		false,
		PP_PKG|PP_y,
		&format_provide_name,
	},
	[PP_PKG_PROVIDED] = {
		'y',
		'\0',
		true,
		true,
		PP_PKG,
		&format_provided,
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
	[PP_PKG_INT_CHECKSUM] =
	{
		'X',
		'\0',
		false,
		true,
		PP_ALL,
		&format_int_checksum,
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
xstring *
format_annotations(xstring *buf, const void *data, struct percent_esc *p)
{
	const struct pkg	*pkg = data;
	int			count;

	if (p->flags & (PP_ALTERNATE_FORM1|PP_ALTERNATE_FORM2)) {
		return (list_count(buf, vec_len(&pkg->annotations), p));
	} else {
		set_list_defaults(p, "%An: %Av\n", "");

		count = 1;
		fflush(p->sep_fmt->fp);
		fflush(p->item_fmt->fp);
		vec_foreach(pkg->annotations, i) {
			if (count > 1)
				iterate_item(buf, pkg, p->sep_fmt->buf,
					     pkg->annotations.d[i], count, PP_A);

			iterate_item(buf, pkg, p->item_fmt->buf,
				     pkg->annotations.d[i], count, PP_A);
			count++;
		}
	}
	return (buf);
}

/*
 * %An -- Annotation tag name.
 */
xstring *
format_annotation_name(xstring *buf, const void *data, struct percent_esc *p)
{
	const struct pkg_kv	*kv = data;

	return (string_val(buf, kv->key, p));
}

/*
 * %Av -- Annotation value.
 */
xstring *
format_annotation_value(xstring *buf, const void *data, struct percent_esc *p)
{
	const struct pkg_kv	*kv = data;

	return (string_val(buf, kv->value, p));
}

/*
 * %B -- Required Shared Libraries.  List of shlibs required by
 * binaries in the pkg.  Optionally accepts per-field format in %{ %|
 * %}.  Default %{%Bn\n%|%}
 */
xstring *
format_shlibs_required(xstring *buf, const void *data, struct percent_esc *p)
{
	const struct pkg	*pkg = data;

	if (p->flags & (PP_ALTERNATE_FORM1|PP_ALTERNATE_FORM2))
		return (list_count(buf, vec_len(&pkg->shlibs_required), p));
	else {
		int			 count;

		set_list_defaults(p, "%Bn\n", "");

		count = 1;
		fflush(p->sep_fmt->fp);
		fflush(p->item_fmt->fp);
		vec_foreach(pkg->shlibs_required, i) {
			if (count > 1)
				iterate_item(buf, pkg, p->sep_fmt->buf,
					     pkg->shlibs_required.d[i], count, PP_B);

			iterate_item(buf, pkg, p->item_fmt->buf,
				     pkg->shlibs_required.d[i], count, PP_B);
			count++;
		}
	}
	return (buf);
}

/*
 * %Bn -- Required Shared Library name or %bn -- Provided Shared
 * Library name
 */
xstring *
format_shlib_name(xstring *buf, const void *data, struct percent_esc *p)
{
	const char	*shlib = data;

	return (string_val(buf, shlib, p));
}

/*
 * %C -- Categories.  List of Category names (strings). 1ary category
 * is not distinguished -- look at the package origin for that.
 * Optionally accepts per-field format in %{ %| %}, where %n is
 * replaced by the category name.  Default %{%Cn%|, %}
 */
xstring *
format_categories(xstring *buf, const void *data, struct percent_esc *p)
{
	const struct pkg	*pkg = data;
	int			 count = 0;

	if (p->flags & (PP_ALTERNATE_FORM1|PP_ALTERNATE_FORM2)) {
		return (list_count(buf, vec_len(&pkg->categories), p));
	} else {
		set_list_defaults(p, "%Cn", ", ");

		count = 1;
		fflush(p->sep_fmt->fp);
		fflush(p->item_fmt->fp);
		vec_foreach(pkg->categories, i) {
			if (count > 1)
				iterate_item(buf, pkg, p->sep_fmt->buf,
				    pkg->categories.d[i], count, PP_C);

			iterate_item(buf, pkg, p->item_fmt->buf, pkg->categories.d[i],
			    count, PP_C);
			count++;
		}
	}
	return (buf);
}

/*
 * %Cn -- Category name.
 */
xstring *
format_category_name(xstring *buf, const void *data, struct percent_esc *p)
{
	const char *cat = data;

	return (string_val(buf, cat, p));
}

/*
 * %D -- Directories.  List of directory names (strings) possibly with
 * other meta-data.  Optionally accepts following per-field format in
 * %{ %| %}, where %Dn is replaced by the directory name.  Default
 * %{%Dn\n%|%}
 */
xstring *
format_directories(xstring *buf, const void *data, struct percent_esc *p)
{
	const struct pkg	*pkg = data;

	if (p->flags & (PP_ALTERNATE_FORM1|PP_ALTERNATE_FORM2))
		return (list_count(buf, pkg_list_count(pkg, PKG_DIRS), p));
	else {
		struct pkg_dir	*dir = NULL;
		int		 count;

		set_list_defaults(p, "%Dn\n", "");

		count = 1;
		fflush(p->sep_fmt->fp);
		fflush(p->item_fmt->fp);
		while (pkg_dirs(pkg, &dir) == EPKG_OK) {
			if (count > 1)
				iterate_item(buf, pkg, p->sep_fmt->buf,
					     dir, count, PP_D);

			iterate_item(buf, pkg, p->item_fmt->buf,
				     dir, count, PP_D);
			count++;
		}
	}
	return (buf);
}

/*
 * %Dg -- Directory group. TODO: numeric gid
 */
xstring *
format_directory_group(xstring *buf, const void *data,
		       struct percent_esc *p)
{
	const struct pkg_dir	*dir = data;

	return (string_val(buf, dir->gname, p));
}

/*
 * %Dn -- Directory path name.
 */
xstring *
format_directory_path(xstring *buf, const void *data, struct percent_esc *p)
{
	const struct pkg_dir	*dir = data;

	return (string_val(buf, dir->path, p));
}

/*
 * %Dp -- Directory permissions.
 */
xstring *
format_directory_perms(xstring *buf, const void *data,
		       struct percent_esc *p)
{
	const struct pkg_dir	*dir = data;

	return (mode_val(buf, dir->perm, p));
}

/*
 * %Du -- Directory user. TODO: numeric UID
 */
xstring *
format_directory_user(xstring *buf, const void *data,
		      struct percent_esc *p)
{
	const struct pkg_dir	*dir = data;

	return (string_val(buf, dir->uname, p));
}

/*
 * %F -- Files.  List of filenames (strings) possibly with other
 * meta-data.  Optionally accepts following per-field format in %{ %|
 * %}, where %n is replaced by the filename, %s by the checksum, etc.
 * Default %{%Fn\n%|%}
 */
xstring *
format_files(xstring *buf, const void *data, struct percent_esc *p)
{
	const struct pkg	*pkg = data;

	if (p->flags & (PP_ALTERNATE_FORM1|PP_ALTERNATE_FORM2))
		return (list_count(buf, pkg_list_count(pkg, PKG_FILES), p));
	else {
		struct pkg_file	*file = NULL;
		int		 count;

		set_list_defaults(p, "%Fn\n", "");

		count = 1;
		fflush(p->sep_fmt->fp);
		fflush(p->item_fmt->fp);
		LL_FOREACH(pkg->files, file) {
			if (count > 1)
				iterate_item(buf, pkg, p->sep_fmt->buf,
					     file, count, PP_F);

			iterate_item(buf, pkg, p->item_fmt->buf,
				     file, count, PP_F);
			count++;
		}
	}
	return (buf);
}

/*
 * %Fg -- File group.
 */
xstring *
format_file_group(xstring *buf, const void *data, struct percent_esc *p)
{
	const struct pkg_file	*file = data;

	return (string_val(buf, file->gname, p));
}

/*
 * %Fn -- File path name.
 */
xstring *
format_file_path(xstring *buf, const void *data, struct percent_esc *p)
{
	const struct pkg_file	*file = data;

	return (string_val(buf, file->path, p));
}

/*
 * %Fp -- File permissions.
 */
xstring *
format_file_perms(xstring *buf, const void *data, struct percent_esc *p)
{
	const struct pkg_file	*file = data;

	return (mode_val(buf, file->perm, p));
}

/*
 * %Fs -- File SHA256 Checksum.
 */
xstring *
format_file_sha256(xstring *buf, const void *data, struct percent_esc *p)
{
	const struct pkg_file	*file = data;

	return (string_val(buf, file->sum, p));
}

/*
 * %Fu -- File user.
 */
xstring *
format_file_user(xstring *buf, const void *data, struct percent_esc *p)
{
	const struct pkg_file	*file = data;

	return (string_val(buf, file->uname, p));
}

/*
 * %G -- Groups. list of string values.  Optionally accepts following
 * per-field format in %{ %| %} where %Gn will be replaced by each
 * groupname or %#Gn by the gid -- a line from
 * /etc/group. Default %{%Gn\n%|%}
 */
xstring *
format_groups(xstring *buf, const void *data, struct percent_esc *p)
{
	const struct pkg	*pkg = data;

	if (p->flags & (PP_ALTERNATE_FORM1|PP_ALTERNATE_FORM2))
		return (list_count(buf, vec_len(&pkg->groups), p));
	else {
		int	 count;

		set_list_defaults(p, "%Gn\n", "");

		count = 1;
		fflush(p->sep_fmt->fp);
		fflush(p->item_fmt->fp);
		vec_foreach(pkg->groups, i) {
			if (count > 1)
				iterate_item(buf, pkg, p->sep_fmt->buf,
					     pkg->groups.d[i], count, PP_G);

			iterate_item(buf, pkg,p->item_fmt->buf,
				     pkg->groups.d[i], count, PP_G);
			count++;
		}
	}
	return (buf);
}

/*
 * %Gn -- Group name.
 */
xstring *
format_group_name(xstring *buf, const void *data, struct percent_esc *p)
{
	const char	*group = data;

	return (string_val(buf, group, p));
}

/*
 * %I -- Row counter (integer*). Usually used only in per-field format.
 */
xstring *
format_row_counter(xstring *buf, const void *data, struct percent_esc *p)
{
	const int *counter = data;

	return (int_val(buf, *counter, p));
}

/*
 * %L -- Licences. List of string values.  Optionally accepts
 * following per-field format in %{ %| %} where %Ln is replaced by the
 * license name and %l by the license logic.  Default %{%n%| %l %}
 */
xstring *
format_licenses(xstring *buf, const void *data, struct percent_esc *p)
{
	const struct pkg	*pkg = data;
	int			 count = 0;

	if (p->flags & (PP_ALTERNATE_FORM1|PP_ALTERNATE_FORM2)) {
		return (list_count(buf, vec_len(&pkg->licenses), p));
	} else {
		set_list_defaults(p, "%Ln", " %l ");

		count = 1;
		fflush(p->sep_fmt->fp);
		fflush(p->item_fmt->fp);
		vec_foreach(pkg->licenses, i) {
			if (count > 1)
				iterate_item(buf, pkg, p->sep_fmt->buf,
				    pkg->licenses.d[i], count, PP_L);

			iterate_item(buf, pkg, p->item_fmt->buf, pkg->licenses.d[i],
			    count, PP_L);
			count++;
		}
	}
	return (buf);
}

/*
 * %Ln -- License name.
 */
xstring *
format_license_name(xstring *buf, const void *data, struct percent_esc *p)
{
	const char *lic = data;

	return (string_val(buf, lic, p));
}

/*
 * %M -- Pkg message. string.  Accepts field-width, left-align
 */
xstring *
format_message(xstring *buffer, const void *data, struct percent_esc *p)
{
	xstring		*buf, *bufmsg = NULL;
	const struct pkg	*pkg = data;
	struct pkg_message	*msg;
	char			*message;

	vec_foreach(pkg->message, i) {
		msg = pkg->message.d[i];
		if (bufmsg == NULL) {
			bufmsg = xstring_new();
		} else {
			fputc('\n', bufmsg->fp);
		}
		switch(msg->type) {
		case PKG_MESSAGE_ALWAYS:
			fprintf(bufmsg->fp, "Always:\n");
			break;
		case PKG_MESSAGE_UPGRADE:
			fprintf(bufmsg->fp, "On upgrade");
			if (msg->minimum_version != NULL ||
			    msg->maximum_version != NULL) {
				fprintf(bufmsg->fp, " from %s", pkg->name);
			}
			if (msg->minimum_version != NULL) {
				fprintf(bufmsg->fp, ">%s", msg->minimum_version);
			}
			if (msg->maximum_version != NULL) {
				fprintf(bufmsg->fp, "<%s", msg->maximum_version);
			}
			fprintf(bufmsg->fp, ":\n");
			break;
		case PKG_MESSAGE_INSTALL:
			fprintf(bufmsg->fp, "On install:\n");
			break;
		case PKG_MESSAGE_REMOVE:
			fprintf(bufmsg->fp, "On remove:\n");
			break;
		}
		fprintf(bufmsg->fp, "%s\n", msg->str);
	}
	if (bufmsg == NULL)
		message = NULL;
	else {
		fflush(bufmsg->fp);
		message = bufmsg->buf;
	}

	buf = string_val(buffer, message, p);
	xstring_free(bufmsg);

	return (buf);
}

/*
 * %N -- Repository identity. string.  Accepts field-width, left-align
 */
xstring *
format_repo_ident(xstring *buf, const void *data, struct percent_esc *p)
{
	const struct pkg	*pkg = data;
	const char		*reponame;

	reponame = pkg->reponame;
	if (reponame == NULL) {
		reponame = pkg_kv_get(&pkg->annotations, "repository");
		if (reponame == NULL)
			reponame = "unknown-repository";
	}
	return (string_val(buf, reponame, p));
}

/*
 * %O -- Options. list of {option,value} tuples. Optionally accepts
 * following per-field format in %{ %| %}, where %On is replaced by the
 * option name and %Ov by the value.  Default %{%On %Ov\n%|%}
 */
xstring *
format_options(xstring *buf, const void *data, struct percent_esc *p)
{
	const struct pkg	*pkg = data;

	if (p->flags & (PP_ALTERNATE_FORM1|PP_ALTERNATE_FORM2))
		return (list_count(buf, pkg_list_count(pkg, PKG_OPTIONS), p));
	else {
		struct pkg_option	*opt = NULL;
		int			 count;

		set_list_defaults(p, "%On %Ov\n", "");

		count = 1;
		fflush(p->sep_fmt->fp);
		fflush(p->item_fmt->fp);
		while (pkg_options(pkg, &opt) == EPKG_OK) {
			if (count > 1)
				iterate_item(buf, pkg, p->sep_fmt->buf,
					     opt, count, PP_O);

			iterate_item(buf, pkg, p->item_fmt->buf,
				     opt, count, PP_O);
			count++;
		}
	}
	return (buf);
}

/*
 * %On -- Option name.
 */
xstring *
format_option_name(xstring *buf, const void *data, struct percent_esc *p)
{
	const struct pkg_option	*option = data;

	return (string_val(buf, option->key, p));
}

/*
 * %Ov -- Option value.
 */
xstring *
format_option_value(xstring *buf, const void *data, struct percent_esc *p)
{
	const struct pkg_option	*option = data;

	return (string_val(buf, option->value, p));
}

/*
 * %Od -- Option default value.
 */
xstring *
format_option_default(xstring *buf, const void *data, struct percent_esc *p)
{
	const struct pkg_option	*option = data;

	return (string_val(buf, option->value, p));
}

/*
 * %OD -- Option description
 */
xstring *
format_option_description(xstring *buf, const void *data, struct percent_esc *p)
{
	const struct pkg_option	*option = data;

	return (string_val(buf, option->description, p));
}

/*
 * %Q -- pkg architecture a.k.a ABI string.  Accepts field-width, left-align
 */
xstring *
format_altabi(xstring *buf, const void *data, struct percent_esc *p)
{
	const struct pkg	*pkg = data;

	return (string_val(buf, pkg->altabi, p));
}

/*
 * %R -- Repo path. string.
 */
xstring *
format_repo_path(xstring *buf, const void *data, struct percent_esc *p)
{
	const struct pkg	*pkg = data;

	return (string_val(buf, pkg->repopath, p));
}

/*
 * %S -- Character string.
 */
xstring *
format_char_string(xstring *buf, const void *data, struct percent_esc *p)
{
	const char	*charstring = data;

	return (string_val(buf, charstring, p));
}

/*
 * %U -- Users. list of string values.  Optionally accepts following
 * per-field format in %{ %| %} where %Un will be replaced by each
 * username or %#Un by the uid -- a line from
 * /etc/passwd. Default %{%Un\n%|%}
 */
xstring *
format_users(xstring *buf, const void *data, struct percent_esc *p)
{
	const struct pkg	*pkg = data;

	if (p->flags & (PP_ALTERNATE_FORM1|PP_ALTERNATE_FORM2))
		return (list_count(buf, vec_len(&pkg->users), p));
	else {
		int	 count;

		set_list_defaults(p, "%Un\n", "");

		count = 1;
		fflush(p->sep_fmt->fp);
		fflush(p->item_fmt->fp);
		vec_foreach(pkg->users, i) {
			if (count > 1)
				iterate_item(buf, pkg, p->sep_fmt->buf,
					     pkg->users.d[i], count, PP_U);

			iterate_item(buf, pkg, p->item_fmt->buf,
				     pkg->users.d[i], count, PP_U);
			count++;
		}
	}
	return (buf);
}

/*
 * %Un -- User name.
 */
xstring *
format_user_name(xstring *buf, const void *data, struct percent_esc *p)
{
	const char	*user = data;

	return (string_val(buf, user, p));
}

/*
 * %V -- Old package version. string. Accepts field width, left align
 */
xstring *
format_old_version(xstring *buf, const void *data, struct percent_esc *p)
{
	const struct pkg	*pkg = data;

	return (string_val(buf, pkg->old_version, p));
}

/*
 * %X -- Package checksum. string. Accepts field width, left align
 */
xstring *
format_int_checksum(xstring *buf, const void *data, struct percent_esc *p)
{
	struct pkg	*pkg = (struct pkg *)data;

	pkg_checksum_calculate(pkg, NULL, true, false, true);
	return (string_val(buf, pkg->digest, p));
}

/*
 * %Y -- Required pattern.  List of pattern required by
 * binaries in the pkg.  Optionally accepts per-field format in %{ %|
 * %}.  Default %{%Yn\nr->item*/
xstring *
format_required(xstring *buf, const void *data, struct percent_esc *p)
{
	const struct pkg	*pkg = data;

	if (p->flags & (PP_ALTERNATE_FORM1|PP_ALTERNATE_FORM2))
		return (list_count(buf, vec_len(&pkg->requires), p));
	else {
		int	 count;

		set_list_defaults(p, "%Yn\n", "");

		count = 1;
		fflush(p->sep_fmt->fp);
		fflush(p->item_fmt->fp);
		vec_foreach(pkg->requires, i) {
			if (count > 1)
				iterate_item(buf, pkg, p->sep_fmt->buf,
					     pkg->requires.d[i], count, PP_Y);

			iterate_item(buf, pkg, p->item_fmt->buf,
				     pkg->requires.d[i], count, PP_Y);
			count++;
		}
	}
	return (buf);
}

/*
 * %Yn -- Required name or %yn -- Provided name
 */
xstring *
format_provide_name(xstring *buf, const void *data, struct percent_esc *p)
{
	const char	*provide = data;

	return (string_val(buf, provide, p));
}
/*
 * %a -- Autoremove flag. boolean.  Accepts field-width, left-align.
 * Standard form: 0, 1.  Alternate form1: no, yes.  Alternate form2:
 * false, true
 */
xstring *
format_autoremove(xstring *buf, const void *data, struct percent_esc *p)
{
	const struct pkg	*pkg = data;

	return (bool_val(buf, pkg->automatic, p));
}


/*
 * %b -- Provided Shared Libraries.  List of shlibs provided by
 * binaries in the pkg.  Optionally accepts per-field format in %{ %|
 * %}, where %n is replaced by the shlib name.  Default %{%bn\n%|%}
 */
xstring *
format_shlibs_provided(xstring *buf, const void *data, struct percent_esc *p)
{
	const struct pkg	*pkg = data;

	if (p->flags & (PP_ALTERNATE_FORM1|PP_ALTERNATE_FORM2))
		return (list_count(buf, vec_len(&pkg->shlibs_provided), p));
	else {
		int	 count;

		set_list_defaults(p, "%bn\n", "");

		count = 1;
		fflush(p->sep_fmt->fp);
		fflush(p->item_fmt->fp);
		vec_foreach(pkg->shlibs_provided, i) {
			if (count > 1)
				iterate_item(buf, pkg, p->sep_fmt->buf,
					     pkg->shlibs_provided.d[i], count, PP_b);

			iterate_item(buf, pkg, p->item_fmt->buf,
				     pkg->shlibs_provided.d[i], count, PP_b);
			count++;
		}
	}
	return (buf);
}

/*
 * %c -- Comment. string.  Accepts field-width, left-align
 */
xstring *
format_comment(xstring *buf, const void *data, struct percent_esc *p)
{
	const struct pkg	*pkg = data;

	return (string_val(buf, pkg->comment, p));
}

/*
 * %d -- Dependencies. List of pkgs. Can be optionally followed by
 * per-field format string in %{ %| %} using any pkg_printf() *scalar*
 * formats. Defaults to printing "%dn-%dv\n" for each dependency.
 */
xstring *
format_dependencies(xstring *buf, const void *data, struct percent_esc *p)
{
	const struct pkg	*pkg = data;

	if (p->flags & (PP_ALTERNATE_FORM1|PP_ALTERNATE_FORM2))
		return (list_count(buf, pkg_list_count(pkg, PKG_DEPS), p));
	else {
		struct pkg_dep	*dep = NULL;
		int		 count;

		set_list_defaults(p, "%dn-%dv\n", "");

		count = 1;
		fflush(p->sep_fmt->fp);
		fflush(p->item_fmt->fp);
		while (pkg_deps(pkg, &dep) == EPKG_OK) {
			if (count > 1)
				iterate_item(buf, pkg, p->sep_fmt->buf,
					     dep, count, PP_d);

			iterate_item(buf, pkg, p->item_fmt->buf,
				     dep, count, PP_d);
			count++;
		}
	}
	return (buf);
}

/*
 * %dk -- Dependency lock status or %rk -- Requirement lock status.
 */
xstring *
format_dependency_lock(xstring *buf, const void *data,
		       struct percent_esc *p)
{
	const struct pkg_dep	*dep = data;

	return (bool_val(buf, pkg_dep_is_locked(dep), p));
}

/*
 * %dn -- Dependency name or %rn -- Requirement name.
 */
xstring *
format_dependency_name(xstring *buf, const void *data,
		       struct percent_esc *p)
{
	const struct pkg_dep	*dep = data;

	return (string_val(buf, dep->name, p));
}

/*
 * %do -- Dependency origin or %ro -- Requirement origin.
 */
xstring *
format_dependency_origin(xstring *buf, const void *data,
			 struct percent_esc *p)
{
	const struct pkg_dep	*dep = data;

	return (string_val(buf, dep->origin, p));
}

/*
 * %dv -- Dependency version or %rv -- Requirement version.
 */
xstring *
format_dependency_version(xstring *buf, const void *data,
			  struct percent_esc *p)
{
	const struct pkg_dep	*dep = data;

	return (string_val(buf, dep->version, p));
}

/*
 * %e -- Description. string. Accepts field-width, left-align
 */
xstring *
format_description(xstring *buf, const void *data, struct percent_esc *p)
{
	const struct pkg	*pkg = data;

	return (string_val(buf, pkg->desc, p));
}

/*
 * %k -- Locked flag. boolean.  Accepts field-width, left-align.
 * Standard form: 0, 1.  Alternate form1: no, yes.  Alternate form2:
 * false, true
 */
xstring *
format_lock_status(xstring *buf, const void *data, struct percent_esc *p)
{
	const struct pkg	*pkg = data;

	return (bool_val(buf, pkg->locked, p));
}

/*
 * %l -- Licence logic. string.  Accepts field-width, left-align.
 * Standard form: and, or, single. Alternate form 1: &, |, ''.
 * Alternate form 2: &&, ||, ==
 */
xstring *
format_license_logic(xstring *buf, const void *data, struct percent_esc *p)
{
	const struct pkg	*pkg = data;

	return (liclog_val(buf, pkg->licenselogic, p));
}

/*
 * %m -- Maintainer e-mail address. string.  Accepts field-width, left-align
 */
xstring *
format_maintainer(xstring *buf, const void *data, struct percent_esc *p)
{
	const struct pkg	*pkg = data;

	return (string_val(buf, pkg->maintainer, p));
}

/*
 * %n -- Package name. string.  Accepts field-width, left-align
 */
xstring *
format_name(xstring *buf, const void *data, struct percent_esc *p)
{
	const struct pkg	*pkg = data;

	return (string_val(buf, pkg->name, p));
}

/*
 * %o -- Package origin. string.  Accepts field-width, left-align
 */
xstring *
format_origin(xstring *buf, const void *data, struct percent_esc *p)
{
	const struct pkg	*pkg = data;

	return (string_val(buf, pkg->origin, p));
}

/*
 * %p -- Installation prefix. string. Accepts field-width, left-align
 */
xstring *
format_prefix(xstring *buf, const void *data, struct percent_esc *p)
{
	const struct pkg	*pkg = data;

	return (string_val(buf, pkg->prefix, p));
}

/*
 * %q -- pkg architecture a.k.a ABI string.  Accepts field-width, left-align
 */
xstring *
format_architecture(xstring *buf, const void *data, struct percent_esc *p)
{
	const struct pkg	*pkg = data;

	return (string_val(buf, pkg->abi, p));
}

/*
 * %r -- Requirements. List of pkgs. Can be optionally followed by
 * per-field format string in %{ %| %} using any pkg_printf() *scalar*
 * formats. Defaults to printing "%{%rn-%rv\n%|%}" for each dependency.
 */
xstring *
format_requirements(xstring *buf, const void *data, struct percent_esc *p)
{
	const struct pkg	*pkg = data;

	if (p->flags & (PP_ALTERNATE_FORM1|PP_ALTERNATE_FORM2))
		return(list_count(buf, pkg_list_count(pkg, PKG_RDEPS), p));
	else {
		struct pkg_dep	*req = NULL;
		int		 count;

		set_list_defaults(p, "%rn-%rv\n", "");

		count = 1;
		fflush(p->sep_fmt->fp);
		fflush(p->item_fmt->fp);
		while (pkg_rdeps(pkg, &req) == EPKG_OK) {
			if (count > 1)
				iterate_item(buf, pkg, p->sep_fmt->buf,
					     req, count, PP_r);

			iterate_item(buf, pkg, p->item_fmt->buf,
				     req, count, PP_r);
			count++;
		}
	}
	return (buf);
}

/*
 * %s -- Size of installed package. integer.  Accepts field-width,
 * left-align, zero-fill, space-for-plus, explicit-plus and
 * alternate-form.  Alternate form is a humanized number using decimal
 * exponents (k, M, G).  Alternate form 2, ditto, but using binary
 * scale prefixes (ki, Mi, Gi etc.)
 */
xstring *
format_flatsize(xstring *buf, const void *data, struct percent_esc *p)
{
	const struct pkg	*pkg = data;

	return (int_val(buf, pkg->flatsize, p));
}

/*
 * %t -- Installation timestamp (Unix time). integer.  Accepts
 * field-width, left-align.  Can be followed by optional strftime
 * format string in %{ %}.  Default is to print seconds-since-epoch as
 * an integer applying our integer format modifiers.
 */
xstring *
format_install_tstamp(xstring *buf, const void *data, struct percent_esc *p)
{
	const struct pkg	*pkg = data;

	fflush(p->item_fmt->fp);
	if (strlen(p->item_fmt->buf) == 0)
		return (int_val(buf, pkg->timestamp, p));
	else {
		char	 buffer[1024];
		time_t	 tsv;

		tsv = (time_t)pkg->timestamp;
		strftime(buffer, sizeof(buffer), p->item_fmt->buf,
			 localtime(&tsv));
		fprintf(buf->fp, "%s", buffer);
	}
	return (buf);
}

/*
 * %v -- Package version. string. Accepts field width, left align
 */
xstring *
format_version(xstring *buf, const void *data, struct percent_esc *p)
{
	const struct pkg	*pkg = data;

	return (string_val(buf, pkg->version, p));
}

/*
 * %u -- Package checksum. string. Accepts field width, left align
 */
xstring *
format_checksum(xstring *buf, const void *data, struct percent_esc *p)
{
	const struct pkg	*pkg = data;

	return (string_val(buf, pkg->sum, p));
}

/*
 * %w -- Home page URL.  string.  Accepts field width, left align
 */
xstring *
format_home_url(xstring *buf, const void *data, struct percent_esc *p)
{
	const struct pkg	*pkg = data;

	return (string_val(buf, pkg->www, p));
}

/*
 * %x - Package tarball size. Integer. Accepts field-width,
 * left-align, zero-fill, space-for-plus, explicit-plus and
 * alternate-form.  Alternate form is a humanized number using decimal
 * exponents (k, M, G).  Alternate form 2, ditto, but using binary
 * scale prefixes (ki, Mi, Gi etc.)
 */
xstring *
format_pkgsize(xstring *buf, const void *data, struct percent_esc *p)
{
	const struct pkg	*pkg = data;

	return (int_val(buf, pkg->pkgsize, p));
}

/*
 * %y -- Provided pattern.  List of pattern provided by
 * binaries in the pkg.  Optionally accepts per-field format in %{ %|
 * %}.  Default %{%yn\n%|%}
 */
xstring *
format_provided(xstring *buf, const void *data, struct percent_esc *p)
{
	const struct pkg	*pkg = data;

	if (p->flags & (PP_ALTERNATE_FORM1|PP_ALTERNATE_FORM2))
		return (list_count(buf, vec_len(&pkg->provides), p));
	else {
		int	 count;

		set_list_defaults(p, "%yn\n", "");

		count = 1;
		fflush(p->sep_fmt->fp);
		fflush(p->item_fmt->fp);
		vec_foreach(pkg->provides, i) {
			if (count > 1)
				iterate_item(buf, pkg, p->sep_fmt->buf,
					     pkg->provides.d[i], count, PP_y);

			iterate_item(buf, pkg, p->item_fmt->buf,
				     pkg->provides.d[i], count, PP_y);
			count++;
		}
	}
	return (buf);
}

/*
 * %z -- Package short checksum. string. Accepts field width, left align
 */
xstring *
format_short_checksum(xstring *buf, const void *data, struct percent_esc *p)
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

	return (string_val(buf, csum, p));
}
/*
 * %% -- Output a literal '%' character
 */
xstring *
format_literal_percent(xstring *buf, __unused const void *data,
		       __unused struct percent_esc *p)
{
	fputc('%', buf->fp);
	return (buf);
}

/*
 * Unknown format code -- return NULL to signal upper layers to pass
 * the text through unchanged.
 */
xstring *
format_unknown(xstring *buf, __unused const void *data,
		       __unused struct percent_esc *p)
{
	fputc('%', buf->fp);
	return (NULL);
}

/* -------------------------------------------------------------- */

struct percent_esc *
new_percent_esc(void)
{
	struct percent_esc	*p;

	p = xcalloc(1, sizeof(struct percent_esc));
	p->item_fmt = xstring_new();
	p->sep_fmt = xstring_new();
	return (p);
}

struct percent_esc *
clear_percent_esc(struct percent_esc *p)
{
	p->flags = 0;
	p->width = 0;
	p->trailer_status = 0;
	xstring_reset(p->item_fmt);
	xstring_reset(p->sep_fmt);

	p->fmt_code = '\0';

	return (p);
}

void
free_percent_esc(struct percent_esc *p)
{
	if (p) {
		if (p->item_fmt)
			xstring_free(p->item_fmt);
		if (p->sep_fmt)
			xstring_free(p->sep_fmt);
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


xstring *
human_number(xstring *buf, int64_t number, struct percent_esc *p)
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

	if (scale == MAXSCALE)
		scale--;

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

	fprintf(buf->fp, format, width, precision, num * sign);

	if (scale > 0)
		fprintf(buf->fp, "%s",
		    bin_scale ? bin_pfx[scale] : si_pfx[scale]);

	return (buf);
}

xstring *
string_val(xstring *buf, const char *str, struct percent_esc *p)
{
	char	format[16];

	/* The '#' '?' '+' ' ' '0' and '\'' modifiers have no meaning
	   for strings */

	p->flags &= ~(PP_ALTERNATE_FORM1 |
		      PP_ALTERNATE_FORM2 |
		      PP_EXPLICIT_PLUS   |
		      PP_SPACE_FOR_PLUS  |
		      PP_ZERO_PAD        |
		      PP_THOUSANDS_SEP);

	if (gen_format(format, sizeof(format), p->flags, "s") == NULL)
		return (NULL);

	fprintf(buf->fp, format, p->width, str);
	return (buf);
}

xstring *
int_val(xstring *buf, int64_t value, struct percent_esc *p)
{
	if (p->flags & (PP_ALTERNATE_FORM1|PP_ALTERNATE_FORM2))
		return (human_number(buf, value, p));
	else {
		char	 format[16];

		if (gen_format(format, sizeof(format), p->flags, PRId64)
		    == NULL)
			return (NULL);

		fprintf(buf->fp, format, p->width, value);
	}
	return (buf);
}

xstring *
bool_val(xstring *buf, bool value, struct percent_esc *p)
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

	return (string_val(buf, boolean_str[value][alternate], p));
}

xstring *
mode_val(xstring *buf, mode_t mode, struct percent_esc *p)
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

		return (string_val(buf, modebuf, p));
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

		fprintf(buf->fp, format, p->width, mode);
	}
	return (buf);
}

xstring *
liclog_val(xstring *buf, lic_t licenselogic, struct percent_esc *p)
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

	return (string_val(buf, liclog_str[llogic][alternate], p));
}

xstring *
list_count(xstring *buf, int64_t count, struct percent_esc *p)
{
	/* Convert to 0 or 1 for %?X */
	if (p->flags & PP_ALTERNATE_FORM1)
		count = (count > 0);

	/* Turn off %#X and %?X flags, then print as a normal integer */
	p->flags &= ~(PP_ALTERNATE_FORM1|PP_ALTERNATE_FORM2);

	return (int_val(buf, count, p));
}

struct percent_esc *
set_list_defaults(struct percent_esc *p, const char *item_fmt,
		  const char *sep_fmt)
{
	if ((p->trailer_status & ITEM_FMT_SET) != ITEM_FMT_SET) {
		fprintf(p->item_fmt->fp, "%s", item_fmt);
		p->trailer_status |= ITEM_FMT_SET;
	}
	if ((p->trailer_status & SEP_FMT_SET) != SEP_FMT_SET) {
		fprintf(p->sep_fmt->fp, "%s", sep_fmt);
		p->trailer_status |= SEP_FMT_SET;
	}
	return (p);
}

xstring *
iterate_item(xstring *buf, const struct pkg *pkg, const char *format,
	     const void *data, int count, unsigned context)
{
	const char		*f;
	struct percent_esc	*p;

	/* Scan the format string and interpret any escapes */

	f = format;
	p = new_percent_esc();

	if (p == NULL) {
		xstring_reset(buf);
		return (buf);	/* Out of memory */
	}

	while ( *f != '\0' ) {
		switch(*f) {
		case '%':
			f = process_format_trailer(buf, p, f, pkg, data, count, context);
			break;
		case '\\':
			f = process_escape(buf, f);
			break;
		default:
			fprintf(buf->fp, "%c", *f);
			f++;
			break;
		}
		if (f == NULL) {
			xstring_reset(buf);
			break;	/* Out of memory */
		}
	}

	free_percent_esc(p);
	return (buf);
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
			fputc(*f2, p->item_fmt->fp);
			fflush(p->item_fmt->fp);
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
				fputc(*f2, p->sep_fmt->fp);
				fflush(p->sep_fmt->fp);
			}

		}

		if (done) {
			f = f1;
		} else {
			xstring_reset(p->item_fmt);
			xstring_reset(p->sep_fmt);
		}
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
maybe_read_hex_byte(xstring *buf, const char *f)
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

		fputc(val, buf->fp);
		f++;
	} else {
		/* Pass through unchanged if it's not a recognizable
		   hex byte. */
		fputc('\\', buf->fp);
		fputc('x', buf->fp);
	}
	return (f);
}

const char*
read_oct_byte(xstring *buf, const char *f)
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
	fputc(val, buf->fp);

	return (f);
}

const char *
process_escape(xstring *buf, const char *f)
{
	f++;			/* Eat the \ */

	switch (*f) {
	case 'a':
		fputc('\a', buf->fp);
		f++;
		break;
	case 'b':
		fputc('\b', buf->fp);
		f++;
		break;
	case 'f':
		fputc('\f', buf->fp);
		f++;
		break;
	case 'n':
		fputc('\n', buf->fp);
		f++;
		break;
	case 't':
		fputc('\t', buf->fp);
		f++;
		break;
	case 'v':
		fputc('\v', buf->fp);
		f++;
		break;
	case '\'':
		fputc('\'', buf->fp);
		f++;
		break;
	case '"':
		fputc('"', buf->fp);
		f++;
		break;
	case '\\':
		fputc('\\', buf->fp);
		f++;
		break;
	case 'x':		/* Hex escape: \xNN */
		f = maybe_read_hex_byte(buf, f);
		break;
	case '0':
	case '1':
	case '2':
	case '3':
	case '4':
	case '5':
	case '6':
	case '7':		/* Oct escape: all fall through */
		f = read_oct_byte(buf, f);
		break;
	default:		/* If it's not a recognised escape,
				   leave f pointing at the escaped
				   character */
		fputc('\\', buf->fp);
		break;
	}

	return (f);
}

const char *
process_format_trailer(xstring *buf, struct percent_esc *p,
		       const char *f, const struct pkg *pkg,
		       const void *data, int count, unsigned context)
{
	const char		*fstart;
	xstring		*s;

	fstart = f;
	f = parse_format(f, context, p);

	if (p->fmt_code == PP_ROW_COUNTER)
		s = fmt[p->fmt_code].fmt_handler(buf, &count, p);
	else if (p->fmt_code > PP_LAST_FORMAT)
		s = fmt[p->fmt_code].fmt_handler(buf, NULL, p);
	else if (fmt[p->fmt_code].struct_pkg)
		s = fmt[p->fmt_code].fmt_handler(buf, pkg, p);
	else
		s = fmt[p->fmt_code].fmt_handler(buf, data, p);


	if (s == NULL) {
		f = fstart + 1;	/* Eat just the % on error */
	}

	clear_percent_esc(p);

	return (f);
}

const char *
process_format_main(xstring *buf, struct percent_esc *p,
		const char *fstart, const char *fend, void *data)
{
	xstring		*s;

	s = fmt[p->fmt_code].fmt_handler(buf, data, p);

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
	xstring	*buf;
	int		 count;

	buf = xstring_new();

	if (buf)
		buf = pkg_xstring_vprintf(buf, format, ap);
	fflush(buf->fp);
	if (buf && strlen(buf->buf) > 0) {
		count = printf("%s", buf->buf);
	} else
		count = -1;
	if (buf)
		xstring_free(buf);
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
	xstring	*buf;
	int		 count;

	buf = xstring_new();

	if (buf)
		buf = pkg_xstring_vprintf(buf, format, ap);
	fflush(buf->fp);
	if (buf && strlen(buf->buf) > 0) {
		count = fprintf(stream, "%s", buf->buf);
	} else
		count = -1;
	if (buf)
		xstring_free(buf);
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
	xstring	*buf;
	int		 count;

	buf = xstring_new();

	if (buf)
		buf = pkg_xstring_vprintf(buf, format, ap);
	fflush(buf->fp);
	if (buf && strlen(buf->buf) > 0) {
		count = dprintf(fd, "%s", buf->buf);
	} else
		count = -1;
	if (buf)
		xstring_free(buf);
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
	xstring	*buf;
	int		 count;

	buf = xstring_new();

	if (buf)
		buf = pkg_xstring_vprintf(buf, format, ap);
	fflush(buf->fp);
	if (buf && strlen(buf->buf) > 0) {
		count = snprintf(str, size, "%s", buf->buf);
	} else
		count = -1;
	if (buf)
		xstring_free(buf);

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
	xstring	*buf;
	int		 count;

	buf = xstring_new();

	if (buf)
		buf = pkg_xstring_vprintf(buf, format, ap);
	fflush(buf->fp);
	if (buf && strlen(buf->buf) > 0) {
		count = xasprintf(ret, "%s", buf->buf);
	} else {
		count = -1;
		*ret = NULL;
	}
	if (buf)
		xstring_free(buf);
	return (count);
}

/**
 * store data from pkg into buf as indicated by the format code format.
 * This is the core function called by all the other pkg_printf() family.
 * @param buf contains the result
 * @param ap Arglist with struct pkg etc. supplying the data
 * @param format String with embedded %-escapes indicating what to output
 * @return count of the number of characters in the result
 */
static xstring *
pkg_xstring_vprintf(xstring * restrict buf, const char * restrict format,
  va_list ap)
{
	const char		*f, *fend;
	struct percent_esc	*p;
	void		*data;

	assert(buf != NULL);
	assert(format != NULL);

	f = format;
	p = new_percent_esc();

	if (p == NULL) {
		xstring_reset(buf);
		return (buf);	/* Out of memory */
	}

	while ( *f != '\0' ) {
		switch(*f) {
		case '%':
			fend = parse_format(f, PP_PKG, p);

			if (p->fmt_code <= PP_LAST_FORMAT)
				data = va_arg(ap, void *);
			else
				data = NULL;
			f = process_format_main(buf, p, f, fend, data);
			break;
		case '\\':
			f = process_escape(buf, f);
			break;
		default:
			fputc(*f, buf->fp);
			f++;
			break;
		}
		if (f == NULL) {
			xstring_reset(buf);
			break;	/* Error: out of memory */
		}
	}

	free_percent_esc(p);
	return (buf);
}
/*
 * That's All Folks!
 */
