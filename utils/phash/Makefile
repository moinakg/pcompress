CFLAGS = -O

.cc.o:
	gcc $(CFLAGS) -c $<

O = lookupa.o recycle.o perfhex.o perfect.o

all : $(O)
	gcc -o perfect $(O) -lm
	sh genhash.sh

clean:
	rm -f perfect foo *.o

# DEPENDENCIES

lookupa.o : lookupa.c standard.h lookupa.h

recycle.o : recycle.c standard.h recycle.h

perfhex.o : perfhex.c standard.h lookupa.h recycle.h perfect.h

perfect.o : perfect.c standard.h lookupa.h recycle.h perfect.h
