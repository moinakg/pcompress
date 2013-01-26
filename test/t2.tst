#
# Test all checksum mechanisms
#
echo "#################################################"
echo "# All checksums"
echo "#################################################"

for algo in zlib ppmd
do
	for tf in `cat files.lst`
	do
		for cksum in CRC64 SHA256 SHA512 BLAKE256 BLAKE512 KECCAK256 KECCAK512
		do
			cmd="../../pcompress -c ${algo} -l 6 -s 1m -S ${cksum} ${tf}"
			echo "Running $cmd"
			eval $cmd
			if [ $? -ne 0 ]
			then
				echo "FATAL: Compression failed."
				rm -f ${tf}.pz
				continue
			fi
			cmd="../../pcompress -d ${tf}.pz ${tf}.1"
			echo "Running $cmd"
			eval $cmd
			if [ $? -ne 0 ]
			then
				echo "FATAL: Decompression failed."
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

echo "#################################################"
echo ""

