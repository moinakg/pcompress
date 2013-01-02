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
					exit 1
				fi
				cmd="../../pcompress -d ${tf}.pz ${tf}.1"
				echo "Running $cmd"
				eval $cmd
				if [ $? -ne 0 ]
				then
					echo "FATAL: Decompression errored."
					exit 1
				fi
				diff ${tf} ${tf}.1 > /dev/null
				if [ $? -ne 0 ]
				then
					echo "FATAL: Decompression was not correct"
					exit 1
				fi
				rm -f ${tf}.pz ${tf}.1
			done
		done
	done
done

echo "#################################################"
echo ""

