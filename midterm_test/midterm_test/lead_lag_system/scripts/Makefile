CC = gcc
CFLAGS = -lm
SOURCES = lead_lag_system.c
EXECUTABLE = lead_lag_system

all: $(SOURCES) $(EXECUTABLE)

$(EXECUTABLE): $(SOURCES)
	$(CC) -o $@ $^ $(CFLAGS)

clean:
	rm -f $(EXECUTABLE) *.txt *.out
