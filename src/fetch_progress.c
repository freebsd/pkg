/*-
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * Copyright (c) 2026 Minsoo Choo
 */

#include <sys/ioctl.h>

#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#if __has_include(<libutil.h>)
#include <libutil.h>
#endif

#include "pkg.h"
#include "pkgcli.h"
#include "xmalloc.h"

struct fetch_progress_row {
	struct pkg *pkg;
	char *name;
	int64_t current, total, initial, sampled;
	double began, sample_time, changed, rate;
};

static vec_t(struct fetch_progress_row) rows = vec_init();
static struct fetch_progress_row serial_row;
static bool active, interactive;
static size_t packages, completed, rendered;
static double last_draw;

static double
fetch_progress_time(void)
{
	struct timespec now;

	if (clock_gettime(CLOCK_MONOTONIC, &now) == -1)
		return (time(NULL));
	return (now.tv_sec + now.tv_nsec / 1e9);
}

bool
fetch_progress_active(void)
{
	return (active);
}

/* The cursor rests below the active rows, including any wrapped lines. */
void
fetch_progress_clear(void)
{
	if (rendered == 0)
		return;
	printf("\033[%zuA\r\033[J", rendered);
	rendered = 0;
	fflush(stdout);
}

static struct winsize
fetch_progress_window(void)
{
	struct winsize window = { .ws_col = 80, .ws_row = 24 };

	if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &window) == -1 || window.ws_col == 0)
		window.ws_col = 80;
	if (window.ws_row == 0)
		window.ws_row = 24;
	return (window);
}

static size_t
fetch_progress_line(struct fetch_progress_row *row, const char *label,
    double now, bool finished, int columns, size_t available)
{
	sb_t suffix = sb_init(), line = sb_init();
	char size[10], rate[16], duration[32];
	static const char *units[] = { "B", "kB", "MB", "GB", "TB" };
	static const char suffix_format[] = " %3d%% %9s %10s %11s";
	double elapsed = now - row->sample_time;
	double speed;
	int64_t seconds;
	int percent, space;
	size_t height, unit = 0;

	if (finished) {
		row->rate = MAX(row->current - row->initial, 0) / MAX(now - row->began, 0.001);
	} else if (elapsed >= 0.5) {
		double speed = MAX(row->current - row->sampled, 0) / elapsed;
		row->rate = row->rate == 0 ? speed : row->rate * 0.7 + speed * 0.3;
		row->sampled = row->current;
		row->sample_time = now;
	}
	percent = finished ? 100 : row->total > 0 ?
	    MIN(99, (int)(100.0 * row->current / row->total)) : 0;
	humanize_number(size, sizeof(size), row->current, "B", HN_AUTOSCALE,
	    HN_IEC_PREFIXES);
	/* Use decimal transfer-rate units and binary size units. */
	speed = row->rate;
	while (speed >= 1000 && unit + 1 < NELEM(units)) {
		speed /= 1000;
		unit++;
	}
	snprintf(rate, sizeof(rate), "%5.1f %2s/s", speed, units[unit]);
	if (!finished && now - row->changed >= 5) {
		strlcpy(duration, "- stalled -", sizeof(duration));
	} else if (!finished && row->rate < 1) {
		strlcpy(duration, "--:-- ETA", sizeof(duration));
	} else {
		seconds = finished ? MAX((int64_t)(now - row->began), 1) :
		    (int64_t)((row->total - row->current) / row->rate);
		if (seconds >= 3600)
			snprintf(duration, sizeof(duration), "%02jd:%02jd:%02jd%s",
			    (intmax_t)(seconds / 3600), (intmax_t)(seconds / 60 % 60),
			    (intmax_t)(seconds % 60), finished ? "" : " ETA");
		else
			snprintf(duration, sizeof(duration), "%02jd:%02jd%s",
			    (intmax_t)(seconds / 60), (intmax_t)(seconds % 60),
			    finished ? "" : " ETA");
	}
	sb_printf(&suffix, suffix_format, percent, size, rate, duration);
	/* Preserve the other fields' minimum widths, even when a value grows. */
	space = columns - 1 - (int)strlen(label) - 1 -
	    snprintf(NULL, 0, suffix_format, 0, "", "", "");
	sb_printf(&line, "%s:%*s", label, MAX(space, 0), "");
	sb_cat(&line, sb_str(&suffix));
	height = columns > 0 ? MAX((line.len + columns - 1) / columns, 1) : 1;
	if (available == 0 || height <= available)
		printf("%s\n", sb_str(&line));
	else
		height = 0;
	sb_fini(&suffix);
	sb_fini(&line);
	return (height);
}

