CC = gcc
CFLAGS = -lm
SOURCES = odometry.c
EXECUTABLE = odometry

all: $(SOURCES) $(EXECUTABLE)

$(EXECUTABLE): $(SOURCES)
	$(CC) -o $@ $^ $(CFLAGS)

clean:
	rm -f $(EXECUTABLE) *.txt *.out
