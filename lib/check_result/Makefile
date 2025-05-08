CFLAGS=-O2 -DGL

ifeq ($(USE_DD), 1)
CFLAGS+=-DDD -I/thfs1/home/penglin_652/qhj/perci-tuner-git-test/orther/orther
endif

all: lib

check_result.o : check_result.c
	clang ${CFLAGS} -c $< -o $@

lib: check_result.o
	clang -shared -fPIC check_result.c ${CFLAGS} -I. -o libcheck_result.so
	ar -ru libcheck_result.a check_result.o

clean:
	rm -f libcheck_result.a libcheck_result.so check_result.o
