#! /bin/bash
######################################################################
#                                                                    #
# Copyright (c) 2013, Niamkik <niamkik@gmail.com>                    # 
# All rights reserved.                                               #
#                                                                    #
# Redistribution and use in source and binary forms, with or without #
# modification, are permitted provided that the following conditions #
# are met:                                                           #
#                                                                    #
# 1. Redistributions of source code must retain the above copyright  #
#    notice, this list of conditions and the following disclaimer.   #
#                                                                    #
# 2. Redistributions in binary form must reproduce the above         #
#    copyright notice, this list of conditions and the following     #
#    disclaimer in the documentation and/or other materials provided #
#    with the distribution.                                          #
#                                                                    #
# THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND             #
# CONTRIBUTORS "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES,        #
# INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF           #
# MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE           #
# DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS  #
# BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,           #
# EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED    #
# TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,      #
# DATA, OR PROFITS; OR BUSINESS INTERRUPTION)HOWEVER CAUSED AND ON   #
# ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR #
# TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF #
# THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF    #
# SUCH DAMAGE.                                                       #
#                                                                    #
# The views and conclusions contained in the software and            #
# documentation are those of the authors and should not be           #
# interpreted as representing official policies, either expressed or #
# implied, of the FreeBSD Project.                                   #
#                                                                    #
# info:                                                              #
#       - original ksh autocompletion script:                        #
#         github.com/pkgng/pkgng/blob/master/scripts/zsh/_pkg        #
#                                                                    #
# Todo:                                                              #
#       - find solution for completion after subcommand              #
#       - find solution for print long options details or only       # 
#         options                                                    #
#                                                                    #
######################################################################

_pkg_installed () {
    local ipkg

    # store local installed package
    ipkg=$(pkg query "%n-%v")
    printf $ipkg
}

_pkg_available_name () {
    local anpkg

    # store all available package with name only
    anpkg=$(pkg rquery "%n")
    printf $anpkg
}

_pkg_available () {
    local apkg
    
    # store all available package
    apkg=$(pkg rquery "%n-%v")
    printf $apkg
}

