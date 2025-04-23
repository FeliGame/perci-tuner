CC = gcc
CFLAGS = -lm
SOURCES = runge_kutta_4.c
EXECUTABLE = runge_kutta_4

all: $(SOURCES) $(EXECUTABLE)

$(EXECUTABLE): $(SOURCES)
	$(CC) -o $@ $^ $(CFLAGS)

clean:
	rm -f $(EXECUTABLE) *.txt *.out
