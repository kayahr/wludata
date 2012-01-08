CFLAGS=-O2

all: wludata

wludata.o: wludata.c
	$(CC) $(CFLAGS) -c -o wludata.o wludata.c

wludata: wludata.o
	$(CC) $(CFLAGS) -o wludata wludata.o

clean:
	$(RM) wludata
	$(RM) *.o
