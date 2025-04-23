CC = gcc
CFLAGS = -lm
SOURCES = instantaneouscurrent.c
EXECUTABLE = instantaneouscurrent

all: $(SOURCES) $(EXECUTABLE)

$(EXECUTABLE): $(SOURCES)
	$(CC) -o $@ $^ $(CFLAGS)

clean:
	rm -f $(EXECUTABLE) *.txt *.out
