#!/bin/sh -
# Copyright (c) 2012 Matthew Seaman <matthew@freebsd.org>
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

# This file contains the authoritative version number for the last
# release from this branch, and for the corresponding port..  The
# version number is edited into ../Doxyfile and pkg.h.

# ------------------------------------------------------------------

# The version of the pkgng software itself. See
# http://wiki.freebsd.org/pkgng/CharterAndRoadMap#Code_Releases for
# details.  Setting PKG_PATCH_LEVEL to 0 is special
#
# For testing purposes you can override these by setting any of the
# variables or PKGVERSION or PORTVERSION in the environment, but this
# should not be done routinely:

: ${PKG_MAJOR_VERSION:="1"}
: ${PKG_MINOR_VERSION="0"}
: ${PKG_PATCH_LEVEL="9"}
 
: ${PORTREVISION:=}
: ${PORTEPOCH:=}

# ------------------------------------------------------------------

# Define this to a true value in the environment if creating a
# snapshot
: ${CREATE_SNAPSHOT:="NO"}

case $PKG_PATCH_LEVEL in
    ''|0)
	case $CREATE_SNAPSHOT in
	    [yY][eE][sS])
		_patch=".0"
		;;
	    *)
		_patch=
		;;
	esac
	;;
    *)
	_patch=".${PKG_PATCH_LEVEL}"
	;;
esac

case $CREATE_SNAPSHOT in
    [yY][eE][sS])
	_snapshot=".$( date +%Y%m%d )"
	;;
    *)
	_snapshot=
	;;
esac

case $PORTREVISION in
     ''|0)
	 _portrevision=
	 ;;
     *)
	 _portrevision="_${PORTREVISION}"
	 ;;
esac

case $PORTEPOCH in
    ''|0)
	_portepoch=
	;;
    *)
	_portepoch=";${PORTEPOCH}"
	;;
esac

_pkgversion=${PKG_MAJOR_VERSION}.${PKG_MINOR_VERSION}${_patch}${_snapshot}
_portversion=${_pkgversion}${_portrevision}${_portepoch}

: ${PKGVERSION:=${_pkgversion}}
: ${PORTVERSION:=${_portversion}}


# Printout the result according to command line args
case $1 in
    pkg)
	echo $PKGVERSION
	;;
    port)
	echo $PORTVERSION
	;;
    *)
        # Print the results in a form suitable for eval by /bin/sh
	echo "PKGVERSION=\"$PKGVERSION\""
	echo "PORTVERSION=\"$PORTVERSION\""
	;;
esac

#
# That's All Folks!
#
