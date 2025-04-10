CC = gcc
CFLAGS = -lm
SOURCES = hartman.c
EXECUTABLE = hartman

all: $(SOURCES) $(EXECUTABLE)

$(EXECUTABLE): $(SOURCES)
	$(CC) -o $@ $^ $(CFLAGS)

clean:
	rm -f $(EXECUTABLE) *.txt *.out
