/*-
 * Copyright (c) 1998 John D. Polstra
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 * $FreeBSD: stable/8/sbin/ldconfig/ldconfig.h 92882 2002-03-21 13:14:21Z imp $
 */

#ifndef LDCONFIG_H
#define LDCONFIG_H 1

#ifdef HAVE_CONFIG_H
#include "pkg_config.h"
#endif

#include <sys/cdefs.h>

#ifdef HAVE_ELF_HINTS_H
#include <elf-hints.h>
#else
/*
 * Hints file produced by ldconfig.
 */
struct elfhints_hdr
{
	u_int32_t magic; /* Magic number */
	u_int32_t version; /* File version (1) */
	u_int32_t strtab; /* Offset of string table in file */
	u_int32_t strsize; /* Size of string table */
	u_int32_t dirlist; /* Offset of directory list in
	 string table */
	u_int32_t dirlistlen; /* strlen(dirlist) */
	u_int32_t spare[26]; /* Room for expansion */
};

#define ELFHINTS_MAGIC  0x746e6845

# ifdef __NetBSD__
#define _PATH_ELF_HINTS "/var/run/ld.so.hints"
# else
#define _PATH_ELF_HINTS "/var/run/ld-elf.so.hints"
# endif
#endif

extern int	insecure;	/* -i flag, needed here for elfhints.c */

__BEGIN_DECLS
void		shlib_list_init(void);
void		rpath_list_init(void);
const char     *shlib_list_find_by_name(const char *);
void		shlib_list_free(void);
void		rpath_list_free(void);
int		shlib_list_from_elf_hints(const char *);
int		shlib_list_from_rpath(const char *, const char *);
void		shlib_list_from_stage(const char *);

void		list_elf_hints(const char *);
void		update_elf_hints(const char *, int, char **, int);
__END_DECLS

#endif
