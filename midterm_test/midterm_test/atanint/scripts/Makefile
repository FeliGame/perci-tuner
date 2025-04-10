CC = gcc
CFLAGS = -I /usr/local/include/ -lm
TARGET = atanint
SRC = atanint.c

all: $(TARGET) 

$(TARGET): $(SRC)
	$(CC) $(SRC) -o $(TARGET) $(CFLAGS)

clean:
	rm -f $(TARGET) *.txt *.out
