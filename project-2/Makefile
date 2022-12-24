CC = gcc

all: sendfile recvfile

sendfile: sendfile.o util.o
	$(CC) sendfile.o util.o -o sendfile

recvfile: recvfile.o util.o
	$(CC) recvfile.c util.o -o recvfile

sendfile.o: sendfile.c
	$(CC) -c sendfile.c 

recvfile.o: recvfile.c
	$(CC) -c recvfile.c 

util.o: util.c
	$(CC) -c util.c 

clean:
	rm -f core.*
	rm -f sendfile recvfile
	rm -f *.o





