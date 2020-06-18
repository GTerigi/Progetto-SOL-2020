#include <stdio.h>
#include <stdlib.h>
#include <string.h>
typedef struct cliente_
{
    int id;
    int nproducts;
    int time;
    int timeq;
    int queuechecked;
    int queuedone;
    int exitok;
} Cliente;

void setupcs(Cliente *cs, int i)
{
    cs->id = (i + 1);
    cs->nproducts = 0;
    cs->time = 0;
    cs->timeq = 0;
    cs->queuedone = 0;
    cs->queuechecked = 0;
    cs->exitok = 0;
}

void printcs(Cliente cs)
{
    printf("%d %d %d %d %d %d \n", cs.id, cs.nproducts, cs.time, cs.timeq,
           cs.queuechecked, cs.queuedone);
}

typedef struct queue
{
    struct queuenode *head;
    int queueopen;
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
        return 1;
    }
    //printcs(**cs);
    while (curr->next != NULL)
        curr = curr->next;
    curr->next = q;
    return 1;
}

Cliente *removecustomer(queue **qs, int nqueue)
{
    queuenode *q = (*qs)->head;
    (*qs)->head = ((*qs)->head)->next;
    Cliente *tmp = q->cs;
    free(q);
    return tmp;
}

void resetQueue(queue **qs, int nqueue)
{

    while ((*qs)->head != NULL)
    {
        queuenode *q = (*qs)->head;
        (*qs)->head = ((*qs)->head)->next;
        free(q);
    }
}

int queuelength(queue *qs, int nqueue)
{
    queuenode *curr = qs->head;
    int counter = 0;
    while (curr != NULL)
    {
        counter++;
        curr = curr->next;
    }
    return counter;
}

void printQueue(queue *qs, int id)
{
    queuenode *curr = qs->head;
    printf("QUEUE %d: ", id);
    fflush(stdout);
    while (curr != NULL)
    {
        printf("%d -> ", (*(curr->cs)).id);
        curr = curr->next;
    }
    printf("\n");
    fflush(stdout);
}
