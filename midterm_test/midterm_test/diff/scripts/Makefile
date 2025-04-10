CC = gcc
CFLAGS = -lm
SOURCES = diff.c
EXECUTABLE = diff

all: $(SOURCES) $(EXECUTABLE)

$(EXECUTABLE): $(SOURCES)
	$(CC) -o $@ $^ $(CFLAGS)

clean:
	rm -f $(EXECUTABLE) *.txt *.out
