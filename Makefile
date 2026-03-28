CC = gcc
CFLAGS = -Wall -g

all: oss user

oss: oss.o
	$(CC) $(CFLAGS) -o oss oss.o

user: user.o
	$(CC) $(CFLAGS) -o user user.o

oss.o: oss.c common.h
	$(CC) $(CFLAGS) -c oss.c

user.o: user.c common.h
	$(CC) $(CFLAGS) -c user.c

clean:
	rm -f *.o oss user