CC = gcc
CFLAGS = -lm
SOURCES = n_body_simulation.c
EXECUTABLE = n_body_simulation

all: $(SOURCES) $(EXECUTABLE)

$(EXECUTABLE): $(SOURCES)
	$(CC) -o $@ $^ $(CFLAGS)

clean:
	rm -f $(EXECUTABLE) *.txt *.out
