CC = gcc
CFLAGS = -lm
SOURCES = arclength_of_a_wiggly_function.c
EXECUTABLE = arclength_of_a_wiggly_function

all: $(SOURCES) $(EXECUTABLE)

$(EXECUTABLE): $(SOURCES)
	$(CC) -o $@ $^ $(CFLAGS)

clean:
	rm -f $(EXECUTABLE) *.txt *.out