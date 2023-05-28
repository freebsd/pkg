/*-
 * Copyright (c) 2019 Baptiste Daroussin <bapt@FreeBSD.org>
 * Copyright (c) 2014 Vsevolod Stakhov <vsevolod@FreeBSD.org>
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

#include <ucl.h>

#include "pkg.h"
#include "private/event.h"
#include "private/pkg.h"

/* Default to repo v1 for now */
#define	DEFAULT_META_VERSION	2

static ucl_object_t *repo_meta_schema_v1 = NULL;
static ucl_object_t *repo_meta_schema_v2 = NULL;

static void
pkg_repo_meta_set_default(struct pkg_repo_meta *meta)
{
	meta->digest_format = PKG_HASH_TYPE_SHA256_BASE32;
	meta->packing_format = DEFAULT_COMPRESSION;

	/* Not use conflicts for now */
	meta->conflicts = NULL;
	meta->conflicts_archive = NULL;
	meta->manifests = xstrdup("packagesite.yaml");
	meta->manifests_archive = xstrdup("packagesite");
	meta->filesite = xstrdup("filesite.yaml");
	meta->filesite_archive = xstrdup("filesite");
	/* Not using fulldb */
	meta->fulldb = NULL;
	meta->fulldb_archive = NULL;

	/*
	 * digest is only used on legacy v1 repository
	 * but pkg_repo_meta_is_special_file depend on the 
	 * information in the pkg_repo_meta.
	 * Leave digests here so pkg will not complain that
	 * repodir/digest.txz isn't a valid package when switching
	 * from version 1 to version 2
	 */
	meta->digests = xstrdup("digests");
	meta->digests_archive = xstrdup("digests");
}

void
pkg_repo_meta_free(struct pkg_repo_meta *meta)
{
	struct pkg_repo_meta_key *k;
	pkghash_it it;

	/*
	 * It is safe to free NULL pointer by standard
	 */
	if (meta != NULL) {
		free(meta->conflicts);
		free(meta->manifests);
		free(meta->digests);
		free(meta->fulldb);
		free(meta->filesite);
		free(meta->conflicts_archive);
		free(meta->manifests_archive);
		free(meta->digests_archive);
		free(meta->fulldb_archive);
		free(meta->filesite_archive);
		free(meta->maintainer);
		free(meta->source);
		free(meta->source_identifier);
		it = pkghash_iterator(meta->keys);
		while (pkghash_next(&it)) {
			k = (struct pkg_repo_meta_key *)it.value;
			free(k->name);
			free(k->pubkey);
			free(k->pubkey_type);
			free(k);
		}
		pkghash_destroy(meta->keys);
		free(meta);
	}
}

static ucl_object_t*
pkg_repo_meta_open_schema_v1()
{
	struct ucl_parser *parser;
	static const char meta_schema_str_v1[] = ""
			"{"
			"type = object;"
			"properties {"
			"version = {type = integer};\n"
			"maintainer = {type = string};\n"
			"source = {type = string};\n"
			"packing_format = {enum = [tzst, txz, tbz, tgz, tar]};\n"
			"digest_format = {enum = [sha256_base32, sha256_hex, blake2_base32, blake2s_base32]};\n"
			"digests = {type = string};\n"
			"manifests = {type = string};\n"
			"conflicts = {type = string};\n"
			"fulldb = {type = string};\n"
			"filesite = {type = string};\n"
			"digests_archive = {type = string};\n"
			"manifests_archive = {type = string};\n"
			"conflicts_archive = {type = string};\n"
			"fulldb_archive = {type = string};\n"
			"filesite_archive = {type = string};\n"
			"source_identifier = {type = string};\n"
			"revision = {type = integer};\n"
			"eol = {type = integer};\n"
			"cert = {"
			"  type = object;\n"
			"  properties {"
			"    type = {enum = [rsa]};\n"
			"    data = {type = string};\n"
			"    name = {type = string};\n"
			"  }"
			"  required = [type, data, name];\n"
			"};\n"

			"}\n"
			"required = [version]\n"
			"}";

	if (repo_meta_schema_v1 != NULL)
		return (repo_meta_schema_v1);

	parser = ucl_parser_new(UCL_PARSER_NO_FILEVARS);
	if (!ucl_parser_add_chunk(parser, meta_schema_str_v1,
			sizeof(meta_schema_str_v1) - 1)) {
		pkg_emit_error("cannot parse schema for repo meta: %s",
				ucl_parser_get_error(parser));
		ucl_parser_free(parser);
		return (NULL);
	}

	repo_meta_schema_v1 = ucl_parser_get_object(parser);
	ucl_parser_free(parser);

	return (repo_meta_schema_v1);
}

