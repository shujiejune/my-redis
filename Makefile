# 1. Compiler and Flags
CC = gcc
CFLAGS = -Wall -Wextra -O2
# -Wall -Wextra: specific warning flags to catch common bugs
# -O2: Optimization level 2 (standard for production)

# 2. File Paths
TARGET = server
SRC = src/server.c

# 3. Default Rule (what happens when you type 'make')
all: $(TARGET)

# 4. Build Rule: How to turn .c into an executable
$(TARGET): $(SRC)
	$(CC) $(CFLAGS) -o $(TARGET) $(SRC)

# 5. Run Rule: Compile (if needed) and then execute
run: $(TARGET)
	./$(TARGET)

# 6. Clean Rule: Remove the executable to start fresh
clean:
	rm -f $(TARGET)
