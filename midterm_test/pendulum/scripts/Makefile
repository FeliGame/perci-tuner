CC = gcc
CFLAGS = -lm
SOURCES = pendulum.c
EXECUTABLE = pendulum

all: $(SOURCES) $(EXECUTABLE)

$(EXECUTABLE): $(SOURCES)
	$(CC) -o $@ $^ $(CFLAGS)

clean:
	rm -f $(EXECUTABLE) *.txt *.out
