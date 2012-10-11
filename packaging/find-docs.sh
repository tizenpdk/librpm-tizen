#!/bin/sh

usage () {
cat << EOF
Usage: $0 TOP_DIR

EOF
exit 1
}

if [ -z "$1" ] ; then usage
elif [ $1 = / ] ; then echo $0: expects non-/ argument for '$1' 1>&2
elif [ ! -d $1 ] ; then
 echo $0: $1: no such directory
 exit 1
else TOP_DIR="`echo $1|sed -e 's:/$::'`"
fi
shift

DOC_NAME=documentation.list
touch $DOC_NAME

find $TOP_DIR -type f -o -type l | sed '
s:'"$TOP_DIR"'::
s:\(.*/man/man./.*\.[0-9]\):%doc \1:
s:\(.*/man/*/man./.*\.[0-9]\):%doc \1:
s:\(.*/gtk-doc/html/.*\):%doc \1:
s:\(.*/info/.*\info.*\):%doc \1:
s:^\([^%].*\)::
/^$/d' >> $DOC_NAME


exit 0
