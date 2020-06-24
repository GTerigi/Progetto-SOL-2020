#include "./CodaClienti.h"

void initCliente(Cliente *cliente, int ID) {
  cliente->IDcliente = (ID + 1);
  cliente->ProdComprati = 0;
  cliente->tempoInside = 0;
  cliente->tempoCoda = 0;
  cliente->uscitaCoda = 0;
  cliente->nCodeScelte = 0; // Contatore per il numero di code scelte
  cliente->possoUscire = 0;
}

CodaClienti *InitCodaClienti() {
  CodaClienti *nodo = malloc(sizeof(CodaClienti));
  nodo->head = NULL;
  nodo->aperta = 0;
  nodo->length = 0;
  return nodo;
}

int pushCoda(CodaClienti **coda, Cliente **argCliente) {
  nodoCliente *nodo;
  if ((nodo = malloc(sizeof(nodoCliente))) == NULL) {
    return -1;
  }
  nodo->cliente = (*argCliente);
  nodo->next = NULL;
  nodoCliente *curr = (*coda)->head;
  if ((*coda)->head == NULL) {
    (*coda)->head = nodo;
    (*coda)->length++;
    return 1;
  }
  while (curr->next != NULL)
    curr = curr->next;
  curr->next = nodo;
  (*coda)->length++;
  return 1;
}

Cliente *popCoda(CodaClienti **coda) {
  nodoCliente *nodo = (*coda)->head;
  (*coda)->head = ((*coda)->head)->next;
  Cliente *tmp = nodo->cliente;
  free(nodo);
  (*coda)->length--;
  return tmp;
}

void destroyCoda(CodaClienti **coda) {

  while ((*coda)->head != NULL) {
    nodoCliente *nodo = (*coda)->head;
    (*coda)->head = ((*coda)->head)->next;
    free(nodo);
    (*coda)->length--;
  }
}
