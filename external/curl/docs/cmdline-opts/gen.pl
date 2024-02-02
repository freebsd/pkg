#!/usr/bin/env perl
#***************************************************************************
#                                  _   _ ____  _
#  Project                     ___| | | |  _ \| |
#                             / __| | | | |_) | |
#                            | (__| |_| |  _ <| |___
#                             \___|\___/|_| \_\_____|
#
# Copyright (C) Daniel Stenberg, <daniel@haxx.se>, et al.
#
# This software is licensed as described in the file COPYING, which
# you should have received as part of this distribution. The terms
# are also available at https://curl.se/docs/copyright.html.
#
# You may opt to use, copy, modify, merge, publish, distribute and/or sell
# copies of the Software, and permit persons to whom the Software is
# furnished to do so, under the terms of the COPYING file.
#
# This software is distributed on an "AS IS" basis, WITHOUT WARRANTY OF ANY
# KIND, either express or implied.
#
# SPDX-License-Identifier: curl
#
###########################################################################

=begin comment

This script generates the manpage.

Example: gen.pl <command> [files] > curl.1

Dev notes:

We open *input* files in :crlf translation (a no-op on many platforms) in
case we have CRLF line endings in Windows but a perl that defaults to LF.
Unfortunately it seems some perls like msysgit cannot handle a global input-only
:crlf so it has to be specified on each file open for text input.

=end comment
=cut

my %optshort;
my %optlong;
my %helplong;
my %arglong;
my %redirlong;
my %protolong;
my %catlong;

use POSIX qw(strftime);
my @ts;
if (defined($ENV{SOURCE_DATE_EPOCH})) {
    @ts = localtime($ENV{SOURCE_DATE_EPOCH});
} else {
    @ts = localtime;
}
my $date = strftime "%B %d %Y", @ts;
my $year = strftime "%Y", @ts;
my $version = "unknown";
my $globals;

open(INC, "<../../include/curl/curlver.h");
while(<INC>) {
    if($_ =~ /^#define LIBCURL_VERSION \"([0-9.]*)/) {
        $version = $1;
        last;
    }
}
close(INC);

# get the long name version, return the man page string
sub manpageify {
    my ($k)=@_;
    my $l;
    my $klong = $k;
    # quote "bare" minuses in the long name
    $klong =~ s/-/\\-/g;
    if($optlong{$k} ne "") {
        # both short + long
        $l = "\\fI-".$optlong{$k}.", \\-\\-$klong\\fP";
    }
    else {
        # only long
        $l = "\\fI\\-\\-$klong\\fP";
    }
    return $l;
}

sub printdesc {
    my @desc = @_;
    my $exam = 0;
    for my $d (@desc) {
        print $d;
    }
}

sub seealso {
    my($standalone, $data)=@_;
    if($standalone) {
        return sprintf
            ".SH \"SEE ALSO\"\n$data\n";
    }
    else {
        return "See also $data. ";
    }
}

sub overrides {
    my ($standalone, $data)=@_;
    if($standalone) {
        return ".SH \"OVERRIDES\"\n$data\n";
    }
    else {
        return $data;
    }
}

sub protocols {
    my ($standalone, $data)=@_;
    if($standalone) {
        return ".SH \"PROTOCOLS\"\n$data\n";
    }
    else {
        return "($data) ";
    }
}

sub too_old {
    my ($version)=@_;
    my $a = 999999;
    if($version =~ /^(\d+)\.(\d+)\.(\d+)/) {
        $a = $1 * 1000 + $2 * 10 + $3;
    }
    elsif($version =~ /^(\d+)\.(\d+)/) {
        $a = $1 * 1000 + $2 * 10;
    }
    if($a < 7500) {
        # we consider everything before 7.50.0 to be too old to mention
        # specific changes for
        return 1;
    }
    return 0;
}

sub added {
    my ($standalone, $data)=@_;
    if(too_old($data)) {
        # do not mention ancient additions
        return "";
    }
    if($standalone) {
        return ".SH \"ADDED\"\nAdded in curl version $data\n";
    }
    else {
        return "Added in $data. ";
    }
}

