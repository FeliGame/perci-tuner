CC = gcc
CFLAGS = -I /usr/local/include/ -lm
TARGET = airy_der
SRC = airy_der.c

all: $(TARGET) 

$(TARGET): $(SRC)
	$(CC) $(SRC) -o $(TARGET) $(CFLAGS)

clean:
	rm -f $(TARGET) *.txt *.out
