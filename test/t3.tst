#
# Simple compress and decompress
#

clean() {
	for algo in lzfx lz4 zlib bzip2 lzma lzmaMt libbsc ppmd adapt adapt2
	do
		for tf in `cat files.lst`
		do
			rm -f ${tf}.${algo}
		done
	done
}

echo "#################################################"
echo "# Compress compressed files"
echo "#################################################"

clean
for algo in lzfx lz4 zlib bzip2 lzma lzmaMt libbsc ppmd adapt adapt2
do
	../../pcompress 2>&1 | grep $algo > /dev/null
	[ $? -ne 0 ] && continue

	for tf in `cat files.lst`
	do
		echo "Preparing ${algo} compressed ${tf} datafile ..."
		cmd="../../pcompress -c ${algo} -l5 -s500k ${tf}"
		eval $cmd
		if [ $? -ne 0 ]
		then
			echo "FATAL: ${cmd} errored."
			exit 1
		fi
		mv ${tf}.pz ${tf}.${algo}
	done
done

for algo in lzfx lz4 zlib bzip2 lzma lzmaMt libbsc ppmd adapt adapt2
do
	../../pcompress 2>&1 | grep $algo > /dev/null
	[ $? -ne 0 ] && continue

	for level in 1 3 9 14
	do
		for tf in `cat files.lst`
		do
			for seg in 1m 100m
			do
				[ $level -lt 14 -a "$seg" = "100m" ] && continue

				cmd="../../pcompress -c ${algo} -l ${level} -s ${seg} ${tf}.${algo}"
				echo "Running $cmd"
				eval $cmd
				if [ $? -ne 0 ]
				then
					echo "${cmd} errored."
					exit 1
				fi
				cmd="../../pcompress -d ${tf}.${algo}.pz ${tf}.${algo}.1"
				echo "Running $cmd"
				eval $cmd
				if [ $? -ne 0 ]
				then
					echo "FATAL: Decompression failed."
					exit 1
				fi
				diff ${tf}.${algo} ${tf}.${algo}.1 > /dev/null
				if [ $? -ne 0 ]
				then
					echo "FATAL: Decompression was not correct"
					exit 1
				fi
				rm -f ${tf}.${algo}.pz ${tf}.${algo}.1
			done
		done
	done
done

clean
echo "#################################################"
echo ""

