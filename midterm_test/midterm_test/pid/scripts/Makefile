CC = gcc
CFLAGS = -lm
SOURCES = pid.c
EXECUTABLE = pid

all: $(SOURCES) $(EXECUTABLE)

$(EXECUTABLE): $(SOURCES)
	$(CC) -o $@ $^ $(CFLAGS)

clean:
	rm -f $(EXECUTABLE) *.txt *.out
