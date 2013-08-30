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
		for feat in "-D" "-D -B3 -L" "-D -B4 -E" "-D -B0 -EE" "-D -B5 -EE -L" "-D -B2 -r" "-P" "-D -P" "-D -L -P" \
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

#
# Test Segmented Global Dedupe
#

echo "#################################################"
echo "# Test Segmented Global Deduplication"
echo "#################################################"

#
# Select a large file from the list
#
tstf=
tsz=0
for tf in `cat files.lst`
do
	sz=`ls -l ${tf} | awk '{ print $5 }'`
	if [ $sz -gt $tsz ]
	then
		tsz=$sz
		tstf="$tf"
	fi
done

#
# Compute minimum index memory needed for segmented dedupe
# sizeof (hash_entry_t) = 20 + sizeof (CRC64) = 28 
# Each hashtable slot ptr = 8bytes
# Total: 28 + 8 = 36
# Segment size = 833848
# 25 Similarity indicators per segment, each needing one hash_entry_t
#
nsegs=$((tsz / 833848 + 1))
nmem=$((nsegs * 25 * 36))
mem_mb=$((nmem / 1048576 + 1))

#
# Now run Global Dedupe with index memory set to force segmented dedupe mechanism
#
export PCOMPRESS_INDEX_MEM=${mem_mb}
cmd="../../pcompress -G -c lz4 -l1 -P -s50m $tstf"
echo "Running $cmd"
eval $cmd
if [ $? -ne 0 ]
then
	echo "FATAL: Compression errored."
	rm -f ${tstf}.pz
	exit
fi
cmd="../../pcompress -d ${tstf}.pz ${tstf}.1"
echo "Running $cmd"
eval $cmd
if [ $? -ne 0 ]
then
	echo "FATAL: Decompression errored."
	rm -f ${tstf}.pz ${tstf}.1
	exit
fi
diff ${tstf} ${tstf}.1 > /dev/null
if [ $? -ne 0 ]
then
	echo "FATAL: Decompression was not correct"
fi
rm -f ${tstf}.pz ${tstf}.1

echo "#################################################"
echo ""

