PROG_NAME = httpserver
CC = gcc
SOURCES = **.c
OBJECTS = $(patsubst %.c, %.o, $(SOURCES))
LIBS = -pthread
CSTD = c99
DEBUG_FLAGS = -std=$(CSTD) -Wall -Wextra -O0 -g3 -ggdb3 -DDEBUG
RELEASE_FLAGS = -std=$(CSTD) -O3

.PHONY: release clean

debug: $(OBJECTS)
	$(CC) $(OBJECTS) -o $(PROG_NAME) $(DEBUG_FLAGS) $(LIBS)


%.o: %.c
	$(CC) $(DEBUG_FLAGS) -c $^ $(LIBS)


release:
	make clean
	$(CC) $(SOURCES) -o $(PROG_NAME) $(RELEASE_FLAGS) $(LIBS)


clean:
	rm -f $(OBJECTS)
