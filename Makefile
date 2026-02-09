# 0. Compiler and Flags
CC = gcc
CFLAGS = -Wall -Wextra -O2 -Isrc
# -Wall -Wextra: specific warning flags to catch common bugs
# -O2: Optimization level 2 (standard for production)
# -Isrc: look for header files in src/

# List of targets to build by default
all: server client

# ----------------------------------------------------
# 1. Compile common and buffer code separately
#    -c means "Compile to object file (.o), don't link yet"
# ----------------------------------------------------
src/common.o: src/common.c
	$(CC) $(CFLAGS) -c src/common.c -o src/common.o

src/buffer.o: src/buffer.c
	$(CC) $(CFLAGS) -c src/buffer.c -o src/buffer.o

# ----------------------------------------------------
# 2. Build the Server
#    Depends on server.c AND common.o
# ----------------------------------------------------
server: src/server.c src/common.o src/buffer.o
	$(CC) $(CFLAGS) -o server src/server.c src/common.o src/buffer.o

# ----------------------------------------------------
# 3. Build the Client
#    Depends on client.c AND common.o
# ----------------------------------------------------
client: src/client.c src/common.o
	$(CC) $(CFLAGS) -o client src/client.c src/common.o

# ----------------------------------------------------
# 4. Utilities
# ----------------------------------------------------

# Run just the server
run: server
	./server

# Run the test script
test: server client
	@echo "--- Starting Server ---"
	./server & PID=$$!; \
	sleep 0.5; \
	echo "--- Running Client ---"; \
	./client; \
	echo "--- Stopping Server ---"; \
	kill $$PID

clean:
	rm -f server client src/*.o
	-pkill -f server
