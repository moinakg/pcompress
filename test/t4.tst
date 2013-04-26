#
# Dedupe, Delta et al.
#
echo "#################################################"
echo "# Test Deduplication, Delta Encoding and LZP"
echo "#################################################"

for algo in lzfx lz4 adapt
do
	for tf in `cat files.lst`
	do
		rm -f ${tf}.*
		for feat in "-D" "-D -B3 -L" "-D -B4 -E" "-D -B2 -EE" "-D -B5 -EE -L" "-D -B2 -r" "-P" "-D -P" "-D -L -P" \
				"-G -D" "-G -F" "-G -L -P" "-G -B2"
		do
			for seg in 2m 100m
			do
				cmd="../../pcompress -c ${algo} -l 3 -s ${seg} $feat ${tf}"
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

echo "#################################################"
echo ""

