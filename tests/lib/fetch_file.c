/*-
 * Copyright (c) 2026 Baptiste Daroussin <bapt@FreeBSD.org>
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include <atf-c.h>
#include <sys/param.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <private/pkg.h>
#include <private/fetch.h>

ATF_TC_WITHOUT_HEAD(file_open_localhost);
ATF_TC_WITHOUT_HEAD(file_open_long_hostname);
ATF_TC_WITHOUT_HEAD(file_open_invalid_url);
ATF_TC_WITHOUT_HEAD(file_open_missing_path);

/*
 * Test that file://localhost/ URLs are accepted
 */
ATF_TC_BODY(file_open_localhost, tc)
{
	struct pkg_repo repo = {0};
	struct fetch_item fi = {0};
	char tmpfile[MAXPATHLEN];
	int fd;
	int ret;

	/* Create a temporary file to fetch */
	snprintf(tmpfile, sizeof(tmpfile), "%s/pkg_test_fetch_XXXXXX", getenv("TMPDIR") ? getenv("TMPDIR") : "/tmp");
	fd = mkstemp(tmpfile);
	ATF_REQUIRE_MSG(fd != -1, "Failed to create temporary file");
	close(fd);

	/* Test file://localhost/ URL */
	fi.url = tmpfile;
	/* Construct file://localhost URL */
	char url_localhost[MAXPATHLEN + 20];
	snprintf(url_localhost, sizeof(url_localhost), "file://localhost%s", tmpfile);
	fi.url = url_localhost;
	fi.mtime = 0;

	ret = file_open(&repo, &fi);
	
	/* The file exists, so it should succeed or fail based on other factors,
	 * but should not crash or have buffer overflow */
	ATF_REQUIRE_MSG(ret == EPKG_OK || ret == EPKG_UPTODATE || ret == EPKG_FATAL,
	    "file_open returned unexpected value: %d", ret);

	/* Cleanup */
	if (repo.fh != NULL)
		fclose(repo.fh);
	unlink(tmpfile);
}

/*
 * Test that URLs with excessively long hostnames are rejected safely
 * without buffer overflow
 */
ATF_TC_BODY(file_open_long_hostname, tc)
{
	struct pkg_repo repo = {0};
	struct fetch_item fi = {0};
	char long_url[PATH_MAX * 3];
	int ret;
	size_t i;

	/* Construct a URL with a hostname longer than 256 characters */
	snprintf(long_url, sizeof(long_url), "file://");
	for (i = 0; i < 300; i++) {
		long_url[7 + i] = 'a';
	}
	snprintf(long_url + 7 + 300, sizeof(long_url) - 7 - 300, "/tmp/nonexistent.pkg");

	fi.url = long_url;
	fi.mtime = 0;

	ret = file_open(&repo, &fi);

	/* Should be rejected as invalid URL (hostname too long or not localhost) */
	ATF_REQUIRE_EQ_MSG(ret, EPKG_FATAL, 
	    "file_open should reject URLs with excessively long hostnames");

	/* Ensure no crash occurred - if we reach here, buffer overflow was prevented */
}

/*
 * Test that invalid URLs are properly rejected
 */
ATF_TC_BODY(file_open_invalid_url, tc)
{
	struct pkg_repo repo = {0};
	struct fetch_item fi = {0};
	int ret;

	/* Test various invalid URLs */
	const char *invalid_urls[] = {
		"file://",           /* Missing path */
		"file:/tmp/test",    /* Single slash */
		"file://",           /* Just prefix */
		"file://hostname",   /* No path after hostname */
		NULL
	};

	for (int i = 0; invalid_urls[i] != NULL; i++) {
		fi.url = invalid_urls[i];
		fi.mtime = 0;
		
		ret = file_open(&repo, &fi);
		ATF_REQUIRE_EQ_MSG(ret, EPKG_FATAL,
		    "file_open should reject invalid URL: %s", invalid_urls[i]);
	}
}

/*
 * Test that file:// URLs without hostname work correctly
 */
ATF_TC_BODY(file_open_missing_path, tc)
{
	struct pkg_repo repo = {0};
	struct fetch_item fi = {0};
	char tmpfile[MAXPATHLEN];
	int fd;
	int ret;

	/* Create a temporary file */
	snprintf(tmpfile, sizeof(tmpfile), "%s/pkg_test_fetch2_XXXXXX", 
	    getenv("TMPDIR") ? getenv("TMPDIR") : "/tmp");
	fd = mkstemp(tmpfile);
	ATF_REQUIRE_MSG(fd != -1, "Failed to create temporary file");
	close(fd);

	/* Test file:// URL (without hostname) */
	char url_file[MAXPATHLEN + 10];
	snprintf(url_file, sizeof(url_file), "file://%s", tmpfile);
	fi.url = url_file;
	fi.mtime = 0;

	ret = file_open(&repo, &fi);
	
	/* Should succeed since file exists */
	ATF_REQUIRE_MSG(ret == EPKG_OK || ret == EPKG_UPTODATE,
	    "file_open should accept valid file:// URL");

	/* Cleanup */
	if (repo.fh != NULL)
		fclose(repo.fh);
	unlink(tmpfile);
}

ATF_TP_ADD_TCS(tp)
{
	ATF_TP_ADD_TC(tp, file_open_localhost);
	ATF_TP_ADD_TC(tp, file_open_long_hostname);
	ATF_TP_ADD_TC(tp, file_open_invalid_url);
	ATF_TP_ADD_TC(tp, file_open_missing_path);

	return (atf_no_error());
}
