#
# Fixed block dedupe
#
echo "#################################################"
echo "# Test Fixed block Deduplication"
echo "#################################################"

rm -f *.pz
rm -f *.1

for algo in lzfx lz4 adapt adapt2
do
	for tf in `cat files.lst`
	do
		for feat in "-F" "-F -B3 -L" "-F -B4" "-F -B5 -L" "-F -P" "-F -L -P" "-G -F -B3 -L"
		do
			for seg in 2m 100m
			do
				cmd="../../pcompress -c ${algo} -l 3 -s ${seg} $feat ${tf}"
				echo "Running $cmd"
				eval $cmd
				if [ $? -ne 0 ]
				then
					echo "FATAL: Compression errored."
					rm -f ${tf}.pz ${tf}.1
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

