C=gcc
WARNINGS=-Wall -Wno-deprecated-declarations
TEST=
CFLAGS=-I. $(WARNINGS) $(TEST)
LDFLAGS=-lpthread

all: http_client http_server

%.o: %.c
	$(CC) -c -o $@ $< $(CFLAGS)

http_client: http_client.o utils.o
	$(CC) -o $@ $^ $(CFLAGS) $(LDFLAGS)

http_server: http_server.o  utils.o
	$(CC) -o $@ $^ $(CFLAGS) $(LDFLAGS)

clean:
	rm -f *.o http_client http_server

.PHONY: all clean