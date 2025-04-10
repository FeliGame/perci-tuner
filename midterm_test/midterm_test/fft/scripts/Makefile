CC = gcc
CFLAGS = -lm
SOURCES = fft.c
EXECUTABLE = fft

all: $(SOURCES) $(EXECUTABLE)

$(EXECUTABLE): $(SOURCES)
	$(CC) -o $@ $^ $(CFLAGS)

clean:
	rm -f $(EXECUTABLE) *.txt *.out