static size_t
fetch_progress_package_line(struct fetch_progress_row *row, size_t number,
    double now, bool finished, int columns, size_t available)
{
	sb_t label = sb_init();
	int digits = 1;
	size_t height;

	for (size_t n = packages; n >= 10; n /= 10)
		digits++;
	sb_printf(&label, "[%*zu/%zu] Fetching %s", digits, number, packages,
	    row->name);
	height = fetch_progress_line(row, sb_str(&label), now, finished,
	    columns, available);
	sb_fini(&label);
	return (height);
}

/* Serial transfers use the same row formatter and wrapped-line accounting. */
void
fetch_progress_serial_start(const char *label)
{
	double now = fetch_progress_time();

	free(serial_row.name);
	serial_row = (struct fetch_progress_row) {
		.name = xstrdup(label),
		.began = now, .sample_time = now, .changed = now,
	};
	rendered = fetch_progress_line(&serial_row, serial_row.name, now, false,
	    fetch_progress_window().ws_col, 0);
	last_draw = now;
	fflush(stdout);
}

void
fetch_progress_serial_tick(int64_t current, int64_t total, bool force)
{
	double now = fetch_progress_time();
	bool finished = current >= total;

	if (serial_row.current != current)
		serial_row.changed = now;
	serial_row.current = current;
	serial_row.total = total;
	if (!finished && !force && now - last_draw < 0.1)
		return;
	fetch_progress_clear();
	rendered = fetch_progress_line(&serial_row, serial_row.name, now,
	    finished, fetch_progress_window().ws_col, 0);
	last_draw = now;
	fflush(stdout);
}

void
fetch_progress_serial_stop(void)
{
	/* Keep the final frame visible, just as for a completed concurrent row. */
	rendered = 0;
	free(serial_row.name);
	serial_row.name = NULL;
}

void
fetch_progress_draw(bool force)
{
	struct winsize window;
	double now;
	size_t available, height;

	if (!active || quiet || !interactive)
		return;
	now = fetch_progress_time();
	if (!force && now - last_draw < 0.1)
		return;
	window = fetch_progress_window();
	available = MAX((int)window.ws_row - 1, 1);
	fetch_progress_clear();
	for (size_t i = 0; i < rows.len && rendered < available; i++) {
		height = fetch_progress_package_line(&rows.d[i], completed + i + 1, now,
		    false, window.ws_col, available - rendered);
		if (height == 0)
			break;
		rendered += height;
	}
	last_draw = now;
	fflush(stdout);
}

void
fetch_progress_event(struct pkg_event *ev)
{
	struct fetch_progress_row row, *r;
	const char *name, *version, *term;
	double now = fetch_progress_time();
	size_t i;
	bool added;

	if (ev->e_fetch_progress.status == PKG_FETCH_PROGRESS_END ||
	    ev->e_fetch_progress.status == PKG_FETCH_PROGRESS_START) {
		fetch_progress_clear();
		vec_foreach(rows, n)
			free(rows.d[n].name);
		vec_free(&rows);
		completed = 0;
		packages = ev->e_fetch_progress.packages;
		active = ev->e_fetch_progress.status == PKG_FETCH_PROGRESS_START;
		term = getenv("TERM");
		interactive = isatty(STDOUT_FILENO) &&
		    (term == NULL || strcmp(term, "dumb") != 0);
		last_draw = 0;
		return;
	}
	if (!active || quiet)
		return;
	for (i = 0; i < rows.len; i++) {
		if (rows.d[i].pkg == ev->e_fetch_progress.pkg)
			break;
	}
	added = i == rows.len;
	if (added) {
		pkg_get(ev->e_fetch_progress.pkg, PKG_ATTR_NAME, &name);
		pkg_get(ev->e_fetch_progress.pkg, PKG_ATTR_VERSION, &version);
		row = (struct fetch_progress_row) {
			.pkg = ev->e_fetch_progress.pkg,
			.initial = ev->e_fetch_progress.current,
			.sampled = ev->e_fetch_progress.current,
			.began = now, .sample_time = now, .changed = now,
		};
		xasprintf(&row.name, "%s-%s", name, version);
		/* Package labels must not introduce terminal control sequences. */
		for (char *p = row.name; *p != '\0'; p++) {
			if ((unsigned char)*p < 32 || *p == 127)
				*p = '?';
		}
		vec_push(&rows, row);
	}
	r = &rows.d[i];
	if (r->current != ev->e_fetch_progress.current)
		r->changed = now;
	r->current = MIN(MAX(ev->e_fetch_progress.current, 0), ev->e_fetch_progress.total);
	r->total = ev->e_fetch_progress.total;
	if (ev->e_fetch_progress.status == PKG_FETCH_PROGRESS_FINISHED) {
		fetch_progress_clear();
		/* Completion order determines the permanent line numbers. */
		fetch_progress_package_line(r, ++completed, now, true,
		    interactive ? fetch_progress_window().ws_col : 0, 0);
		free(r->name);
		vec_remove(&rows, i);
		fetch_progress_draw(true);
		fflush(stdout);
	} else
		fetch_progress_draw(added);
}
