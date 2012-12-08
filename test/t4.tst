#
# Dedupe, Delta et al.
#
echo "#################################################"
echo "# Test Deduplication, Delta Encoding and LZP"
echo "#################################################"

for algo in lzfx lz4 adapt
do
	for tf in combined.dat comb_d.dat
	do
		for feat in "-D" "-D -B3 -L" "-D -B4 -E" "-D -B2 -EE" "-D -B5 -EE -L" "-D -B2 -r" "-P" "-D -P" "-D -E -P"
		do
			for seg in 2m 100m
			do
				cmd="../../pcompress -c ${algo} -l 3 -s ${seg} $feat ${tf}"
				echo "Running $cmd"
				eval $cmd
				if [ $? -ne 0 ]
				then
					echo "${cmd} errored."
					exit 1
				fi
				cmd="../../pcompress -d ${tf}.pz ${tf}.1"
				echo "Running $cmd"
				eval $cmd
				if [ $? -ne 0 ]
				then
					echo "${cmd} errored."
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

