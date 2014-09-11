#!/usr/bin/ksh


exclude_list='^bsc|^filters/packjpg|^filters/lzp|^utils/qsort_gnu.h|^\.git/|^\.gitignore|^COPYING|^utils/fixlic.sh'
my_path=./licensing_utils

target_dir=../pcompress_mplv2
if [ -d ${target_dir} ]
then
	if [ "$1" = "-f" ]
	then
		rm -rf ${target_dir}
	else
		echo "${target_dir} exists"
		exit 1
	fi
fi

mkdir -p ${target_dir}

for dir in `find * -type d`
do
	echo ${dir} | egrep "$exclude_list" > /dev/null
	[ $? -eq 0 ] && continue
	mkdir -p ${target_dir}/${dir}
done

for f in `find * -type f`
do
	echo ${f} | egrep "$exclude_list" > /dev/null
	[ $? -eq 0 ] && continue
	case ${f} in
	*.c)
		cp ${my_path}/lic_header.c ${target_dir}/${f}
		cat ${f} | sed '/This file is a part of Pcompress,/,/moinakg@belenix.org,/d' >> ${target_dir}/${f}
	;;
	*.h)
		cp ${my_path}/lic_header.c ${target_dir}/${f}
		cat ${f} | sed '/This file is a part of Pcompress,/,/moinakg@belenix.org,/d' >> ${target_dir}/${f}
	;;
	*.s)
		cp ${my_path}/lic_header.s ${target_dir}/${f}
		cat ${f} | sed '/This file is a part of Pcompress,/,/moinakg@belenix.org,/d' >> ${target_dir}/${f}
	;;
	*.asm)
		cp ${my_path}/lic_header.asm ${target_dir}/${f}
		cat ${f} | sed '/This file is a part of Pcompress,/,/moinakg@belenix.org,/d' >> ${target_dir}/${f}
	;;
	esac
done

f=Makefile.in
cp ${my_path}/lic_header.sh ${target_dir}/${f}
cat ${f} | sed '
s@\$(PJPGOBJS)@@
s@\$(PPNMOBJS)@@
s@\$(LZPOBJS)@@
s@BASE_CPPFLAGS =@BASE_CPPFLAGS = -D_MPLV2_LICENSE_@
s@\-I\./filters/packjpg@@
s@\-I\./filters/lzp@@
/This file is a part of Pcompress,/,/moinakg@belenix.org,/d
' >> ${target_dir}/${f}

cat config | sed 's@my_license=LGPLv3@my_license=MPLv2@' > ${target_dir}/config.new
cp ${my_path}/lic_header.sh ${target_dir}/config
cat ${target_dir}/config.new | sed '/This file is a part of Pcompress,/,/moinakg@belenix.org,/d' >> ${target_dir}/config
rm -f ${target_dir}/config.new

cp ${my_path}/LICENSE.MPLV2 ${target_dir}
