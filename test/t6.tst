#
# Simple compress and decompress
#
echo "#################################################"
echo "# Simple pipe mode compress and decompress"
echo "#################################################"

for algo in lzfx lz4 adapt
do
	../../pcompress 2>&1 | grep $algo > /dev/null
	[ $? -ne 0 ] && continue

	for level in 1 3
	do
		for tf in `cat files.lst`
		do
			rm -f ${tf}.*
			for seg in 1m 2m 3m
			do
				cmd="cat ${tf} | ../../pcompress -p -c ${algo} -l ${level} -s ${seg} > ${tf}.pz"
				echo "Running $cmd"
				eval $cmd
				if [ $? -ne 0 ]
				then
					echo "FATAL: Compression errored."
					rm -f ${tf}.pz
					continue
				fi
				cmd="../../pcompress -d ${tf}.pz ${tf}.1"
				echo "Running $cmd"
				eval $cmd
				if [ $? -ne 0 ]
				then
					echo "FATAL: Decompression errored."
					rm -f ${tf}.pz ${tf}.1
					continue
				fi
				diff ${tf} ${tf}.1 > /dev/null
				if [ $? -ne 0 ]
				then
					echo "FATAL: Decompression was not correct"
				fi
				rm -f ${tf}.pz ${tf}.1
			done
		done
	done
done

for algo in lz4 zlib
do
	for dopts in "" "-G -D" "-G -F" "-D"
	do
		for tf in `cat files.lst`
		do
			rm -f ${tf}.*
			for seg in 2m 21m
			do
				cmd="../../pcompress -c ${algo} -l6 -s ${seg} ${dopts} ${tf} - > ${tf}.pz"
				echo "Running $cmd"
				eval $cmd
				if [ $? -ne 0 ]
				then
					echo "FATAL: Compression errored."
					rm -f ${tf}.pz
					continue
				fi
				cmd="cat ${tf}.pz | ../../pcompress -d - ${tf}.1"
				echo "Running $cmd"
				eval $cmd
				if [ $? -ne 0 ]
				then
					echo "FATAL: Decompression errored."
					rm -f ${tf}.pz ${tf}.1
					continue
				fi
				diff ${tf} ${tf}.1 > /dev/null
				if [ $? -ne 0 ]
				then
					echo "FATAL: Decompression was not correct"
				fi
				rm -f ${tf}.pz ${tf}.1
			done
		done
	done
done

echo "#################################################"
echo ""

