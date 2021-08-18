# Fix all bugs reported

This is the number one requirement :)

# Replace dependencies by dep formula

Dep formula is a definition of a formula where can represent requirements
(keyword) and packages but also can define alternatives of those:

name1 = 1.0 | name2 != 1.0, name3 > 1.0 < 2.0 != 1.5, name4 +opt1 -opt2

Difficulty: hard

# Remove sqlite calls from frontend for (r)query

sqlite should be a technical detail and thus should not be exposed by the
library

Difficulty: hard

# Explore a rework of the library dependencies

Would be nice to have a better mechanism that could track the symbols of the
libraries (including versions)

RPM already does this

# Allow duplicate package handling

Allow multiple packages having the same name as long as they don't conflict

* pkg.conf:

  duplicatenumber: 2
  duplicate: { FreeBSD-kernel-*: 3, clibs: 5 }

* Drop unique index on packages(name) or make it on name,version
* annotation saveme
* Upgrade:

  * get_local_pkg()

    * check saveme
    *  ensure only one
    *  in summary: check for saveme annotation, change to install

  * pkg_add()

    * check for saveme annotation, remove upgrade flag

# Teach pkg repo to keep a list of unavailable packages

When a build fail the package is not added to the repository. As such, a user who attempts to install the package is told that it does not exist, rather than it is just temporarily unavailable.

The repo building process could create a list of failed packages during the build, and provide that as part of the repo in the form of a UCL file. If there are no result when looking for a package, we could check the failed packages list, and provide the user with a reason why the package is not available (build failed, RESTRICTED, BROKEN, EXPIRED, etc)

A similar process may also make sense for the MOVED file.

# Add a mechanism to allow a user to know when a package no longer exists in the remote repos

pkg upgrade could grow a way to complain about packages it cannot upgrade because they no longer exist in the repo. When combined with the 'failed packages' list, and possibly the MOVED list, this could provide a reliable mechanism for the user to know that the package they have no longer exists in the ports tree.

# Add a periodic script for reporting pkg check -d output

# add a pkg wrongabi alias if possible in pkg.conf

This would allow after a major upgrade to check what packages are still on the old ABI

# Add a FreeBSD only check on FreeBSD version (which should also be added to packages)

This would allow to only install packages for the right ABI and only if the binary was built on an
OS version which is lower or equal that the one we are running on.

# new groupinstall concept

Add some new ucl files in the ports tree do define groups of files instead of meta ports

# Test suite

* Tests for pkg audit
* Improve tests for triggers

# pkg reposync/repomerge

Currently there is a race easily obtain when you pkg install blah and the remote repository
is synced.
The idea of reposync/repomerge is to address this issue by having a command that takes two
repository directories, one being the directory served by the webserver (named www below) and
the other being the newly created repository (named new below) and does:

* Open www/packagesite and rm all packages in www/All/ that aren't in it (this clean pkgs-1)
* Copy all new/All/* to www/All/
* Copy atomically meta.conf/meta.XXX/packagesite.XXX from new/ to www/

This leaves the new/ directory with the new packagesite, the new pkgs and the
pkgs-1 from the previous run.

# Alternatives

Alternatives are install in /usr/local/share/alternatives
For a given alternative X we have a subdirectory alternatives/X
Then a files with the various alternatives names for each alternative
alternatives/php/7.2
alternatives/php/7.4

A special alternatives is provided by the repo based on DEFAULT\_VERSION when
the alternative is version based, or a specific KNOBS when not based on version
this add a new file (symlink) to the package alternatives/php/default -> 7.2

The alternative file is a ucl file, with a list of symlinks:

Description: "yeah baby"
symlinks {
	file1: target1
	file2: target2
	file3: target3
}
exec: { type = lua; script: ... },
exec: { type = shell; script: ...}

The exec part is an optional script which could be use to regenerate caches if needed for example

In case pkg 2 pkg have the default symlink then the regular conflict mechanism would be used
By default pkg priorize user defined default before package defined repo
if user defined repo to not exists then it switches to the global defaut if in non interactive
or as the user if in interractives mode

# List of "broken builds" in pkg repo metadata

Would be nice if we could provide some notes to the repo metadata where for example
a builder car specify that some packages are broken and why, or failed to build and why

# pipe long message to $PAGER

It would be nice to add a way to detect if pkg is running on a terminal and
plans to print long messages, if so, automatically pipes the message to $PAGER
