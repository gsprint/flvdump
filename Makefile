all: flvdump

flvdump: flvdump.c
	gcc -O3 -Wall -o flvdump flvdump.c -lm

clean:
	rm -f flvdump
