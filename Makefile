CC		=  gcc
CFLAGS  = -lpthread -Wall   

INCDIR		= ./include
TESTDIR 	= ./testfile
DEBUG 		= -DDEBUG

TESTFILE = test.txt

.PHONY: test1 clean

all: ./supermarket

./supermarket: ./supermarket.c
	$(CC) ./supermarket.c $(CFLAGS) -o $@ 

test:
	(./supermarket testfile/config.txt & echo $$! > supermarket.PID) &
	sleep 25s; \
	kill -1 $$(cat supermarket.PID); \
	chmod +x ./analisi.sh 
	./analisi.sh $$(cat supermarket.PID); \
	
clean:
	rm -f ./supermarket ./statsfile.log supermarket.PID

