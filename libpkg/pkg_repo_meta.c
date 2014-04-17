/*-
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

static ucl_object_t *repo_meta_schema_v1 = NULL;

static void
pkg_repo_meta_set_default(struct pkg_repo_meta *meta)
{
	meta->digest_format = strdup("sha256");
	meta->packing_format = TXZ;

	/* Not use conflicts for now */
	meta->conflicts = NULL;
	meta->manifests = strdup("packagesite.yaml");
	meta->digests = strdup("digests");
	/* Not using fulldb */
	meta->fulldb = NULL;
}

void
pkg_repo_meta_free(struct pkg_repo_meta *meta)
{
	struct pkg_repo_meta_key *k, *ktmp;

	/*
	 * It is safe to free NULL pointer by standard
	 */
	if (meta != NULL) {
		free(meta->conflicts);
		free(meta->manifests);
		free(meta->digests);
		free(meta->fulldb);
		free(meta->digest_format);
		free(meta->maintainer);
		free(meta->source);
		free(meta->source_identifier);
		HASH_ITER(hh, meta->keys, k, ktmp) {
			HASH_DELETE(hh, meta->keys, k);
			free(k->name);
			free(k->pubkey);
			free(k->pubkey_type);
			free(k);
		}
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
			"packing_format = {enum = [txz, tbz, tgz]};\n"
			"digest_format = {enum = [sha256]};\n"
			"digests = {type = string};\n"
			"manifests = {type = string};\n"
			"conflicts = {type = string};\n"
			"fulldb = {type = string};\n"
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

	parser = ucl_parser_new(0);
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

static struct pkg_repo_meta_key*
pkg_repo_meta_parse_cert(const ucl_object_t *obj)
{
	struct pkg_repo_meta_key *key;

	key = calloc(1, sizeof(*key));
	if (key == NULL) {
		pkg_emit_errno("pkg_repo_meta_parse", "malloc failed for pkg_repo_meta_key");
		return (NULL);
	}

	/*
	 * It is already validated so just use it as is
	 */
	key->name = strdup(ucl_object_tostring(ucl_object_find_key(obj, "name")));
	key->pubkey = strdup(ucl_object_tostring(ucl_object_find_key(obj, "data")));
	key->pubkey_type = strdup(ucl_object_tostring(ucl_object_find_key(obj, "type")));

	return (key);
}

#define META_EXTRACT_STRING(field) do { 						\
	obj = ucl_object_find_key(top, (#field)); 					\
	if (obj != NULL && obj->type == UCL_STRING) { 				\
	    if (meta->field != NULL) 								\
	    	free(meta->field);									\
	    meta->field = strdup(ucl_object_tostring(obj));			\
	}															\
} while (0)

static int
pkg_repo_meta_parse(ucl_object_t *top, struct pkg_repo_meta **target, int version)
{
	const ucl_object_t *obj, *cur;
	ucl_object_iter_t iter = NULL;
	struct pkg_repo_meta *meta;
	struct pkg_repo_meta_key *cert;

	meta = calloc(1, sizeof(*meta));
	if (meta == NULL) {
		pkg_emit_errno("pkg_repo_meta_parse", "malloc failed for pkg_repo_meta");
		return (EPKG_FATAL);
	}

	pkg_repo_meta_set_default(meta);

	META_EXTRACT_STRING(maintainer);
	META_EXTRACT_STRING(source);
	META_EXTRACT_STRING(digest_format);

	META_EXTRACT_STRING(conflicts);
	META_EXTRACT_STRING(digests);
	META_EXTRACT_STRING(manifests);
	META_EXTRACT_STRING(fulldb);

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

	obj = ucl_object_find_key(top, "cert");
	while ((cur = ucl_iterate_object(obj, &iter, false)) != NULL) {
		cert = pkg_repo_meta_parse_cert(cur);
		if (cert != NULL)
			HASH_ADD_STR(meta->keys, name, cert);
	}

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
pkg_repo_meta_load(const char *file, struct pkg_repo_meta **target)
{
	struct ucl_parser *parser;
	ucl_object_t *top, *schema;
	struct ucl_schema_error err;
	int version;

	parser = ucl_parser_new(UCL_PARSER_KEY_LOWERCASE);

	if (!ucl_parser_add_file(parser, file)) {
		pkg_emit_error("cannot parse repository meta from %s: %s", file,
				ucl_parser_get_error(parser));
		ucl_parser_free(parser);
		return (EPKG_FATAL);
	}

	top = ucl_parser_get_object(parser);
	ucl_parser_free(parser);

	version = pkg_repo_meta_version(top);
	if (version == -1) {
		pkg_emit_error("repository meta %s has wrong version or wrong format", file);
		ucl_object_unref(top);
		return (EPKG_FATAL);
	}

	/* Now we support only v1 meta */
	if (version == 1) {
		schema = pkg_repo_meta_open_schema_v1();

		if (schema != NULL) {
			if (!ucl_object_validate(schema, top, &err)) {
				pkg_emit_error("repository meta %s cannot be validated: %s", file, err.msg);
				ucl_object_unref(top);
				return (EPKG_FATAL);
			}
		}
	}
	else {
		pkg_emit_error("repository meta %s has wrong version %d", file, version);
		ucl_object_unref(top);
		return (EPKG_FATAL);
	}

	return (pkg_repo_meta_parse(top, target, version));
}

struct pkg_repo_meta *
pkg_repo_meta_default(void)
{
	struct pkg_repo_meta *meta;

	meta = calloc(1, sizeof(*meta));
	if (meta == NULL) {
		pkg_emit_errno("pkg_repo_meta_default", "malloc failed for pkg_repo_meta");
		return (NULL);
	}

	pkg_repo_meta_set_default(meta);

	return (meta);
}
