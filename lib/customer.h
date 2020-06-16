typedef struct customer {
    int id;
    int nproducts;
    int time;
    int timeq;
    int queuechecked;
    int queuedone;
    int exitok; 
}customer;


void setupcs(customer * cs, int i) {
    cs->id = (i+1);
    cs->nproducts=0;
    cs->time=0;
    cs->timeq=0;   
    cs->queuedone=0;
    cs->queuechecked=0;
    cs->exitok=0;
}


void printcs(customer cs) {
    printf("%d %d %d %d %d %d \n",cs.id,cs.nproducts,cs.time,cs.timeq,cs.queuechecked,cs.queuedone);
}