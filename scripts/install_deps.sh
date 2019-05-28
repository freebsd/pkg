#! /bin/sh
# Copyright (c) 2012 Bryan Drewery <bryan@shatow.net>
# All rights reserved.
#
# Redistribution and use in source and binary forms, with or without
# modification, are permitted provided that the following conditions
# are met:
# 1. Redistributions of source code must retain the above copyright
#    notice, this list of conditions and the following disclaimer
#    in this position and unchanged.
# 2. Redistributions in binary form must reproduce the above copyright
#    notice, this list of conditions and the following disclaimer in the
#    documentation and/or other materials provided with the distribution.
#
# THIS SOFTWARE IS PROVIDED BY THE AUTHOR(S) ``AS IS'' AND ANY EXPRESS OR
# IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
# OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
# IN NO EVENT SHALL THE AUTHOR(S) BE LIABLE FOR ANY DIRECT, INDIRECT,
# INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
# NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
# DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
# THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
# (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
# THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

# Install build dependencies for pkg on Mac or Linux (Debiaun/Ubuntu) systems.
#
# This script is primarily intended for travis CI to be able to setup the
# build environment.

install_from_github() {
	local name="${1}"
	local ver="${2}"
	local distname="${name}-${ver}"

	# https://github.com/jmmv/kyua/releases/download/kyua-0.12/kyua-0.12.tar.gz
	local url="https://github.com/jmmv/${name}"
	wget "${url}/releases/download/${distname}/${distname}.tar.gz"
	tar -xzvf "${distname}.tar.gz"

	cd "${distname}"
	./configure \
		--disable-developer \
		--without-atf \
		--without-doxygen \
		CPPFLAGS="-I/usr/local/include" \
		LDFLAGS="-L/usr/local/lib -Wl,-R/usr/local/lib" \
		PKG_CONFIG_PATH="/usr/local/lib/pkgconfig"
	make
	if [ `id -u` -eq 0 ]; then
		make install
	else
		sudo make install
	fi
	cd -

	rm -rf "${distname}" "${distname}.tar.gz"
}

if [ $(uname -s) = "Darwin" ]; then
	brew update
	brew upgrade openssl
	brew install libarchive
	brew install kyua
elif [ $(uname -s) = "Linux" ]; then
	install_from_github atf 0.21
	install_from_github lutok 0.4
	install_from_github kyua 0.12
	if [ `id -u` -eq 0 ]; then
		ldconfig
	else
		sudo ldconfig
	fi
fi
