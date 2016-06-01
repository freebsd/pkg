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

#include "binary.h"

struct pkg_repo_ops pkg_repo_binary_ops = {
	.type = "binary",
	.init = pkg_repo_binary_init,
	.access = pkg_repo_binary_access,
	.open = pkg_repo_binary_open,
	.create = pkg_repo_binary_create,
	.close = pkg_repo_binary_close,
	.update = pkg_repo_binary_update,
	.query = pkg_repo_binary_query,
	.shlib_provided = pkg_repo_binary_shlib_provide,
	.shlib_required = pkg_repo_binary_shlib_require,
	.provided = pkg_repo_binary_provide,
	.required = pkg_repo_binary_require,
	.search = pkg_repo_binary_search,
	.fetch_pkg = pkg_repo_binary_fetch,
	.mirror_pkg = pkg_repo_binary_mirror,
	.get_cached_name = pkg_repo_binary_get_cached_name,
	.ensure_loaded = pkg_repo_binary_ensure_loaded,
	.stat = pkg_repo_binary_stat
};