static ucl_object_t*
pkg_repo_meta_open_schema_v2()
{
	struct ucl_parser *parser;
	static const char meta_schema_str_v2[] = ""
			"{"
			"type = object;"
			"properties {"
			"version = {type = integer};\n"
			"maintainer = {type = string};\n"
			"source = {type = string};\n"
			"packing_format = {enum = [tzst, txz, tbz, tgz, tar]};\n"
			"manifests = {type = string};\n"
			"conflicts = {type = string};\n"
			"fulldb = {type = string};\n"
			"filesite = {type = string};\n"
			"manifests_archive = {type = string};\n"
			"conflicts_archive = {type = string};\n"
			"fulldb_archive = {type = string};\n"
			"filesite_archive = {type = string};\n"
			"source_identifier = {type = string};\n"
			"revision = {type = integer};\n"
			"eol = {type = integer};\n"
			"cert = {"
			"  type = object;\n"
			"  properties {"
			"    type = {enum = [rsa]};\n"
			"    data = {type = string};\n"
			"    name = {type = string};\n"
			"  }"
			"  required = [type, data, name];\n"
			"};\n"

			"}\n"
			"required = [version]\n"
			"}";

	if (repo_meta_schema_v2 != NULL)
		return (repo_meta_schema_v2);

	parser = ucl_parser_new(UCL_PARSER_NO_FILEVARS);
	if (!ucl_parser_add_chunk(parser, meta_schema_str_v2,
			sizeof(meta_schema_str_v2) - 1)) {
		pkg_emit_error("cannot parse schema for repo meta: %s",
				ucl_parser_get_error(parser));
		ucl_parser_free(parser);
		return (NULL);
	}

	repo_meta_schema_v2 = ucl_parser_get_object(parser);
	ucl_parser_free(parser);

	return (repo_meta_schema_v2);
}

static struct pkg_repo_meta_key*
pkg_repo_meta_parse_cert(const ucl_object_t *obj)
{
	struct pkg_repo_meta_key *key;

	key = xcalloc(1, sizeof(*key));

	/*
	 * It is already validated so just use it as is
	 */
	key->name = xstrdup(ucl_object_tostring(ucl_object_find_key(obj, "name")));
	key->pubkey = xstrdup(ucl_object_tostring(ucl_object_find_key(obj, "data")));
	key->pubkey_type = xstrdup(ucl_object_tostring(ucl_object_find_key(obj, "type")));

	return (key);
}

