#
# Test all checksum mechanisms
#
echo "#################################################"
echo "# All checksums"
echo "#################################################"

for algo in zlib ppmd
do
	for tf in bin.dat share.dat inc.dat
	do
		for cksum in CRC64 SHA256 SHA512 SKEIN256 SKEIN512
		do
			cmd="../../pcompress -c ${algo} -l 6 -s 1m -S ${cksum} ${tf}"
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

			diff ${tf} ${tf}.1 | grep -i differ
			if [ $? -eq 0 ]
			then
				echo "${cmd}: Decompression was not correct"
				exit 1
			fi
			rm -f ${tf}.pz ${tf}.1
		done
	done
done

echo "#################################################"
echo ""

