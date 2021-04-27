CC=gcc
CFLAGS=-g
BINS=server
OBJS=server.o myqueue.o

all: $(BINS)

server: $(OBJS)
	$(CC) $(CFLAGS) -c -o $@ $^

%.o: %.c
	$(CC) $(CFLAGS) -c -o $@ $^

clean:s
	rm -rf *.dSYM $(BINS)
