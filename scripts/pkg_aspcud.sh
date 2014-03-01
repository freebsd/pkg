#!/bin/sh
# Copyright (c) 2014 Vsevolod Stakhov <vsevolod@FreeBSD.org>
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
#
#
# This is a simple support script to enable aspcud external solver.
# This solver can be installed from package aspcud or from the port
# in math/aspcud.
# 
# To use this utility as the external solver you can specify CUDF_SOLVER option:
# pkg -o CUDF_SOLVER=/path/to/pkg_aspcud.sh <upgrade|install|remove>


[ -z "$CLASP_OPTS" ] && CLASP_OPTS="--opt-heu=1 --sat-prepro --restarts=L,128 --heuristic=VSIDS --opt-hierarch=1 --local-restarts --del-max=200000,250 --save-progress=0 --quiet=1,1"
[ -z "$CLASP" ] && CLASP="clasp"

[ -z "$GRINGO_OPTS" ] && GRINGO_OPTS="/usr/local/share/aspcud/misc2012.lp"
[ -z "$GRINGO" ] && GRINGO="gringo"

TRENDY="-count(removed),-notuptodate(solution),-unsat_recommends(solution),-count(new)"

[ -z "$CUDF2LP_OPTS" ] && CUDF2LP_OPTS="-c $TRENDY"
[ -z "$CUDF2LP" ] && CUDF2LP="cudf2lp"

( $CUDF2LP $CUDF2LP_OPTS ) | ( $GRINGO $GRINGO_OPTS - ) | ( $CLASP $CLASP_OPTS ) \
| grep -A 1 "Answer" | sed -e '1d' | tr " " "\n" \
| grep 'in(' | sed -e 's/in("\([^,]*\)",\([0-9]*\))/package: \1\
version: \2\
installed: true\
/'
