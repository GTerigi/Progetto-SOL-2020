CC		:= gcc
C_FLAGS := -Wall -lpthread  -std=gnu99 -g#  -Wextra -pedantic

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
	-rm -f  ./statsfile.log pid.PID
	-clear

test:
	(./bin/supermercato.o & echo $$! > pid.PID) &
	sleep 10s; \
	kill -1 $$(cat pid.PID); \
	chmod +x ./analisi.sh 
	./analisi.sh $$(cat pid.PID); \

test2:
	(./bin/supermercato.o & echo $$! > pid.PID) &
	sleep 10s; \
	kill -1 $$(cat pid.PID); \
	chmod +x ./analisi.sh 
	./analisi.sh $$(cat pid.PID); \
