CC	 	= gcc
LD	 	= gcc
CFLAGS	= -Wall -g

all:	client  server

client: client.c
	$(CC) $(DEFS) $(CFLAGS) $(LIB) -o client client.c
server: server.c 
	$(CC) $(DEFS) $(CFLAGS) $(LIB) -o server server.c
clean:
	rm -f *.or
	rm -f *~
	rm -f core.*
	rm -f client
	rm -f server
