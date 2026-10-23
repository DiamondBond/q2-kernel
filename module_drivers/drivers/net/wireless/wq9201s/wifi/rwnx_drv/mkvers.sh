#!/bin/bash
#
# Outputs svn revision of directory $VERDIR into $TARGET if it changes $TARGET
# example output omitting enclosing quotes:
#
# '#define RWNX_VERS_REV "svn1676M"'
# '#define RWNX_VERS_MOD "vX.X.X.X"'
# '#define RWNX_VERS_OTH "build: username date hour"'

set -e

VERDIR=$(dirname $(readlink -f $0))
TARGET=$1

DATE_FORMAT="%b %d %Y %T"
# RWNX_VERS_MOD=$(grep RWNX_VERS_NUM $VERDIR/Makefile | cut -f2 -d=)

tmpout=$TARGET.tmp
cd $VERDIR

if (git status -uno >& /dev/null)
then
    # If git-svn find the corresponding svn revision
    if (git svn info >& /dev/null)
    then
	idx=0
	while [ ! "$svnrev" ]
	do
	    # loop on all commit to find the first one that match a svn revision
	    svnrev=$(git svn find-rev HEAD~$idx)
	    if [ $? -ne 0 ]
	    then
		break;
	    elif [ ! "$svnrev" ]
	    then
		idx=$((idx + 1))
	    elif [ $idx -gt 0 ] || (git status -uno --porcelain | grep -q '^ M')
	    then
		# If this is not the HEAD, or working copy is not clean then we're
		# not at a commited svn revision so add 'M'
		svnrev=$svnrev"M"
	    fi
	done
    fi

    # append git info (sha1 and branch name)
    git_sha1=$(git rev-parse --short HEAD)
    if (git status -uno --porcelain | grep -q '^ M')
    then
	git_sha1+="M"
    fi
    git_branch=$(git symbolic-ref --short HEAD 2>/dev/null || echo "detached")
    if [ "$svnrev" ]
    then
	svnrev="svn$svnrev ($git_sha1/$git_branch)"
    else
	svnrev="$git_sha1 ($git_branch)"
    fi
else
    svnrev="Unknown Revision"
fi

date=$(LC_TIME=C date +"$DATE_FORMAT")

cat <<EOF > $tmpout
#define WQ_WLAN_BUILD	"build $(whoami) $date"
#define WQ_WLAN_VERSION	"$svnrev"
EOF

cmp -s $TARGET $tmpout && rm -f $tmpout || mv $tmpout $TARGET
