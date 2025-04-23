CC = gcc
CFLAGS = -lm
SOURCES = trapeze.c
EXECUTABLE = trapeze

all: $(SOURCES) $(EXECUTABLE)

$(EXECUTABLE): $(SOURCES)
	$(CC) -o $@ $^ $(CFLAGS)

clean:
	rm -f $(EXECUTABLE) *.txt *.out
