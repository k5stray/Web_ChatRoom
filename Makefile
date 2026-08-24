CC = g++

CFLAGS = -std=c++11 -Wall -g -pthread
LIBS = -lssl -lcrypto

SRC = $(wildcard *.cc)
SRC += $(wildcard net/*.cc)
SRC += $(wildcard util/*.cc)
SRC += $(wildcard web/*.cc)

TARGET = server

$(TARGET): $(SRC)
	$(CC) $(CFLAGS) $^ -o $@ $(LIBS)

clean:
	rm -f $(TARGET)
