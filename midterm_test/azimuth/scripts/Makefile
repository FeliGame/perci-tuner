CC = gcc
CFLAGS = -lm
SOURCES = azimuth.c
EXECUTABLE = azimuth

all: $(SOURCES) $(EXECUTABLE)

$(EXECUTABLE): $(SOURCES)
	$(CC) -o $@ $^ $(CFLAGS)

clean:
	rm -f $(EXECUTABLE) *.txt *.out
