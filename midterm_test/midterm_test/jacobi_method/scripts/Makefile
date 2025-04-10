CC = gcc
CFLAGS = -lm
SOURCES = jacobi_method.c
EXECUTABLE = jacobi_method

all: $(SOURCES) $(EXECUTABLE)

$(EXECUTABLE): $(SOURCES)
	$(CC) -o $@ $^ $(CFLAGS)

clean:
	rm -f $(EXECUTABLE) *.txt *.out
