CC		:= gcc
C_FLAGS := -Wall -lpthread #-g  -Wextra -pedantic -std=gnu99

BIN		:= bin
SRC		:= src
INCLUDE	:= include
LIB		:= include

#LIBRARIES	:= -lFunzioni

SOURCES := $(wildcard $(SRC)/*.c)
OBJECTS := $(patsubst $(SRC)/%.c, $(BIN)/%.o, $(SOURCES))


all: $(OBJECTS)

$(BIN)/%.o: $(SRC)/%.c
	$(CC) $(C_FLAGS) -I$(INCLUDE) -L$(LIB) -I$(LIB) $^ -o $@ 

clear:
	-rm $(BIN)/*
	-rm -f ./supermarket ./statsfile.log supermarket.PID

test:
	(./bin/supermarket.o & echo $$! > supermarket.PID) &
	sleep 25s; \
	kill -1 $$(cat supermarket.PID); \
	chmod +x ./analisi.sh 
	./analisi.sh $$(cat supermarket.PID); \
