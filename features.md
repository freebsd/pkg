# List of Features supported by pkg

## Scripts

Before running any scripts pkg will become the reaper of its children,
spawn the scripts and kill all the remaining process after the script
if finished. This is done in order to prevent the scripts from running
daemons.

### shell scripts

shells scripts are scripts run by /bin/sh at various phases:

 - post-install
 - pre-install
 - post-deinstall
 - pre-deinstall

It provides the following environment variables (see pkg-script(5) for
documentation)

 - PKG\_PREFIX
 - PKG\_ROOTDIR
 - PKG\_MSGFD
 - PKG\_UPGRADE

### lua scripts

One of the particulatiry of the lua scripts is the fact they always run in
a capsicum sandbox which prevent doing anything but accessing the filesystem

Another particularity is they run in a modified version of lua which makes all
IO operation seamlessly rootdir friendly.

Last they do prevent executing any external program, which make the lua scripts
cross installation friendly

They run at various phases:

 - post-install
 - pre-install
 - post-deinstall
 - pre-deinstall

see pkg-lua-script(5) for the provided API

Note that lua scripts are always run before shell scripts

## plist parsing (FreeBSD only)

Feature available in the plist parser (see pkg-create(8) for more details)

The format is the following

> @keyword(user,group,mode) line

or

> @keyword line

Hardcoded keywords:

 - @cwd
 - @preexec
 - @preunexec
 - @postexec
 - @postunexec
 - @exec
 - @unexec
 - @mode
 - @owner
 - @group
 - @comment
 - @dir

if a Keyword is not found then the hardworded keywords, pkg will lookup for it
in a dedicated directory for files named

> "keyword".ucl

Those files supports shell scripts and lua scripts, see pkg-keywords(5) for details.

## message

pkg supports messages in UCL format which allows to specify when a message should
be presented to the users:

 - always
 - on first install
 - on upgrade
 - on deletion

## rootdir

pkg is rootdir friendly it means pkg can install files as a user in a root
directory as if it was a chroot.

## reproducible builds

a timestamp can be provided at creation time to ensure the time used when
create the package is crontrol activating the reproducible build support.

## graphviz/dot file visualisation

by running pkg -o DOT\_FILE=something.dot or by exporting the environement
variable, pkg can generate a dot file allowing to visualise the internal
dependency tree via graphviz

## aliases

Aliases supported at runtime so one can create its own commands

## Json output

pkg can write to a unix socket or a unix pipe via the EVENT\_PIPE configuration
entry:

> pkg -o EVENT\_PIPE=path ...

this way all the event from pkg: progress of installation, warning etc will be
written in json format in those pipes allowing to easily write wrappers on top
of pkg.

## ssh protocol

packages can be installed over ssh

## sandboxing

Most of pkg operation are run inside capsicum sandbox when possible

## auto backup of libraries on upgrade

When BACKUP\_LIBRARIES is set to true, pkg will keep backups of the libraries
it may remove during upgrades

## METALOG

It is possible to ask pkg to create a metalog file to keep trace of what files
are being installed and how they should be packaged (mode, user, group etc)
usefull when installing in rootdir

## Provides/Requires

### automated via shlibs

pkg automatically keep track of the libraries exposed by a packages and required
by a packages. (it is possible to disable this behaviour via BUNDLE\_LIB variable

### manual via provides/requires keywords

What the title says

## Compression format

pkg supports the following compression format:

 - zstd
 - xz
 - bzip2
 - gz
 - none
