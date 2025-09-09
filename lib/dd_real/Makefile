#CC=$(COMP_PRE)/clang
CC=/home/guocheng2/MxPPE/MxPCC/bin/clang
SRC=$(wildcard *.c)
#SRC=ldouble.c
OBJ=$(patsubst %.c,%.o,$(SRC))
BC=$(patsubst %.c,%.bc,$(SRC))
LIB=libautop.a
SLIB=libautop.so
LBC=dd.bc
LL=dd.ll
CFLAGS=-fPIC -I.

all: $(SLIB) $(LIB)
#all: $(SLIB) $(LIB) $(LL)

#$(LL): $(LBC)
#	$(COMP_PRE)/llvm-dis $^ -o $@
#
#$(LBC): $(BC)
#	$(COMP_PRE)/llvm-link $^ -o $@

#%.bc: %.c
#	$(CC) -o0 -emit-llvm -fno-discard-value-names -fPIE -c $< -o $@

$(LIB): $(OBJ)
	ar -ru $@ $^

$(SLIB): $(SRC)
	$(CC) -mcpu=ft2000 -shared -fPIC $^ -o $@

%.o: %.c
	$(CC) -mcpu=ft2000 -g -O2 -c $< ${CFLAGS} -o $@
	@#gcc -std=c99 -c $< ${CFLAGS} -o $@

clean:
	rm -rf $(SLIB) $(LIB) $(OBJ) $(LL) $(LBC) $(BC) 

.PHONY: all clean

