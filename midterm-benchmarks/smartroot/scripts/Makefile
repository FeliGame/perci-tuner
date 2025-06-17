CC = gcc
CFLAGS = -lm
SOURCES = smartroot.c
EXECUTABLE = smartroot

all: $(SOURCES) $(EXECUTABLE)

$(EXECUTABLE): $(SOURCES)
	$(CC) -o $@ $^ $(CFLAGS)

clean:
	rm -f $(EXECUTABLE) *.txt *.out
