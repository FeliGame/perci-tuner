TOOLSSRC=$(wildcard *.c)
TOOLSOBJ=$(patsubst %.c,%.o,$(TOOLSSRC))
TOOLSEXE=$(patsubst %.o,%.e,$(TOOLSOBJ))
TOOLSEXE2=$(patsubst %.e,%,$(TOOLSEXE))

#TOOLSLIB=libtools.a
#STOOLSLIB=libtools.so
CFLAGS=-fPIC -I.. 

all: $(TOOLSEXE) #$(STOOLSLIB) $(TOOLSLIB)
	@for file in $(TOOLSEXE); do mv "$$file" "$${file%.e}"; done

#$(TOOLSLIB): $(TOOLSOBJ)
#	ar -ru $@ $^
#
#$(STOOLSLIB): $(TOOLSSRC)
#	gcc -shared -fPIC -I.. $^ -o $@

%.e: %.o
	gcc $< ../libddreal.a -lm -o $@

%.o: %.c
	gcc -c $< ${CFLAGS} -o $@

clean:
	rm -rf $(TOOLSEXE2) $(TOOLSEXE) $(TOOLSOBJ) 
	@#rm -rf $(TOOLSEXE) $(TOOLSOBJ) $(STOOLSLIB) $(TOOLSLIB)

.PHONY: all clean
