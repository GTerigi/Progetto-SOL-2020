#if !defined(CODA_CLIENTI_H)
#define CODA_CLIENTI_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
typedef struct cliente_
{
    int IDcliente;
    int ProdComprati;
    long tempoInside;
    long tempoCoda;
    int nCodeScelte;
    int uscitaCoda;
    int possoUscire;
} Cliente;
typedef struct queuenode
{
    Cliente *cliente;
    struct queuenode *next;
} nodoCliente;

typedef struct queue
{
    nodoCliente *head;
    int aperta;
    int length;
} CodaClienti;

void initCliente(Cliente *cliente, int ID);

CodaClienti *InitCodaClienti();

int pushCoda(CodaClienti **coda, Cliente **argCliente);

Cliente *popCoda(CodaClienti **coda);

void destroyCoda(CodaClienti **coda);
#endif