#
# Test crypto
#
echo "#################################################"
echo "# Pipe mode Crypto tests"
echo "#################################################"

rm -f *.pz
rm -f *.1

for algo in lzfx adapt2
do
	for tf in `cat files.lst`
	do
		for feat in "-e SALSA20" "-e AES -L" "-D -e SALSA20" "-D -EE -L -e AES" "-e SALSA20 -S CRC64" "-e SALSA20 -L" "-e AES -E"
		do
			for seg in 2m 5m
			do
				echo "sillypassword" > /tmp/pwf
				cmd="cat ${tf} | ../../pcompress -c${algo} -p -l3 -s${seg} $feat -w /tmp/pwf > ${tf}.pz"
				echo "Running $cmd"
				eval $cmd
				if [ $? -ne 0 ]
				then
					echo "FATAL: Compression errored."
					rm -f ${tf}.pz ${tf}.1
					continue
				fi

				pw=`cat /tmp/pwf`
				if [ "$pw" = "sillypassword" ]
				then
					echo "FATAL: Password file /tmp/pwf not zeroed!"
				fi

				echo "sillypassword" > /tmp/pwf
				cmd="../../pcompress -d -w /tmp/pwf ${tf}.pz ${tf}.1"
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

				pw=`cat /tmp/pwf`
				if [ "$pw" = "sillypassword" ]
				then
					echo "FATAL: Password file /tmp/pwf not zeroed!"
				fi
				rm -f ${tf}.pz ${tf}.1
			done
		done
	done
done

for algo in lzfx zlib
do
	for tf in `cat files.lst`
	do
		for feat in "-e SALSA20" "-e AES -L" "-D -e SALSA20" "-D -EE -L -e SALSA20 -S KECCAK256" "-G -e SALSA20" "-G -F -e AES"
		do
			for seg in 5m
			do
				echo "sillypassword" > /tmp/pwf
				cmd="../../pcompress -c${algo} -l3 -s${seg} $feat -w /tmp/pwf ${tf} - > ${tf}.pz"
				echo "Running $cmd"
				eval $cmd
				if [ $? -ne 0 ]
				then
					echo "FATAL: Compression errored."
					rm -f ${tf}.pz ${tf}.1
					continue
				fi

				pw=`cat /tmp/pwf`
				if [ "$pw" = "sillypassword" ]
				then
					echo "FATAL: Password file /tmp/pwf not zeroed!"
				fi

				echo "sillypassword" > /tmp/pwf
				cmd="cat ${tf}.pz | ../../pcompress -d -w /tmp/pwf - ${tf}.1"
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

				pw=`cat /tmp/pwf`
				if [ "$pw" = "sillypassword" ]
				then
					echo "FATAL: Password file /tmp/pwf not zeroed!"
				fi
				rm -f ${tf}.pz ${tf}.1
			done
		done
	done
done

rm -f /tmp/pwf

echo "#################################################"
echo ""

