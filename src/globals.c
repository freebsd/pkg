/* Copyright (c) 2014, Vsevolod Stakhov
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *       * Redistributions of source code must retain the above copyright
 *         notice, this list of conditions and the following disclaimer.
 *       * Redistributions in binary form must reproduce the above copyright
 *         notice, this list of conditions and the following disclaimer in the
 *         documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED ''AS IS'' AND ANY
 * EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED. IN NO EVENT SHALL AUTHOR BE LIABLE FOR ANY
 * DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
 * ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
 * SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include <pkg.h>

int default_yes; /* Default always yes */
int yes; /* Assume always yes */
int dry_run; /* Do not perform any actions */
bool auto_update; /* Do not update repo */
int case_sensitive; /* Case sensitive queries */
int force; /* Forced operation */
int quiet; /* Silent output */
int newpkgversion; /* New package version is available */

void
set_globals(void)
{
	default_yes = pkg_object_bool(pkg_config_get("DEFAULT_ALWAYS_YES"));
	yes = pkg_object_bool(pkg_config_get("ASSUME_ALWAYS_YES"));
	dry_run = 0;
	auto_update = pkg_object_bool(pkg_config_get("REPO_AUTOUPDATE"));
	case_sensitive = pkg_object_bool(pkg_config_get("CASE_SENSITIVE_MATCH"));
	force = 0;
	quiet = 0;
	newpkgversion = 0;
}

