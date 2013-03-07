lic_txt='/*
 * This file is a part of Pcompress, a chunked parallel multi-
 * algorithm lossless compression and decompression program.
 *
 * Copyright (C) 2012-2013 Moinak Ghosh. All rights reserved.
 * Use is subject to license terms.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 3 of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this program.
 * If not, see <http://www.gnu.org/licenses/>.
 *
 * moinakg@belenix.org, http://moinakg.wordpress.com/
 *      
 */
'

for fn in `find . \( -name "*.c" -o -name "*.h" \) | egrep -v '^.git'`
do
	grep "This program is free software; you can redistribute it and/or" ${fn} > /dev/null
	if [ $? -ne 0 ]
	then
		echo "Adding license to ${fn}"
		echo "${lic_txt}" > ${fn}.lic
		cat ${fn} >> ${fn}.lic
		cp ${fn}.lic ${fn}
		rm ${fn}.lic
		continue
	fi
		
	sed -i '
/ \* This program includes partly\-modified public domain\/LGPL source/d
/ \* This program includes partly\-modified public domain source/d
/ \* code from the LZMA SDK: http:\/\/www.7-zip.org\/sdk.html/d
s/Copyright (C) 2012 /Copyright (C) 2012-2013 /' ${fn}

	grep "You should have received a copy of the GNU Lesser General Public" ${fn} > /dev/null
	if [ $? -ne 0 ]
	then
		sed -i '
/ \* Lesser General Public License for more details./a\
 \*\
 \* You should have received a copy of the GNU Lesser General Public\
 \* License along with this program.\
 \* If not, see <http:\/\/www.gnu.org\/licenses\/>.
' ${fn}
	fi
done


