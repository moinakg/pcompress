#
# Test crypto
#
echo "#################################################"
echo "# Crypto tests"
echo "#################################################"

for algo in lzfx adapt2
do
	for tf in `cat files.lst`
	do
		rm -f ${tf}.*
		for feat in "-e" "-e -L -S SHA256" "-D -e -S SHA512" "-D -EE -L -e -S SKEIN512" "-e -S CRC64" "-e -P" "-e -L -P -S KECCAK256" "-D -e -L -S KECCAK512"
		do
			for seg in 2m 100m
			do
				echo "sillypassword" > /tmp/pwf
				cmd="../../pcompress -c ${algo} -l 3 -s ${seg} $feat -w /tmp/pwf ${tf}"
				echo "Running $cmd"
				eval $cmd
				if [ $? -ne 0 ]
				then
					echo "FATAL: Compression errored."
					rm -f ${tf}.pz
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
					rm -f ${tf}.pz ${tf}.1
					continue
				fi

				pw=`cat /tmp/pwf`
				if [ "$pw" = "sillypassword" ]
				then
					echo "FATAL: Password file /tmp/pwf not zeroed!"
				fi

				#
				# Now try decompression with invalid password. It should
				# fail.
				#
				rm -f ${tf}.1
				cmd="../../pcompress -d -w /tmp/pwf ${tf}.pz ${tf}.1"
				echo "Running $cmd"
				eval $cmd
				if [ $? -eq 0 ]
				then
					echo "FATAL: Decompression did not fail where expected."
				fi
				rm -f ${tf}.pz ${tf}.1
			done
		done
	done
done

rm -f /tmp/pwf

echo "#################################################"
echo ""

