CC = gcc
CFLAGS = -lm
SOURCES = rocket_trajectory.c
EXECUTABLE = rocket_trajectory

all: $(SOURCES) $(EXECUTABLE)

$(EXECUTABLE): $(SOURCES)
	$(CC) -o $@ $^ $(CFLAGS)

clean:
	rm -f $(EXECUTABLE) *.txt *.out
