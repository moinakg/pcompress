#
# Out of range parameters
#
echo "####################################################"
echo "# Test out of range parameters and error conditions."
echo "####################################################"

for feat in "-L" "-L -D" "-L -D -E" "-L -B5" "-L -D -E -B2" "-F" "-F -L"
do
	cmd="../../pcompress -c dummy -l4 -s1m $feat combined.dat"
	echo "Running $cmd"
	eval $cmd
	if [ $? -eq 0 ]
	then
		echo "FATAL: Compression DID NOT ERROR where expected"
		exit 1
	fi
done

for feat in "-B8 -s2m -l1" "-B0 -s2m -l1" "-D -s10k -l1" "-D -F -s2m -l1" "-p -e -s2m -l1" "-s2m -l15"
do
	for algo in lzfx lz4 zlib bzip2 libbsc ppmd lzma
	do
		cmd="../../pcompress -c lzfx $feat combined.dat"
		echo "Running $cmd"
		eval $cmd
		if [ $? -eq 0 ]
		then
			echo "FATAL: Compression DID NOT ERROR where expected"
			rm -f combined.dat.pz
			exit 1
		fi
	done
done

for feat in "-S CRC64" "-S SKEIN256" "-S SKEIN512" "-S SHA256" "-S SHA512" "-S KECCAK256" "-S KECCAK512"
do
	rm -f combined.dat.1.pz
	rm -f combined.dat.pz
	rm -f combined.dat.1

	cmd="../../pcompress -c lzfx -l3 -s1m $feat combined.dat"
	echo "Running $cmd"
	eval $cmd
	if [ $? -ne 0 ]
	then
		echo "FATAL: Compression errored."
		rm -f combined.dat.pz
		exit 1
	fi

	echo "Corrupting file header ..."
	dd if=/dev/urandom conv=notrunc of=combined.dat.pz bs=4 seek=1 count=1
	cmd="../../pcompress -d combined.dat.pz combined.dat.1"
	eval $cmd
	if [ $? -eq 0 ]
	then
		echo "FATAL: Decompression DID NOT ERROR where expected."
		rm -f combined.dat.pz
		rm -f combined.dat.1
		exit 1
	fi

	rm -f combined.dat.pz
	rm -f combined.dat.1

	cmd="../../pcompress -c zlib -l3 -s1m $feat combined.dat"
	echo "Running $cmd"
	eval $cmd
	if [ $? -ne 0 ]
	then
		echo "FATAL: Compression errored."
		rm -f combined.dat.pz
		exit 1
	fi

	cp combined.dat.pz combined.dat.1.pz
	echo "Corrupting file ..."
	dd if=/dev/urandom conv=notrunc of=combined.dat.pz bs=4 seek=100 count=1
	cmd="../../pcompress -d combined.dat.pz combined.dat.1"
	eval $cmd
	if [ $? -eq 0 ]
	then
		echo "FATAL: Decompression DID NOT ERROR where expected."
		rm -f combined.dat.pz
		rm -f combined.dat.1
		rm -f combined.dat.1.pz
		exit 1
	fi

	rm -f combined.dat.1
	cp combined.dat.1.pz combined.dat.pz
	echo "Corrupting file ..."
	dd if=/dev/urandom conv=notrunc of=combined.dat.1.pz bs=4 seek=51 count=1
	cmd="../../pcompress -d combined.dat.1.pz combined.dat.1"
	eval $cmd
	if [ $? -eq 0 ]
	then
		echo "FATAL: Decompression DID NOT ERROR where expected."
		rm -f combined.dat.pz
		rm -f combined.dat.1
		rm -f combined.dat.1.pz
		exit 1
	fi

	rm -f combined.dat.1 combined.dat.1.pz combined.dat.pz
	echo "plainpass" > /tmp/pwf
	cmd="../../pcompress -c zlib -l3 -s1m -e -w /tmp/pwf $feat combined.dat"
	echo "Running $cmd"
	eval $cmd
	if [ $? -ne 0 ]
	then
		echo "FATAL: Compression errored."
		rm -f combined.dat.pz
		exit 1
	fi
	pw=`cat /tmp/pwf`
	if [ "$pw" = "plainpasswd" ]
	then
		echo "FATAL: Password file was not zeroed"
		rm -f /tmp/pwf combined.dat.pz
		exit 1
	fi

	cp combined.dat.pz combined.dat.1.pz
	echo "Corrupting file ..."
	dd if=/dev/urandom conv=notrunc of=combined.dat.pz bs=4 seek=115 count=1
	echo "plainpass" > /tmp/pwf
	cmd="../../pcompress -d -w /tmp/pwf combined.dat.pz combined.dat.1"
	eval $cmd
	if [ $? -eq 0 ]
	then
		echo "FATAL: Decompression DID NOT ERROR where expected."
		rm -f combined.dat.pz
		rm -f combined.dat.1
		rm -f combined.dat.1.pz
		exit 1
	fi

	cp combined.dat.1.pz combined.dat.pz
	rm -f combined.dat.1
	echo "Corrupting file header ..."
	dd if=/dev/urandom conv=notrunc of=combined.dat.pz bs=4 seek=10 count=1
	echo "plainpass" > /tmp/pwf
	cmd="../../pcompress -d -w /tmp/pwf combined.dat.pz combined.dat.1"
	eval $cmd
	if [ $? -eq 0 ]
	then
		echo "FATAL: Decompression DID NOT ERROR where expected."
		rm -f combined.dat.pz
		rm -f combined.dat.1
		rm -f combined.dat.1.pz
		exit 1
	fi
done

rm -f combined.dat.1.pz
rm -f combined.dat.pz
rm -f combined.dat.1
rm -f /tmp/pwf

echo "#################################################"
echo ""

