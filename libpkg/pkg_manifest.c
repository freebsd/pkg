#include <sys/types.h>
#include <sys/sbuf.h>

#include <assert.h>
#include <ctype.h>
#include <err.h>
#include <errno.h>
#include <inttypes.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>

#include <yaml.h>
#include "pkg.h"
#include "pkg_error.h"
#include "pkg_util.h"
#include "pkg_private.h"

#define PKG_UNKNOWN -1
#define PKG_DEPS -2
#define PKG_CONFLICTS -3
#define PKG_FILES -4
#define PKG_DIRS -5
#define PKG_FLATSIZE -6

static struct manifest_key {
	const char *key;
	int type;
} manifest_key[] = {
	{ "name", PKG_NAME },
	{ "origin", PKG_ORIGIN },
	{ "version", PKG_VERSION },
	{ "arch", PKG_ARCH },
	{ "osversion", PKG_OSVERSION },
	{ "www", PKG_WWW },
	{ "comment", PKG_COMMENT},
	{ "maintainer", PKG_MAINTAINER},
	{ "prefix", PKG_PREFIX},
	{ "deps", PKG_DEPS},
	{ "conflicts", PKG_CONFLICTS},
	{ "files", PKG_FILES},
	{ "dirs", PKG_DIRS},
	{ "flatsize", PKG_FLATSIZE},
};

#define manifest_key_len (int)(sizeof(manifest_key)/sizeof(manifest_key[0]))

static int manifest_type(const char *key) {
	int i = 0;
	
	for (i = 0; i < manifest_key_len; i++) {
		if (!strcasecmp(key, manifest_key[i].key))
			return (manifest_key[i].type);
	}

	return (PKG_UNKNOWN);
}

int
pkg_load_manifest_file(struct pkg *pkg, const char *fpath)
{
	char *manifest = NULL;
	off_t sz;
	int ret = EPKG_OK;

	if ((ret = file_to_buffer(fpath, &manifest, &sz)) != EPKG_OK)
		return (ret);

	ret = pkg_parse_manifest(pkg, manifest);
	if (ret != EPKG_OK && manifest != NULL)
			free(manifest);

	return (ret);
}

int
pkg_parse_manifest(struct pkg *pkg, char *buf)
{
	yaml_parser_t parser;
	yaml_event_t event;
	int done = 0;
	int level = 0;
	bool inseq = false;
	bool expectvalue = false;
	char depname[BUFSIZ];
	char key[BUFSIZ];
	char origin[BUFSIZ];
	char version[BUFSIZ];
	int nbdepkeys = 0;
	char file[MAXPATHLEN];
	int64_t size;
	int type;

	yaml_parser_initialize(&parser);
	yaml_parser_set_input_string(&parser, buf, strlen(buf));

	while (!done) {
		if (!yaml_parser_parse(&parser, &event))
			goto error;

		done = (event.type == YAML_STREAM_END_EVENT);

		switch (event.type) {
			case YAML_STREAM_START_EVENT:
			case YAML_DOCUMENT_START_EVENT:
			case YAML_STREAM_END_EVENT:
			case YAML_DOCUMENT_END_EVENT:
				continue;
				break; /* NOT REACHED */
			case YAML_MAPPING_START_EVENT:
				level++;
				break;
			case YAML_MAPPING_END_EVENT:
				level--;
				break;
			case YAML_SEQUENCE_START_EVENT:
				inseq = true;
				break;
			case YAML_SEQUENCE_END_EVENT:
				inseq = false;
				break;
			case YAML_SCALAR_EVENT:
				if (level == 1 && !inseq && !expectvalue) {
					if ((type = manifest_type(event.data.scalar.value)) == -1) {
						warnx("Unknown key: %s", event.data.scalar.value);
						break;
					}
					if (type >= 0 || type == PKG_FLATSIZE)
						expectvalue = true;
					break;
				} else if (level == 1 && !inseq) {
					if (type == PKG_FLATSIZE) {
						size = strtoimax(buf, NULL, 10);
						pkg_setflatsize(pkg, size);
					} else {
						pkg_set(pkg, type, event.data.scalar.value);
					}
					expectvalue = false;
					break;
				} else if (level == 1 && inseq && type == PKG_CONFLICTS) {
					pkg_addconflict(pkg, event.data.scalar.value);
					break;
				} else if (level == 2 && !inseq && type == PKG_DEPS) {
					strlcpy(depname, event.data.scalar.value, BUFSIZ);
					nbdepkeys = 0;
					break;
				} else if (level == 3 && !inseq && type == PKG_DEPS && !expectvalue) {
					strlcpy(key, event.data.scalar.value, BUFSIZ);
					expectvalue = true;
					break;
				} else if (level == 3 && !inseq && type == PKG_DEPS && expectvalue) {
					if (!strcasecmp(key, "origin"))
						strlcpy(origin, event.data.scalar.value, BUFSIZ);
					else if (!strcasecmp(key, "version"))
						strlcpy(version, event.data.scalar.value, BUFSIZ);
					expectvalue = false;
					nbdepkeys++;
					if (nbdepkeys == 2)
						pkg_adddep(pkg, depname, origin, version);
					break;
				} else if (level == 2 && type == PKG_FILES && !expectvalue) {
					strlcpy(file, event.data.scalar.value, MAXPATHLEN);
					expectvalue = true;
					break;
				} else if (level == 2 && type == PKG_FILES && expectvalue) {
					pkg_addfile(pkg, file, event.data.scalar.length == 65 ? event.data.scalar.value : NULL);
					expectvalue = false;
					break;
				} else if (level == 1 && type == PKG_DIRS) {
					pkg_adddir(pkg, event.data.scalar.value);
					break;
				}

				printf("%d, %d %d: %s\n", level, type, inseq, event.data.scalar.value);
				break;
		}

		yaml_event_delete(&event);
	}
	yaml_parser_delete(&parser);

	return EPKG_OK;
error:
	yaml_parser_delete(&parser);
	return EPKG_FATAL;


}

