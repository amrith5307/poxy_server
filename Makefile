# Compiler
CC = gcc

# Compiler flags
CFLAGS = -g -Wall -pthread

# Target executable
TARGET = proxy

# Source files
SRCS = proxyserver.c proxy_parse.c

# Object files
OBJS = $(SRCS:.c=.o)

# Default target
all: $(TARGET)

# Rule to build the executable
$(TARGET): $(OBJS)
	$(CC) $(CFLAGS) -o $(TARGET) $(OBJS)

# Rule to build object files
%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

# Clean compiled files
clean:
	rm -f $(OBJS) $(TARGET)

# Optional: create tarball
tar:
	tar -cvzf proxy_project.tgz $(SRCS) proxy_parse.h Makefile
