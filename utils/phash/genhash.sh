#!/bin/sh

echo '
/* Generated File. DO NOT EDIT. */
/*
 * List of extensions and their types.
 */

#ifndef __EXT_H__
#define __EXT_H__
struct ext_entry {
	char *ext;
	int type;
	int len;
} extlist[] = {' > extensions.h

rm -f extlist
cat extensions.txt | while read line
do
	[ "x$line" = "x" ] && continue
	echo "$line" | egrep "^#" > /dev/null
	[ $? -eq 0 ] && continue
	_OIFS="$IFS"
	IFS=","
	set -- $line
	IFS="$_OIFS"
	ext=$1
	type=$2
	len=`printf $ext | wc -c`
	echo $ext >> extlist
	echo "	{\"${ext}\"	, $type, $len}," >> extensions.h
done

echo '};' >> extensions.h
echo "#endif" >> extensions.h
./perfect -nm < extlist
rm -f extlist