sub render {
    my ($fh, $f, $line) = @_;
    my @desc;
    my $tablemode = 0;
    my $header = 0;
    # if $top is TRUE, it means a top-level page and not a command line option
    my $top = ($line == 1);
    my $quote;
    $start = 0;

    while(<$fh>) {
        my $d = $_;
        $line++;
        if($d =~ /^\.(SH|BR|IP|B)/) {
            print STDERR "$f:$line:1:ERROR: nroff instruction in input: \".$1\"\n";
            return 4;
        }
        if(/^ *<!--/) {
            # skip comments
            next;
        }
        if((!$start) && ($_ =~ /^[\r\n]*\z/)) {
            # skip leading blank lines
            next;
        }
        $start = 1;
        if(/^# (.*)/) {
            $header = 1;
            if($top != 1) {
                # ignored for command line options
                $blankline++;
                next;
            }
            push @desc, ".SH $1\n";
            next;
        }
        elsif(/^###/) {
            print STDERR "$f:$line:1:ERROR: ### header is not supported\n";
            exit 3;
        }
        elsif(/^## (.*)/) {
            my $word = $1;
            # if there are enclosing quotes, remove them first
            $word =~ s/[\"\'](.*)[\"\']\z/$1/;

            # remove backticks from headers
            $words =~ s/\`//g;

            # if there is a space, it needs quotes
            if($word =~ / /) {
                $word = "\"$word\"";
            }
            if($top == 1) {
                push @desc, ".IP $word\n";
            }
            else {
                if(!$tablemode) {
                    push @desc, ".RS\n";
                    $tablemode = 1;
                }
                push @desc, ".IP $word\n";
            }
            $header = 1;
            next;
        }
        elsif(/^##/) {
            if($top == 1) {
                print STDERR "$f:$line:1:ERROR: ## empty header top-level mode\n";
                exit 3;
            }
            if($tablemode) {
                # end of table
                push @desc, ".RE\n.IP\n";
                $tablmode = 0;
            }
            $header = 1;
            next;
        }
        elsif(/^\.(IP|RS|RE)/) {
            my ($cmd) = ($1);
            print STDERR "$f:$line:1:ERROR: $cmd detected, use ##-style\n";
            return 3;
        }
        elsif(/^[ \t]*\n/) {
            # count and ignore blank lines
            $blankline++;
            next;
        }
        elsif($d =~ /^    (.*)/) {
            my $word = $1;
            if(!$quote) {
                push @desc, ".nf\n";
            }
            $quote = 1;
            $d = "$word\n";
        }
        elsif($quote && ($d !~ /^    (.*)/)) {
            # end of quote
            push @desc, ".fi\n";
            $quote = 0;
        }

        $d =~ s/`%DATE`/$date/g;
        $d =~ s/`%VERSION`/$version/g;
        $d =~ s/`%GLOBALS`/$globals/g;

        # convert single backslahes to doubles
        $d =~ s/\\/\\\\/g;

        # convert backticks to double quotes
        $d =~ s/\`/\"/g;

        if(!$quote && $d =~ /--/) {
            # scan for options in longest-names first order
            for my $k (sort {length($b) <=> length($a)} keys %optlong) {
                # --tlsv1 is complicated since --tlsv1.2 etc are also
                # acceptable options!
                if(($k eq "tlsv1") && ($d =~ /--tlsv1\.[0-9]\\f/)) {
                    next;
                }
                my $l = manpageify($k);
                $d =~ s/\-\-$k([^a-z0-9-])/$l$1/g;
            }
        }

        if($d =~ /\(Added in ([0-9.]+)\)/i) {
            my $ver = $1;
            if(too_old($ver)) {
                $d =~ s/ *\(Added in $ver\)//gi;
            }
        }

        if(!$quote && ($d =~ /^(.*)  /)) {
            printf STDERR "$f:$line:%d:ERROR: 2 spaces detected\n",
                length($1);
            return 3;
        }
        # quote minuses in the output
        $d =~ s/([^\\])-/$1\\-/g;
        # replace single quotes
        $d =~ s/\'/\\(aq/g;
        # handle double quotes or periods first on the line
        $d =~ s/^([\.\"])/\\&$1/;
        # **bold**
        $d =~ s/\*\*(\S.*?)\*\*/\\fB$1\\fP/g;
        # *italics*
        $d =~ s/\*(\S.*?)\*/\\fI$1\\fP/g;

        # trim trailing spaces
        $d =~ s/[ \t]+\z//;
        push @desc, "\n" if($blankline && !$header);
        $blankline = 0;
        push @desc, $d;
        $header = 0;

    }
    if($tablemode) {
        # end of table
        push @desc, ".RE\n.IP\n";
    }
    return @desc;
}

sub single {
    my ($f, $standalone)=@_;
    my $fh;
    open($fh, "<:crlf", "$f") ||
        return 1;
    my $short;
    my $long;
    my $tags;
    my $added;
    my $protocols;
    my $arg;
    my $mutexed;
    my $requires;
    my $category;
    my @seealso;
    my $copyright;
    my $spdx;
    my @examples; # there can be more than one
    my $magic; # cmdline special option
    my $line;
    my $dline;
    my $multi;
    my $scope;
    my $experimental;
    my $start;
    my $list; # identifies the list, 1 example, 2 see-also
    while(<$fh>) {
        $line++;
        if(/^ *<!--/) {
            next;
        }
        if(!$start) {
            if(/^---/) {
                $start = 1;
            }
            next;
        }
        if(/^Short: *(.)/i) {
            $short=$1;
        }
        elsif(/^Long: *(.*)/i) {
            $long=$1;
        }
        elsif(/^Added: *(.*)/i) {
            $added=$1;
        }
        elsif(/^Tags: *(.*)/i) {
            $tags=$1;
        }
        elsif(/^Arg: *(.*)/i) {
            $arg=$1;
        }
        elsif(/^Magic: *(.*)/i) {
            $magic=$1;
        }
        elsif(/^Mutexed: *(.*)/i) {
            $mutexed=$1;
        }
        elsif(/^Protocols: *(.*)/i) {
            $protocols=$1;
        }
        elsif(/^See-also: +(.+)/i) {
            if($seealso) {
                print STDERR "ERROR: duplicated See-also in $f\n";
                return 1;
            }
            push @seealso, $1;
        }
        elsif(/^See-also:/i) {
            $list=2;
        }
        elsif(/^  *- (.*)/i && ($list == 2)) {
            push @seealso, $1;
        }
        elsif(/^Requires: *(.*)/i) {
            $requires=$1;
        }
        elsif(/^Category: *(.*)/i) {
            $category=$1;
        }
        elsif(/^Example: +(.+)/i) {
            push @examples, $1;
        }
        elsif(/^Example:/i) {
            # '1' is the example list
            $list = 1;
        }
        elsif(/^  *- (.*)/i && ($list == 1)) {
            push @examples, $1;
        }
        elsif(/^Multi: *(.*)/i) {
            $multi=$1;
        }
        elsif(/^Scope: *(.*)/i) {
            $scope=$1;
        }
        elsif(/^Experimental: yes/i) {
            $experimental=1;
        }
        elsif(/^C: (.*)/i) {
            $copyright=$1;
        }
        elsif(/^SPDX-License-Identifier: (.*)/i) {
            $spdx=$1;
        }
        elsif(/^Help: *(.*)/i) {
            ;
        }
        elsif(/^---/) {
            $start++;
            if(!$long) {
                print STDERR "ERROR: no 'Long:' in $f\n";
                return 1;
            }
            if(!$category) {
                print STDERR "ERROR: no 'Category:' in $f\n";
                return 2;
            }
            if(!$examples[0]) {
                print STDERR "$f:$line:1:ERROR: no 'Example:' present\n";
                return 2;
            }
            if(!$added) {
                print STDERR "$f:$line:1:ERROR: no 'Added:' version present\n";
                return 2;
            }
            if(!$seealso[0]) {
                print STDERR "$f:$line:1:ERROR: no 'See-also:' field present\n";
                return 2;
            }
            if(!$copyright) {
                print STDERR "$f:$line:1:ERROR: no 'C:' field present\n";
                return 2;
            }
            if(!$spdx) {
                print STDERR "$f:$line:1:ERROR: no 'SPDX-License-Identifier:' field present\n";
                return 2;
            }
            last;
        }
        else {
            chomp;
            print STDERR "$f:$line:1:WARN: unrecognized line in $f, ignoring:\n:'$_';"
        }
    }

    if($start < 2) {
        print STDERR "$f:1:1:ERROR: no proper meta-data header\n";
        return 2;
    }

    my @desc = render($fh, $f, $line);
    close($fh);
    if($tablemode) {
        # end of table
        push @desc, ".RE\n.IP\n";
    }
    my $opt;

    if(defined($short) && $long) {
        $opt = "-$short, --$long";
    }
    elsif($short && !$long) {
        $opt = "-$short";
    }
    elsif($long && !$short) {
        $opt = "--$long";
    }

    if($arg) {
        $opt .= " $arg";
    }

    # quote "bare" minuses in opt
    $opt =~ s/-/\\-/g;
    if($standalone) {
        print ".TH curl 1 \"30 Nov 2016\" \"curl 7.52.0\" \"curl manual\"\n";
        print ".SH OPTION\n";
        print "curl $opt\n";
    }
    else {
        print ".IP \"$opt\"\n";
    }
    if($protocols) {
        print protocols($standalone, $protocols);
    }

    if($standalone) {
        print ".SH DESCRIPTION\n";
    }

    if($experimental) {
        print "**WARNING**: this option is experimental. Do not use in production.\n\n";
    }

    printdesc(@desc);
    undef @desc;

    if($scope) {
        if($scope eq "global") {
            print "\nThis option is global and does not need to be specified for each use of --next.\n";
        }
        else {
            print STDERR "$f:$line:1:ERROR: unrecognized scope: '$scope'\n";
            return 2;
        }
    }

    my @extra;
    if($multi eq "single") {
        push @extra, "\nIf --$long is provided several times, the last set ".
            "value is used.\n";
    }
    elsif($multi eq "append") {
        push @extra, "\n--$long can be used several times in a command line\n";
    }
    elsif($multi eq "boolean") {
        my $rev = "no-$long";
        # for options that start with "no-" the reverse is then without
        # the no- prefix
        if($long =~ /^no-/) {
            $rev = $long;
            $rev =~ s/^no-//;
        }
        push @extra,
            "\nProviding --$long multiple times has no extra effect.\n".
            "Disable it again with \\-\\-$rev.\n";
    }
    elsif($multi eq "mutex") {
        push @extra,
            "\nProviding --$long multiple times has no extra effect.\n";
    }
    elsif($multi eq "custom") {
        ; # left for the text to describe
    }
    else {
        print STDERR "$f:$line:1:ERROR: unrecognized Multi: '$multi'\n";
        return 2;
    }

    printdesc(@extra);

    my @foot;

    my $mstr;
    my $and = 0;
    my $num = scalar(@seealso);
    if($num > 2) {
        # use commas up to this point
        $and = $num - 1;
    }
    my $i = 0;
    for my $k (@seealso) {
        if(!$helplong{$k}) {
            print STDERR "$f:$line:1:WARN: see-also a non-existing option: $k\n";
        }
        my $l = manpageify($k);
        my $sep = " and";
        if($and && ($i < $and)) {
            $sep = ",";
        }
        $mstr .= sprintf "%s$l", $mstr?"$sep ":"";
        $i++;
    }
    push @foot, seealso($standalone, $mstr);

    if($requires) {
        my $l = manpageify($long);
        push @foot, "$l requires that the underlying libcurl".
            " was built to support $requires. ";
    }
    if($mutexed) {
        my @m=split(/ /, $mutexed);
        my $mstr;
        for my $k (@m) {
            if(!$helplong{$k}) {
                print STDERR "WARN: $f mutexes a non-existing option: $k\n";
            }
            my $l = manpageify($k);
            $mstr .= sprintf "%s$l", $mstr?" and ":"";
        }
        push @foot, overrides($standalone,
                              "This option is mutually exclusive to $mstr. ");
    }
    if($examples[0]) {
        my $s ="";
        $s="s" if($examples[1]);
        print "\nExample$s:\n.nf\n";
        foreach my $e (@examples) {
            $e =~ s!\$URL!https://example.com!g;
            #$e =~ s/-/\\-/g;
            #$e =~ s/\'/\\(aq/g;
            # convert single backslahes to doubles
            $e =~ s/\\/\\\\/g;
            print " curl $e\n";
        }
        print ".fi\n";
    }
    if($added) {
        push @foot, added($standalone, $added);
    }
    if($foot[0]) {
        print "\n";
        my $f = join("", @foot);
        $f =~ s/ +\z//; # remove trailing space
        print "$f\n";
    }
    return 0;
}

sub getshortlong {
    my ($f)=@_;
    open(F, "<:crlf", "$f");
    my $short;
    my $long;
    my $help;
    my $arg;
    my $protocols;
    my $category;
    my $start = 0;
    while(<F>) {
        if(!$start) {
            if(/^---/) {
                $start = 1;
            }
            next;
        }
        if(/^Short: (.)/i) {
            $short=$1;
        }
        elsif(/^Long: (.*)/i) {
            $long=$1;
        }
        elsif(/^Help: (.*)/i) {
            $help=$1;
        }
        elsif(/^Arg: (.*)/i) {
            $arg=$1;
        }
        elsif(/^Protocols: (.*)/i) {
            $protocols=$1;
        }
        elsif(/^Category: (.*)/i) {
            $category=$1;
        }
        elsif(/^---/) {
            last;
        }
    }
    close(F);
    if($short) {
        $optshort{$short}=$long;
    }
    if($long) {
        $optlong{$long}=$short;
        $helplong{$long}=$help;
        $arglong{$long}=$arg;
        $protolong{$long}=$protocols;
        $catlong{$long}=$category;
    }
}

sub indexoptions {
    my (@files) = @_;
    foreach my $f (@files) {
        getshortlong($f);
    }
}

sub header {
    my ($f)=@_;
    my $fh;
    open($fh, "<:crlf", "$f");
    my @d = render($fh, $f, 1);
    close($fh);
    printdesc(@d);
}

sub listhelp {
    print <<HEAD
/***************************************************************************
 *                                  _   _ ____  _
 *  Project                     ___| | | |  _ \\| |
 *                             / __| | | | |_) | |
 *                            | (__| |_| |  _ <| |___
 *                             \\___|\\___/|_| \\_\\_____|
 *
 * Copyright (C) Daniel Stenberg, <daniel\@haxx.se>, et al.
 *
 * This software is licensed as described in the file COPYING, which
 * you should have received as part of this distribution. The terms
 * are also available at https://curl.se/docs/copyright.html.
 *
 * You may opt to use, copy, modify, merge, publish, distribute and/or sell
 * copies of the Software, and permit persons to whom the Software is
 * furnished to do so, under the terms of the COPYING file.
 *
 * This software is distributed on an "AS IS" basis, WITHOUT WARRANTY OF ANY
 * KIND, either express or implied.
 *
 * SPDX-License-Identifier: curl
 *
 ***************************************************************************/
#include "tool_setup.h"
#include "tool_help.h"

/*
 * DO NOT edit tool_listhelp.c manually.
 * This source file is generated with the following command in an autotools
 * build:
 *
 * "make listhelp"
 */

const struct helptxt helptext[] = {
HEAD
        ;
    foreach my $f (sort keys %helplong) {
        my $long = $f;
        my $short = $optlong{$long};
        my @categories = split ' ', $catlong{$long};
        my $bitmask = ' ';
        my $opt;

        if(defined($short) && $long) {
            $opt = "-$short, --$long";
        }
        elsif($long && !$short) {
            $opt = "    --$long";
        }
        for my $i (0 .. $#categories) {
            $bitmask .= 'CURLHELP_' . uc $categories[$i];
            # If not last element, append |
            if($i < $#categories) {
                $bitmask .= ' | ';
            }
        }
        $bitmask =~ s/(?=.{76}).{1,76}\|/$&\n  /g;
        my $arg = $arglong{$long};
        if($arg) {
            $opt .= " $arg";
        }
        my $desc = $helplong{$f};
        $desc =~ s/\"/\\\"/g; # escape double quotes

        my $line = sprintf "  {\"%s\",\n   \"%s\",\n  %s},\n", $opt, $desc, $bitmask;

        if(length($opt) > 78) {
            print STDERR "WARN: the --$long name is too long\n";
        }
        elsif(length($desc) > 78) {
            print STDERR "WARN: the --$long description is too long\n";
        }
        print $line;
    }
    print <<FOOT
  { NULL, NULL, CURLHELP_HIDDEN }
};
FOOT
        ;
}

sub listcats {
    my %allcats;
    foreach my $f (sort keys %helplong) {
        my @categories = split ' ', $catlong{$f};
        foreach (@categories) {
            $allcats{$_} = undef;
        }
    }
    my @categories;
    foreach my $key (keys %allcats) {
        push @categories, $key;
    }
    @categories = sort @categories;
    unshift @categories, 'hidden';
    for my $i (0..$#categories) {
        print '#define ' . 'CURLHELP_' . uc($categories[$i]) . ' ' . "1u << " . $i . "u\n";
    }
}

sub listglobals {
    my (@files) = @_;
    my @globalopts;

    # Find all global options and output them
    foreach my $f (sort @files) {
        open(F, "<:crlf", "$f") ||
            next;
        my $long;
        my $start = 0;
        while(<F>) {
            if(/^---/) {
                if(!$start) {
                    $start = 1;
                    next;
                }
                else {
                    last;
                }
            }
            if(/^Long: *(.*)/i) {
                $long=$1;
            }
            elsif(/^Scope: global/i) {
                push @globalopts, $long;
                last;
            }
        }
        close(F);
    }
    return $ret if($ret);
    for my $e (0 .. $#globalopts) {
        $globals .= sprintf "%s--%s",  $e?($globalopts[$e+1] ? ", " : " and "):"",
            $globalopts[$e],;
    }
}

sub noext {
    my $in = $_[0];
    $in =~ s/\.d//;
    return $in;
}

sub sortnames {
    return noext($a) cmp noext($b);
}

sub mainpage {
    my (@files) = @_;
    my $ret;
    my $fh;
    open($fh, "<:crlf", "mainpage.idx") ||
        return 1;

    print <<HEADER
.\\" **************************************************************************
.\\" *                                  _   _ ____  _
.\\" *  Project                     ___| | | |  _ \\| |
.\\" *                             / __| | | | |_) | |
.\\" *                            | (__| |_| |  _ <| |___
.\\" *                             \\___|\\___/|_| \\_\\_____|
.\\" *
.\\" * Copyright (C) Daniel Stenberg, <daniel\@haxx.se>, et al.
.\\" *
.\\" * This software is licensed as described in the file COPYING, which
.\\" * you should have received as part of this distribution. The terms
.\\" * are also available at https://curl.se/docs/copyright.html.
.\\" *
.\\" * You may opt to use, copy, modify, merge, publish, distribute and/or sell
.\\" * copies of the Software, and permit persons to whom the Software is
.\\" * furnished to do so, under the terms of the COPYING file.
.\\" *
.\\" * This software is distributed on an "AS IS" basis, WITHOUT WARRANTY OF ANY
.\\" * KIND, either express or implied.
.\\" *
.\\" * SPDX-License-Identifier: curl
.\\" *
.\\" **************************************************************************
.\\"
.\\" DO NOT EDIT. Generated by the curl project gen.pl man page generator.
.\\"
.TH curl 1 "$date" "curl $version" "curl Manual"
HEADER
        ;

    while(<$fh>) {
        my $f = $_;
        chomp $f;
        if($f =~ /^#/) {
            # stardard comment
            next;
        }
        if(/^%options/) {
            # output docs for all options
            foreach my $f (sort sortnames @files) {
                $ret += single($f, 0);
            }
        }
        else {
            # render the file
            header($f);
        }
    }
    close($fh);
    exit $ret if($ret);
}

sub showonly {
    my ($f) = @_;
    if(single($f, 1)) {
        print STDERR "$f: failed\n";
    }
}

sub showprotocols {
    my %prots;
    foreach my $f (keys %optlong) {
        my @p = split(/ /, $protolong{$f});
        for my $p (@p) {
            $prots{$p}++;
        }
    }
    for(sort keys %prots) {
        printf "$_ (%d options)\n", $prots{$_};
    }
}

sub getargs {
    my ($f, @s) = @_;
    if($f eq "mainpage") {
        listglobals(@s);
        mainpage(@s);
        return;
    }
    elsif($f eq "listhelp") {
        listhelp();
        return;
    }
    elsif($f eq "single") {
        showonly($s[0]);
        return;
    }
    elsif($f eq "protos") {
        showprotocols();
        return;
    }
    elsif($f eq "listcats") {
        listcats();
        return;
    }

    print "Usage: gen.pl <mainpage/listhelp/single FILE/protos/listcats> [files]\n";
}

#------------------------------------------------------------------------

my $cmd = shift @ARGV;
my @files = @ARGV; # the rest are the files

# learn all existing options
indexoptions(@files);

getargs($cmd, @files);