static int
yaml_write_buf(void *data, unsigned char *buffer, size_t size)
{
	char **dest = (char **)data;
	*dest = malloc(size);
	strlcpy(*dest, buffer, size);
	return (1);
}

int
pkg_emit_manifest(struct pkg *pkg, char **dest)
{
	yaml_emitter_t emitter;
	yaml_event_t event;
	char tmpbuf[BUFSIZ];
	bool substarted = false;
	struct pkg_dep *dep = NULL;
	struct pkg_conflict *conflict = NULL;
	struct pkg_option *option = NULL;
	struct pkg_file *file = NULL;
	struct pkg_dir *dir = NULL;
	int rc = EPKG_OK;

	yaml_emitter_initialize(&emitter);
	yaml_emitter_set_output(&emitter, yaml_write_buf, dest);
	yaml_stream_start_event_initialize(&event, YAML_UTF8_ENCODING);
	if (!yaml_emitter_emit(&emitter, &event))
		goto error;

	yaml_document_start_event_initialize(&event, NULL, NULL, NULL, 1);
	if (!yaml_emitter_emit(&emitter, &event))
		goto error;


#define manifest_append_key(em, ev, key) \
	do { \
		yaml_scalar_event_initialize(ev, NULL, NULL, __DECONST(yaml_char_t*,key), strlen(key), true, true, YAML_PLAIN_SCALAR_STYLE); \
		yaml_emitter_emit(em, ev); \
	} while (0)

#define manifest_append_kv(em, ev, key, value) \
	do { \
		manifest_append_key(em, ev, key); \
		yaml_scalar_event_initialize( ev, NULL, NULL, __DECONST(yaml_char_t*,value), strlen(value), true, true, YAML_PLAIN_SCALAR_STYLE); \
		yaml_emitter_emit(em, ev); \
	} while (0)

#define manifest_append_value(em, ev, value) manifest_append_key(em, ev, value)
#define manifest_start_mapping(em, ev) \
	do { \
		yaml_mapping_start_event_initialize(ev, NULL, NULL, 1, YAML_ANY_MAPPING_STYLE); \
		yaml_emitter_emit(em, ev); \
	} while (0)

#define manifest_start_sequence(em, ev) \
	do { \
		yaml_sequence_start_event_initialize(ev, NULL, NULL, 1, YAML_ANY_MAPPING_STYLE); \
		yaml_emitter_emit(em, ev); \
	} while (0)

#define manifest_end_mapping(em, ev) \
	do { \
		yaml_mapping_end_event_initialize(ev); \
		yaml_emitter_emit(em, ev); \
	} while (0)

#define manifest_end_sequence(em, ev) \
	do { \
		yaml_sequence_end_event_initialize(ev); \
		yaml_emitter_emit(em, ev); \
	} while (0)

	manifest_start_mapping(&emitter, &event);

	manifest_append_kv(&emitter, &event, "name", pkg_get(pkg, PKG_NAME));
	manifest_append_kv(&emitter, &event, "version", pkg_get(pkg, PKG_VERSION));
	manifest_append_kv(&emitter, &event, "origin", pkg_get(pkg, PKG_ORIGIN));
	manifest_append_kv(&emitter, &event, "comment", pkg_get(pkg, PKG_COMMENT));
	manifest_append_kv(&emitter, &event, "arch", pkg_get(pkg, PKG_ARCH));
	manifest_append_kv(&emitter, &event, "osversion", pkg_get(pkg, PKG_OSVERSION));
	manifest_append_kv(&emitter, &event, "www", pkg_get(pkg, PKG_WWW));
	manifest_append_kv(&emitter, &event, "maintainer", pkg_get(pkg, PKG_MAINTAINER));
	manifest_append_kv(&emitter, &event, "prefix", pkg_get(pkg, PKG_PREFIX));
	snprintf(tmpbuf, BUFSIZ, "%ld", pkg_flatsize(pkg));
	manifest_append_kv(&emitter, &event, "flatsize", tmpbuf);

	while (pkg_deps(pkg, &dep) == EPKG_OK) {
		if (substarted == false) {
			manifest_append_key(&emitter, &event, "deps");

			manifest_start_mapping(&emitter, &event);
			substarted = true;

		}
		manifest_append_value(&emitter, &event, pkg_dep_name(dep));

		manifest_start_mapping(&emitter, &event);

		manifest_append_kv(&emitter, &event, "origin", pkg_dep_origin(dep));
		manifest_append_kv(&emitter, &event, "version", pkg_dep_version(dep));

		manifest_end_mapping(&emitter, &event);
	}

	if (substarted == true) {
		manifest_end_mapping(&emitter, &event);
		substarted = false;
	}

	while (pkg_conflicts(pkg, &conflict) == EPKG_OK) {
		if (substarted == false) {
			manifest_append_key(&emitter, &event, "conflicts");

			manifest_start_sequence(&emitter, &event);
			substarted = true;
		}
		manifest_append_value(&emitter, &event, pkg_conflict_glob(conflict));
	}

	if (substarted == true) {
		manifest_end_sequence(&emitter, &event);
		substarted=false;
	}

/*	while (pkg_options(pkg, &option) == EPKG_OK) {
		sbuf_printf(manifest, "option: %s %s\n", pkg_option_opt(option),
					pkg_option_value(option));
	}*/


	while (pkg_files(pkg, &file) == EPKG_OK) {
		if (substarted == false) {
			manifest_append_key(&emitter, &event, "files");

			manifest_start_mapping(&emitter, &event);
			substarted=true;
		}
		manifest_append_kv(&emitter, &event, pkg_file_path(file), pkg_file_sha256(file) && strlen(pkg_file_sha256(file)) > 0 ? pkg_file_sha256(file) : "-");
	}

	if (substarted == true) {
		manifest_end_mapping(&emitter, &event);
		substarted = false;
	}

	while (pkg_dirs(pkg, &dir) == EPKG_OK) {
		if (substarted == false) {
			manifest_append_key(&emitter, &event, "dirs");

			manifest_start_sequence(&emitter, &event);
			substarted = true;
		}
		manifest_append_value(&emitter, &event, pkg_dir_path(dir));
	}

	if (substarted == true) {
		manifest_end_sequence(&emitter, &event);
		substarted = false;
	}

	manifest_end_mapping(&emitter, &event);

	yaml_document_end_event_initialize(&event, 1);
	if (!yaml_emitter_emit(&emitter, &event))
		goto error;

	yaml_stream_end_event_initialize(&event);
	if (!yaml_emitter_emit(&emitter, &event))
		goto error;

	rc = EPKG_OK;
done:
	yaml_emitter_delete(&emitter);
	return (rc);

error:
	rc = EPKG_FATAL;
	goto done;

}
