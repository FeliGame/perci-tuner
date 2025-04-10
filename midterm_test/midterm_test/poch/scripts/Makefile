CC = gcc
CFLAGS = -I /usr/local/include/ -L /usr/local/lib -lgsl -lgslcblas -lm
TARGET = poch
SRC = poch.c

all: $(TARGET) 

$(TARGET): $(SRC)
	$(CC) $(SRC) -o $(TARGET) $(CFLAGS)

clean:
	rm -f $(TARGET) *.txt *.out
