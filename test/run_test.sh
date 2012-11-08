#!/bin/sh

tst=$1

if [ ! -d datafiles ]
then
	mkdir datafiles
else
	rm -f datafiles/.pco*
	rm -f datafiles/*.pz
	rm -f datafiles/*.1
fi
PDIR=`pwd`

[ ! -f datafiles/bin.dat ] && (tar cpf - /usr/bin | dd of=datafiles/bin.dat bs=1024 count=5120; cat res/jpg/*.jpg >> datafiles/bin.dat)
[ ! -f datafiles/share.dat ] && tar cpf - /usr/share | dd of=datafiles/share.dat bs=1024 count=5120
[ ! -f datafiles/inc.dat ] && (tar cpf - /usr/include | dd of=datafiles/inc.dat bs=1024 count=5120; cat res/xml/*.xml >> datafiles/inc.dat)
[ ! -f datafiles/combined.dat ] && cat datafiles/bin.dat datafiles/share.dat datafiles/inc.dat >> datafiles/combined.dat
[ ! -f datafiles/comb_d.dat ] && sh -c "cat datafiles/combined.dat > datafiles/comb_d.dat; cat datafiles/combined.dat >> datafiles/comb_d.dat"


failures=0
if [ "x$tst" = "x" ]
then
	for tf in *
	do
		echo "$tf" | grep "tst" > /dev/null
		[ $? -ne 0 ] && continue

		cd datafiles
		(. ../${tf})
		if [ $? -ne 0 ]
		then
			echo "FATAL: Test ${tf} failed"
			failures=$((failures + 1))
		fi
		cd $PDIR
	done
else
	tf="t${tst}.tst"
	if [ -f $tf ]
	then
		cd datafiles
		(. ../${tf})
		if [ $? -ne 0 ]
		then
			echo "FATAL: Test ${tf} failed"
			failures=$((failures + 1))
		fi
		cd $PDIR
	else
		echo "No such test $tst"
		exit 1
	fi
fi

if [ $failures -gt 0 ]
then
	echo "$failures tests Failed!"
	exit 1
else
	echo "All tests PASSED"
fi

