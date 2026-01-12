# 1. Compiler and Flags
CC = gcc
CFLAGS = -Wall -Wextra -O2
# -Wall -Wextra: specific warning flags to catch common bugs
# -O2: Optimization level 2 (standard for production)

# 2. File Paths
SERVER_TARGET = server
SERVER_SRC = src/server.c
CLIENT_TARGET = client
CLIENT_SRC = src/client.c

# 3. Default Rule (what happens when you type 'make')
all: $(SERVER_TARGET) $(CLIENT_TARGET)

# 4. Build Rule: How to turn .c into an executable
$(SERVER_TARGET): $(SERVER_SRC)
	$(CC) $(CFLAGS) -o $(SERVER_TARGET) $(SERVER_SRC)
$(CLIENT_TARGET): $(CLIENT_SRC)
	$(CC) $(CFLAGS) -o $(CLIENT_TARGET) $(CLIENT_SRC)

# 5. Run Rule: Compile server (if needed) and then execute
run: $(SERVER_TARGET)
	./$(SERVER_TARGET)

# 6. Test
test: $(SERVER_TARGET) $(CLIENT_TARGET)
	@echo "--- Starting Server ---"
	./$(SERVER_TARGET) & PID=$$!; \
	sleep 0.5; \
	echo "--- Running Client ---"; \
	./$(CLIENT_TARGET); \
	echo "--- Stopping Server ---"; \
	kill $$PID

# 7. Clean Rule: Remove the executable to start fresh
clean:
	rm -f $(SERVER_TARGET) $(CLIENT_TARGET)
