#include <stdio.h>
#include <stdlib.h>
#include <string.h>
typedef struct cliente_
{
    int IDcliente;
    int ProdComprati;
    int tempoInside;
    int tempoCoda;
    int nCodeScelte;
    int uscitaCoda;
    int possoUscire;
} Cliente;

void setupcs(Cliente *cs, int i)
{
    cs->IDcliente = (i + 1);
    cs->ProdComprati = 0;
    cs->tempoInside = 0;
    cs->tempoCoda = 0;
    cs->uscitaCoda = 0;
    cs->nCodeScelte = 0; // Contatore per il numero di code scelte
    cs->possoUscire = 0;
}

void printcs(Cliente cs)
{
    printf("%d %d %d %d %d %d \n", cs.IDcliente, cs.ProdComprati, cs.tempoInside, cs.tempoCoda,
           cs.nCodeScelte, cs.uscitaCoda);
}

typedef struct queue
{
    struct queuenode *head;
    int queueopen;
    int length;
} queue;

typedef struct queuenode
{
    Cliente *cs;
    struct queuenode *next;
} queuenode;

void printQueue(queue *qs, int id);

queue *createqueues(int id)
{
    struct queue *q = malloc(sizeof(queue));
    q->head = NULL;
    q->queueopen = 0;
    q->length = 0;
    return q;
}

int joinqueue(queue **qs, Cliente **cs, int nqueue)
{
    queuenode *q;
    if ((q = malloc(sizeof(queuenode))) == NULL)
    {
        return -1;
    }
    q->cs = (*cs);
    q->next = NULL;
    queuenode *curr = (*qs)->head;
    if ((*qs)->head == NULL)
    {
        (*qs)->head = q;
        (*qs)->length++;
        return 1;
    }
    //printcs(**cs);
    while (curr->next != NULL)
        curr = curr->next;
    curr->next = q;
    (*qs)->length++;
    return 1;
}

Cliente *removecustomer(queue **qs, int nqueue)
{
    queuenode *q = (*qs)->head;
    (*qs)->head = ((*qs)->head)->next;
    Cliente *tmp = q->cs;
    free(q);
    (*qs)->length--;
    return tmp;
}

void resetQueue(queue **qs, int nqueue)
{

    while ((*qs)->head != NULL)
    {
        queuenode *q = (*qs)->head;
        (*qs)->head = ((*qs)->head)->next;
        free(q);
        (*qs)->length--;
    }
}

void printQueue(queue *qs, int id)
{
    queuenode *curr = qs->head;
    printf("QUEUE %d: ", id);
    fflush(stdout);
    while (curr != NULL)
    {
        printf("%d -> ", (*(curr->cs)).IDcliente);
        curr = curr->next;
    }
    printf("\n");
    fflush(stdout);
}
