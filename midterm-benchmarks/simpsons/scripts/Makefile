CC = gcc
CFLAGS = -lm
SOURCES = simpsons.c
EXECUTABLE = simpsons

all: $(SOURCES) $(EXECUTABLE)

$(EXECUTABLE): $(SOURCES)
	$(CC) -o $@ $^ $(CFLAGS)

clean:
	rm -f $(EXECUTABLE) *.txt *.out
