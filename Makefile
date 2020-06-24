CC		:= gcc
C_FLAGS := -Wall -lpthread #-g  -Wextra -pedantic -std=gnu99

BIN		:= bin
SRC		:= src
INCLUDE	:= lib
LIB		:= lib

LIBRARIES	:= -lFunzioni

SOURCES := $(wildcard $(SRC)/*.c)
OBJECTS := $(patsubst $(SRC)/%.c, $(BIN)/%.o, $(SOURCES))


all: $(OBJECTS)

$(BIN)/%.o: $(SRC)/%.c
	$(CC) $(C_FLAGS) -I$(INCLUDE) -L$(LIB) -I$(LIB) $^ -o $@ $(LIBRARIES)

clear:
	-rm $(BIN)/*
	-rm -f  ./statsfile.log supermarket.PID
	-clear

test:
	(./bin/supermarket.o & echo $$! > supermarket.PID) &
	sleep 10s; \
	kill -1 $$(cat supermarket.PID); \
	chmod +x ./analisi.sh 
	./analisi.sh $$(cat supermarket.PID); \

test2:
	(./bin/supermarket.o & echo $$! > supermarket.PID) &
	sleep 10s; \
	kill -3 $$(cat supermarket.PID); \
	chmod +x ./analisi.sh 
	./analisi.sh $$(cat supermarket.PID); \