_pkg () {

    local cur prev opts lopts
    COMPREPLY=()

    # get command name
    cur="${COMP_WORDS[COMP_CWORD]}"
    
    # get first arguments
    prev="${COMP_WORDS[COMP_CWORD-1]}"
    
    # init opts for first completion
    opts='add audit autoremove backup check clean create delete
          fetch help info install query rquery search set shell
          shlib stats update updating upgrade version which'

    # init lopts for second completion with details
    lopts=( 'add[Registers a package and installs it on the system]'
            'audit[Reports vulnerable packages]'
            'autoremove[Removes orphan packages]'
            'backup[Backs-up and restores the local package database]'
            'check[Checks for missing dependencies and database consistency]'
            'clean[Cleans old packages from the cache]'
            'create[Creates software package distributions]'
            'delete[Deletes packages from the database and the system]'
            'fetch[Fetches packages from a remote repository]'
            'help[Displays help information]'
	    'info[Displays information about installed packages]'
            'install[Installs packages from remote package repositories]'
            'query[Queries information about installed packages]'
            'register[Registers a package into the local database]'
            'remove[Deletes packages from the database and the system]'
            'repo[Creates a package repository catalogue]'
            'rquery[Queries information in repository catalogues]'
            'search[Performs a search of package repository catalogues]'
            'set[Modifies information about packages in the local database]'
            'shell[Opens a debug shell]'
            'shlib[Displays which packages link against a specific shared library]'
            'stats[Displays package database statistics]'
            'update[Updates package repository catalogues]'
            'updating[Displays UPDATING information for a package]'
            'upgrade[Performs upgrades of packaged software distributions]'
            'version[Displays the versions of installed packages]'
            'which[Displays which package installed a specific file]' )

    # switch on second arguments
    case "${prev}" in 

 	add) 
	    COMPREPLY=( 
		$(compgen -f file *.t?z --) 
	    )
	    return 0 ;;

        audit) 
	    COMPREPLY=( 
		'-F[Fetch the database before checking.]'
		'-q[Quiet]'
		$(compgen -W `_pkg_installed` -- ${cur}) 
	    )
	    return 0 ;;

        autoremove) 
	    COMPREPLY=( 
		'-n'
		'-y'
		$(compgen -W `_pkg_installed` -- ${cur})  
	    )
	    return 0 ;;

        backup) 
	    if [ -z "$COMPREPLY" ] 
	    then
		COMPREPLY=(
		    '-d'
		    '-r'
		)
	    fi

	    if [ "$COMPREPLY" == "-d" ]
	    then
		COMPREPLY=(
		    $(compgen -f file *)
		)
	    fi
	    return 0 ;;

        check) 
	    COMPREPLY=(
		'-B[reanalyse SHLIBS of installed packages]' 
		'-d[check for and install missing dependencies]' 
		'-r[recompute sizes and checksums of installed]' 
		'-s[find invalid checksums]' 
		'-v[Be verbose]' 
		'(-g -x -X)-a[Process all packages]' 
		'(-x -X -a)-g[Process packages that matches glob]'
		'(-g -X -a)-x[Process packages that matches regex]'
		'(-g -x -a)-X[Process packages that matches extended regex]'
	    )
	    return 0 ;;

        clean) 
	    return 0 ;;
	
        create) 
	    COMPREPLY=(
		'-r[Root directory] -/'
		'-m[Manifest directory] -/'
		'-f[format]'
		'-o[Ouput directory] -/'
		'(-g -x -X)-a[Process all packages]'
		'(-x -X -a)-g[Process packages that matches glob]'
		'(-g -X -a)-x[Process packages that matches regex]'
		'(-g -x -a)-X[Process packages that matches extended regex]'
		'*:Package:_pkg_installed'
	    )
	    return 0 ;;

        delete|remove) 
	    COMPREPLY=(
		'(-y)-n[Assume yes when asked for confirmation]'
		'(-n)-y[Assume no (dry run) for confirmations]'
		'-f[Forces packages to be removed]'
		'(-g -x -X)-a[Process all packages]'
		'(-x -X -a)-g[Process packages that matches glob]'
		'(-g -X -a)-x[Process packages that matches regex]'
		'(-g -x -a)-X[Process packages that matches extended regex]'
		'*:Package:_pkg_installed'
	    )
	    return 0 ;;

        fetch) 
	    COMPREPLY=(
		'-y[Assume yes when asked for confirmation]'
		'-L[Do not try to update the repository metadata]'
		'-q[Be quiet]'
		'(-g -x -X)-a[Process all packages]'
		'(-x -X -a)-g[Process packages that matches glob]'
		'(-g -X -a)-x[Process packages that matches regex]' 
		'(-g -x -a)-X[Process packages that matches extended regex]'
		'*:Available packages:_pkg_available'
	    )
	    return 0 ;;

        help) 
	    COMPREPLY=() && \
		return 0 ;;

        info) 
	    COMPREPLY=(
		'(-e -d -r -l -o -p -D)-f[Displays full information]'
		'(-f -d -r -l -o -p -D)-e[Returns 0 if <pkg-name> is installed]'
		'(-e -f -r -l -o -p -D)-d[Displays the dependencies]'
		'(-e -d -f -l -o -p -D)-r[Displays the reverse dependencies]'
		'(-e -d -r -f -o -p -D)-l[Displays all files]'
		'(-e -d -r -l -f -p -D)-o[Displays origin]'
		'(-e -d -r -l -o -f -D)-p[Displays prefix]'
		'(-e -d -r -l -o -p -f)-D[Displays message]'
		'-q[Be quiet]'
		'(-g -x -X -F)-a[Process all packages]'
		'(-x -X -a -F)-g[Process packages that matches glob]'
		'(-g -X -a -F)-x[Process packages that matches regex]'
		'(-g -x -a -F)-X[Process packages that matches extended regex]'
		'(-g -x -X -a)-F[Process the specified package]'
		'*:Package:_pkg_installed'
	    )
	    return 0 ;;

        install) 
	    COMPREPLY=(
		'(-y)-n[Assume yes when asked for confirmation]'
		'(-n)-y[Assume no (dry run) for confirmations]'
		'-f[Force reinstallation if needed]'
		'-R[Reinstall every package depending on matching expressions]'
		'-L[Do not try to update the repository metadata]'
		'(-x -X)-g[Process packages that matches glob]'
		'(-g -X)-x[Process packages that matches regex]'
		'(-g -x)-X[Process packages that matches extended regex]'
		'*:Available packages:_pkg_available'
	    )
	    return 0 ;;

        query) 
	    COMPREPLY=(
		'(-g -x -X -F -e)-a[Process all packages]'
		'(-x -X -a -F -e)-g[Process packages that matches glob]'
		'(-g -X -a -F -e)-x[Process packages that matches regex]'
		'(-g -x -a -F -e)-X[Process packages that matches extended regex]'
		'(-g -x -X -a -F)-e[Process packages that matches the evaluation]'
		'(-g -x -X -a -e)-F[Process the specified package]'
		':Ouput format:'
	    )
	    return 0 ;;

        register) 
	    COMPREPLY=(
		'-l[registered as a legacy format]'
		'-d[mark the package as an automatic dependency]'
		'-f[packing list file]'
		'-m[metadata directory]'
		'-a[ABI]'
		'-i[input path (aka root directory)]'
	    )
	    return 0 ;;

        repo) 
	    COMPREPLY=(
		''
		''
	    )
	    return 0 ;;

        rquery) 
	    COMPREPLY=(
		'(-g -x -X -e)-a[Process all packages]'
		'(-x -X -a -e)-g[Process packages that matches glob]'
		'(-g -X -a -e)-x[Process packages that matches regex]'
		'(-g -x -a -e)-X[Process packages that matches extended regex]'
		'(-g -x -X -a)-e[Process packages that matches the evaluation]'
	    )
	    return 0 ;;

        search) 
	    COMPREPLY=(
		'(-x -X)-g[Process packages that matches glob]'
		'(-g -X)-x[Process packages that matches regex]'
		'(-g -x)-X[Process packages that matches extended regex]'
	    )
	    return 0 ;;

        set) 
	    COMPREPLY=(
		'(-o)-A[Mark as automatic or not]'
		'(-A)-o[Change the origin]'
		'-y[Assume yes when asked for confirmation]'
		'(-g -x -X)-a[Process all packages]'
		'(-x -X -a)-g[Process packages that matches glob]'
		'(-g -X -a)-x[Process packages that matches regex]'
		'(-g -x -a)-X[Process packages that matches extended regex]'
	    )
	    return 0 ;;

        shell) 
	    COMPREPLY=()
	    return 0 ;;

        shlib) 
	    COMPREPLY=()
	    return 0 ;;

        stats) 
	    COMPREPLY=(
		'-q[Be quiet]'
		'(-l)-r[Display stats only for the local package database]'
		'(-r)-l[Display stats only for the remote package database(s)]' 
	    )
	    return 0 ;;

        update) 
	    COMPREPLY=(
		'-f[Force updating]'
		'-q[Be quiet]'
	    )
	    return 0 ;;

        updating) 
	    COMPREPLY=(
		'-d[Only entries newer than date are shown]'
		'-f[Defines a alternative location of the UPDATING file]'
	    )
	    return 0 ;;

        upgrade) 
	    COMPREPLY=(
		'(-y)-n[Assume no (dry run) for confirmations]' 
		'(-n)-y[Assume yes when asked for confirmation]' 
		'-f[Upgrade/Reinstall everything]' 
		'-L[Do not try to update the repository metadata]'
	    )
	    return 0 ;;

        version) 
	    COMPREPLY=(
		'(-P -R)-I[Use INDEX file]'
		'(-R -I)-P[Force checking against the ports tree]'
		'(-I -P)-R[Use remote repository]'
		'-o[Display package origin, instead of package name]'
		'-q[Be quiet]'
		'-v[Be verbose]'
		'(-L)-l[Display only the packages for given status flag]'
		'(-l)-L[Display only the packages without given status flag]'
	    )
	    return 0 ;;

        which) 
	    COMPREPLY=( $(compgen -W "$(compgen -A file)") )
	    return 0 ;;
    esac

    COMPREPLY=( $(compgen -W "${opts}" -- ${cur}) )
}

complete -D -F _pkg pkg