#define META_EXTRACT_STRING(field) do { 						\
	obj = ucl_object_find_key(top, (#field)); 					\
	if (obj != NULL && obj->type == UCL_STRING) { 				\
	    free(meta->field);									\
	    meta->field = xstrdup(ucl_object_tostring(obj));			\
	}															\
} while (0)

static int
pkg_repo_meta_parse(ucl_object_t *top, struct pkg_repo_meta **target, int version)
{
	const ucl_object_t *obj, *cur;
	ucl_object_iter_t iter = NULL;
	struct pkg_repo_meta *meta;
	struct pkg_repo_meta_key *cert;

	meta = xcalloc(1, sizeof(*meta));

	pkg_repo_meta_set_default(meta);
	meta->version = version;

	META_EXTRACT_STRING(maintainer);
	META_EXTRACT_STRING(source);

	META_EXTRACT_STRING(conflicts);
	META_EXTRACT_STRING(digests);
	META_EXTRACT_STRING(manifests);
	META_EXTRACT_STRING(fulldb);
	META_EXTRACT_STRING(filesite);
	META_EXTRACT_STRING(conflicts_archive);
	META_EXTRACT_STRING(digests_archive);
	META_EXTRACT_STRING(manifests_archive);
	META_EXTRACT_STRING(fulldb_archive);
	META_EXTRACT_STRING(filesite_archive);

	META_EXTRACT_STRING(source_identifier);

	obj = ucl_object_find_key(top, "eol");
	if (obj != NULL && obj->type == UCL_INT) {
		meta->eol = ucl_object_toint(obj);
	}

	obj = ucl_object_find_key(top, "revision");
	if (obj != NULL && obj->type == UCL_INT) {
		meta->revision = ucl_object_toint(obj);
	}

	obj = ucl_object_find_key(top, "packing_format");
	if (obj != NULL && obj->type == UCL_STRING) {
		meta->packing_format = packing_format_from_string(ucl_object_tostring(obj));
	}

	obj = ucl_object_find_key(top, "digest_format");
	if (obj != NULL && obj->type == UCL_STRING) {
		meta->digest_format = pkg_checksum_type_from_string(ucl_object_tostring(obj));
	}

	obj = ucl_object_find_key(top, "cert");
	while ((cur = ucl_iterate_object(obj, &iter, false)) != NULL) {
		cert = pkg_repo_meta_parse_cert(cur);
		if (cert != NULL)
			pkghash_safe_add(meta->keys, cert->name, cert, NULL);
	}

	*target = meta;

	return (EPKG_OK);
}

#undef META_EXTRACT_STRING

static int
pkg_repo_meta_version(ucl_object_t *top)
{
	const ucl_object_t *obj;

	if ((obj = ucl_object_find_key(top, "version")) != NULL) {
		if (obj->type == UCL_INT) {
			return (ucl_object_toint(obj));
		}
	}

	return (-1);
}

int
pkg_repo_meta_dump_fd(struct pkg_repo_meta *meta, const int fd)
{
	FILE *f;

	f = fdopen(dup(fd), "w+");
	if (f == NULL) {
		pkg_emit_error("Cannot dump file");
		return (EPKG_FATAL);
	}
	ucl_object_emit_file(pkg_repo_meta_to_ucl(meta), UCL_EMIT_JSON_COMPACT, f);
	fclose(f);
	return (EPKG_OK);
}

int
pkg_repo_meta_load(const int fd, struct pkg_repo_meta **target)
{
	struct ucl_parser *parser;
	ucl_object_t *top, *schema;
	struct ucl_schema_error err;
	int version;

	parser = ucl_parser_new(UCL_PARSER_KEY_LOWERCASE);

	if (!ucl_parser_add_fd(parser, fd)) {
		pkg_emit_error("cannot parse repository meta: %s",
				ucl_parser_get_error(parser));
		ucl_parser_free(parser);
		return (EPKG_FATAL);
	}

	top = ucl_parser_get_object(parser);
	ucl_parser_free(parser);

	version = pkg_repo_meta_version(top);
	if (version == -1) {
		pkg_emit_error("repository meta has wrong version or wrong format");
		ucl_object_unref(top);
		return (EPKG_FATAL);
	}

	/* Now we support only v1 and v2 meta */
	if (version == 1) {
		schema = pkg_repo_meta_open_schema_v1();
		printf("WARNING: Meta v1 support will be removed in the next version\n");
	}
	else if (version == 2)
		schema = pkg_repo_meta_open_schema_v2();
	else {
		pkg_emit_error("repository meta has wrong version %d", version);
		ucl_object_unref(top);
		return (EPKG_FATAL);
	}
	if (schema != NULL) {
		if (!ucl_object_validate(schema, top, &err)) {
			printf("repository meta cannot be validated: %s\n", err.msg);
			ucl_object_unref(top);
			return (EPKG_FATAL);
		}
	}

	return (pkg_repo_meta_parse(top, target, version));
}

struct pkg_repo_meta *
pkg_repo_meta_default(void)
{
	struct pkg_repo_meta *meta;

	meta = xcalloc(1, sizeof(*meta));
	meta->version = DEFAULT_META_VERSION;
	pkg_repo_meta_set_default(meta);

	return (meta);
}

#define META_EXPORT_FIELD(result, meta, field, type)	do { 					\
	if (meta->field != 0)					\
		ucl_object_insert_key((result), ucl_object_from ## type (meta->field),	\
				#field, 0, false); 												\
	} while(0)

#define META_EXPORT_FIELD_FUNC(result, meta, field, type, func)	do {			\
	if (func(meta->field) != 0)				\
		ucl_object_insert_key((result), ucl_object_from ## type (func(meta->field)), \
				#field, 0, false); 												\
	} while(0)


ucl_object_t *
pkg_repo_meta_to_ucl(struct pkg_repo_meta *meta)
{
	ucl_object_t *result = ucl_object_typed_new(UCL_OBJECT);

	META_EXPORT_FIELD(result, meta, version, int);
	META_EXPORT_FIELD(result, meta, maintainer, string);
	META_EXPORT_FIELD(result, meta, source, string);

	META_EXPORT_FIELD_FUNC(result, meta, packing_format, string,
		packing_format_to_string);

	if (meta->version == 1) {
		META_EXPORT_FIELD_FUNC(result, meta, digest_format, string,
		    pkg_checksum_type_to_string);
		META_EXPORT_FIELD(result, meta, digests, string);
		META_EXPORT_FIELD(result, meta, digests_archive, string);
	}
	META_EXPORT_FIELD(result, meta, manifests, string);
	META_EXPORT_FIELD(result, meta, conflicts, string);
	META_EXPORT_FIELD(result, meta, fulldb, string);
	META_EXPORT_FIELD(result, meta, filesite, string);
	META_EXPORT_FIELD(result, meta, manifests_archive, string);
	META_EXPORT_FIELD(result, meta, conflicts_archive, string);
	META_EXPORT_FIELD(result, meta, fulldb_archive, string);
	META_EXPORT_FIELD(result, meta, filesite_archive, string);

	META_EXPORT_FIELD(result, meta, source_identifier, string);
	META_EXPORT_FIELD(result, meta, revision, int);
	META_EXPORT_FIELD(result, meta, eol, int);

	/* TODO: export keys */

	return (result);
}

#undef META_EXPORT_FIELD
#undef META_EXPORT_FIELD_FUNC

#define META_SPECIAL_FILE(file, meta, field) \
	special || (meta->field == NULL ? false : (strcmp(file, meta->field) == 0))

bool
pkg_repo_meta_is_special_file(const char *file, struct pkg_repo_meta *meta)
{
	bool special = false;

	special = META_SPECIAL_FILE(file, meta, digests_archive);
	special = META_SPECIAL_FILE(file, meta, manifests_archive);
	special = META_SPECIAL_FILE(file, meta, filesite_archive);
	special = META_SPECIAL_FILE(file, meta, conflicts_archive);
	special = META_SPECIAL_FILE(file, meta, fulldb_archive);

	return (special);
}

bool
pkg_repo_meta_is_old_file(const char *file, struct pkg_repo_meta *meta)
{
	bool special = false;

	if (meta->version != 1)
		special = META_SPECIAL_FILE(file, meta, digests_archive);

	return (special);
}
