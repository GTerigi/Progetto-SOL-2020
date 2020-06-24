CC = gcc 
CFLAGS = -Wall 
LDFLAGS = -L ./ -I ./
.PHONY: clean

all: libFunzioni.a iniParser/ini.o CodaClienti/CodaClienti.o

libFunzioni.a :  iniParser/ini.o CodaClienti/CodaClienti.o
	ar rvs $@ $^
	-rm $^

CodaClienti/CodaClienti.o : CodaClienti/CodaClienti.c
	$(CC) $^ $(CGLAGS) $(LDFLAGS) -c -o $@
	
iniParser/ini.o : iniParser/ini.c
	$(CC) $^ $(CGLAGS) $(LDFLAGS) -c -o $@
