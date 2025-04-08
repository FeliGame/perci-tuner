#CFLAGS=-O0 -ffp-contract=off -I/home/qhj/workspace/orther -I/home/qhj/sofeware/install/cpfloat/include
CFLAGS=
LDFLAGS=-lm

CFLAGS+=-O0 -ffp-contract=off
SRC=test.c
CC=clang
ifeq ($(FP128), 1)
CFLAGS+=-DFP128 -DUSE_MPFR
EXE=fp128_test
else
ifeq ($(D), 1)
CFLAGS+=-DD -DUSE_MPFR
EXE=fp64_test
endif
endif


all: $(EXE)


$(EXE):$(SRC)
	$(CC) $^ ${CFLAGS} ${LDFLAGS} -o $@


clean:
	rm -f fp64_test fp128_test

.PHONY: all clean
