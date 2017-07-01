GCC=bin/gcc

all: clean simulfs

simulfs:
	gcc -std=c11 -fno-stack-protector *.c -o simulfs

clean:
	rm -f *.o
	rm -f simulfs
	rm -f simul.fs
	rm -f log.dat
	rm -f sector_map.png