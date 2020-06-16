#include <signal.h>
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <time.h>
#include "./include/customer.h"
#include "./include/queue.h"
#include <math.h>
#include <errno.h>

#define MAX_LEN 256

#ifndef DEBUG
#define DEBUG 0
#endif
#define DEBUG_PRINT(x) printf x



typedef struct supermarketcheckout {
    int id;
    int nproducts;
    int ncustomers;
    int time;
    float servicetime;
    int nclosure;
    long randomtime;
    int hasbeenopened;
}supermarketcheckout;

void setupsm(supermarketcheckout * sm, int i);
void printsm(supermarketcheckout sm);

typedef struct config {
    int K; //Number of supermarket checkouts
    int C; //Number of max customers in the supermarket
    int E; //Number of customers that need to exit before make enter other E customers
    int T; //Min time for customers to buy
    int P; //Number of max products for each customers
    int S; //Seconds to do a product by the cashiers
    int S1; //If the are S1 queues with <=1 customers -> Close one of them
    int S2; //If the is a queue with more then S2 customers in queue -> Open another supermarket checkouts
    int smopen; //How much sm are opened at the start of the program
    int directornews; //Update time used from sm to send queue length to director
}config;

int confcheck(config * cf);
config * test(const char* configfile);
void printconf(config configvalues);

//Queue control
static pthread_mutex_t * queuemutex; //Lock on queues
static pthread_cond_t * queuecond; //Condition variable on queues
static pthread_cond_t * smcond; //Used to wake up a cashier when there is some one in queue

//Queue closing control
static pthread_mutex_t * smexitmutex; //Mutex on the variable "smexit" to control the close of a queue (0=Director is not closing the queue, 1=Director is closing the queue)

//DirectorOK 
static pthread_mutex_t okmutex = PTHREAD_MUTEX_INITIALIZER; //Lock on queues
static pthread_cond_t okcond = PTHREAD_COND_INITIALIZER; //Condition variable on queues
static pthread_cond_t DirectorCond = PTHREAD_COND_INITIALIZER; //Director cond

//Used to syncronized the variable qslength for the queues length (Used by director to open/close the supermarket checkouts)
static pthread_mutex_t * qslengthmutex;
static pthread_cond_t * qslengthcond;

//Lock and condition variable to syncronize the update by the cashiers
static pthread_mutex_t updatelock = PTHREAD_MUTEX_INITIALIZER; //Lock on queues
static pthread_cond_t updatecond = PTHREAD_COND_INITIALIZER; //Condition variable on queues
static pthread_mutex_t * upvarlock;

//FILE MUTEX
static pthread_mutex_t filemutex = PTHREAD_MUTEX_INITIALIZER;

static config * cf; //Program configuration
static queue ** qs; //Queues
static queue * csexitq;
static int * qslength; //Array that contains queue's length
static int * smexit; //Array that contains if the queue is going to be closed or not
static int * updatevariable; //Used to update the director from cashier!
static int exitbroadcast=0; //Used when activecustomers are = 0 to send a signal to cashier in case of a SIGHUP
static long t; //TIME
static FILE* statsfile; //File containing stats of the execution

volatile sig_atomic_t sighup=0;
volatile sig_atomic_t sigquit=0;

void CreateQueueManagement();
void QueueFree();


static void handler (int signum) {

if (signum==1) sighup=1;
if (signum==3) sigquit=1;

printf("Signal Recived: %d\n",signum); fflush(stdout);
printf("Supermarket is closing!\n");
}


void * clockT(void *arg) {
    int id=(int) (intptr_t) arg;
    int exittime=0;
    while(1){
        pthread_mutex_lock(&smexitmutex[id]);
        if (smexit[id]==1) exittime=1;
        pthread_mutex_unlock(&smexitmutex[id]);
        if (exittime!=1) {
            struct timespec t={(cf->directornews/1000),((cf->directornews%1000)*1000000)}; //Wait cf->director ms to update the director
            nanosleep(&t,NULL); //Sleeping randomtime mseconds
        }
        pthread_mutex_lock(&smexitmutex[id]);
        if (smexit[id]==1) exittime=1;
        pthread_mutex_unlock(&smexitmutex[id]);
        if(DEBUG) { DEBUG_PRINT(("%d: EXITTIME %d\n",id,exittime)); fflush(stdout); }
        if (exittime!=1 && sigquit==0 && sighup==0 ) {  
            if(DEBUG) { DEBUG_PRINT(("%d UPDATINGGGGGGGGGGGGGGGGGGGGGGGGGGGG \n",id)); fflush(stdout); }
            pthread_mutex_lock(&qslengthmutex[id]);
            pthread_mutex_lock(&queuemutex[id]);
            qslength[id]=queuelength(qs[id],id); //Update queue length 
            if(DEBUG) DEBUG_PRINT(("QUEUE LENGTH %d = %d\n",id,qslength[id])); 
            fflush(stdout);
            pthread_mutex_unlock(&queuemutex[id]);
            pthread_mutex_unlock(&qslengthmutex[id]);

            pthread_mutex_lock(&updatelock);
            pthread_mutex_lock(&upvarlock[id]);
            updatevariable[id]=1; //To warn the director that the cashier "id" has updated the queue's length info
            pthread_mutex_unlock(&upvarlock[id]);
            
            pthread_mutex_lock(&smexitmutex[id]);
            if (smexit[id]==1) exittime=1;
            if (exittime!=1) pthread_cond_signal(&updatecond); //Wake up the director
            pthread_mutex_unlock(&smexitmutex[id]);
            
            pthread_mutex_unlock(&updatelock);
        }
        else { 
            if (DEBUG) { DEBUG_PRINT(("CLOCK %d CLOSED \n",id)); fflush(stdout); }
            if (sigquit==1 || sighup==1) {
                pthread_mutex_lock(&updatelock);
                pthread_cond_signal(&updatecond); //Wake up the thread that controls the sm checkouts to make it exit
                pthread_mutex_unlock(&updatelock);
            }
            pthread_exit(NULL); 
        }
    }
    
    return NULL;
}



void * customerT (void *arg) {

    struct timespec spec;
    struct timespec spec2;
    struct timespec spec3;
    struct timespec spec4;
    long time1,time2,time3=-1,time4=-1;

    clock_gettime(CLOCK_REALTIME, &spec); //Timestamp when customer enters the supermarket
    time1 = (spec.tv_sec)*1000 + (spec.tv_nsec) / 1000000;


    customer * cs = malloc(sizeof(customer)); //Customer data
    setupcs(cs,(int) (intptr_t) (arg)); //Set up struct
    int id = cs->id; //ID CUSTOMER
    unsigned int seed=cs->id+t; //Creating seed
    long randomtime; //Random time to buy products
    int nqueue; //Queue chosen
    int check; //If chosen a valid queue
    int changequeue=0; //If the queue has been closed and need to change queue
    int change=0; //Controls if the customer is switching supermarket checkout or not
    if (DEBUG) { DEBUG_PRINT(("Customer %d: joined the supermarket\n",id)); fflush(stdout); }

    //Customer's buy time
    cs->nproducts=rand_r(&seed)%(cf->P); //Random number of products: 0<nproducts<=P
    while ((randomtime = rand_r(&seed) % (cf->T))<10); //Random number of buy time
    struct timespec t={(randomtime/1000),((randomtime%1000)*1000000)};
    nanosleep(&t,NULL); //Sleeping randomtime mseconds
    if ((cs->nproducts)!=0){
        do{
            changequeue=0; //Reset change queue value
            check=0; 
            do {
                nqueue=rand_r(&seed) % (cf->K); //Random queue number
                pthread_mutex_lock(&queuemutex[nqueue]);
                if (qs[nqueue]->queueopen!=0) check=1; //Check if the queue is open
                pthread_mutex_unlock(&queuemutex[nqueue]);
                if(sigquit==1) break;
            }while(check==0);
            if (sigquit!=1){
                pthread_mutex_lock(&queuemutex[nqueue]);
                if (change==0) {if(DEBUG) { DEBUG_PRINT(("Customer %d: Entra in cassa --> %d\n",id,nqueue)); } cs->queuechecked=1; }//First time that the cs is in a queue
                else { if(DEBUG) DEBUG_PRINT(("Customer %d: CAMBIA E VA NELLA CASSAAAAAAAAAAAAAAAAAAAAAAAA --> %d\n",id,nqueue)); cs->queuechecked++;}//Not the first time that the cs is in a queue
                fflush(stdout);

                clock_gettime(CLOCK_REALTIME, &spec3); //timestamp for calculating time waited in queue
                time3 = (spec3.tv_sec)*1000 + (spec3.tv_nsec) / 1000000;
                if((joinqueue(&qs[nqueue],&cs,nqueue))==-1){ //Joining the queue chosen
                    fprintf(stderr, "malloc failed\n");
                    exit(EXIT_FAILURE); 
                }
                if(DEBUG) { DEBUG_PRINT(("ID %d: JOINING THE nqueue: %d\n", id,nqueue)); fflush(stdout); }
                pthread_cond_signal(&smcond[nqueue]); //Signal to the queue to warn that a new customer is in queue
                while ((cs->queuedone)==0 && changequeue==0){ //While the customer hasnt paid or needs to change queue 'cause it has been closed
                    pthread_cond_wait(&queuecond[nqueue],&queuemutex[nqueue]);
                    if (qs[nqueue]->queueopen==0) changequeue=1; //If the customer has been waken and he hasn't done the queue --> sm closed and changes the queue
                }

                //DA IMPLEMENTARE: SE SIGQUIT SETTATO -> THREAD EXIT 
                pthread_mutex_unlock(&queuemutex[nqueue]);
                change++;
            }
            else break;
        }while(cs->queuedone==0);   
        if (time3!=-1)clock_gettime(CLOCK_REALTIME, &spec4); //Get timestamp when customer has paid
        time4 = (spec4.tv_sec)*1000 + (spec4.tv_nsec) / 1000000;
    }
    
    pthread_mutex_lock(&okmutex);
    if((joinqueue(&csexitq,&cs,-1))==-1){
        fprintf(stderr,"malloc failed\n");
    }
    pthread_cond_signal(&DirectorCond); //Signal to the director that someone wants to exit
    while(cs->exitok!=1) { pthread_cond_wait(&okcond,&okmutex);} //While director doesnt allow the exit --> wait
    pthread_mutex_unlock(&okmutex);

    if(DEBUG) { DEBUG_PRINT(("Customer %d: leaved the supermarket\n",id)); fflush(stdout); }

    if (cs->queuedone!=1) cs->nproducts=0;

    clock_gettime(CLOCK_REALTIME, &spec2); //Timestamp of the exit from the supermarket
    time2 = (spec2.tv_sec)*1000 + (spec2.tv_nsec) / 1000000;
    cs->time=time2-time1; //Time passed in the supermarket
    if (time3!=-1 && time4!=-1) cs->timeq=time4-time3; //Time passed in queue

    pthread_mutex_lock(&filemutex);
    fprintf(statsfile,"CUSTOMER -> | id customer:%d | n. bought products:%d | time in the supermarket: %0.3f s | time in queue: %0.3f s | n. queues checked: %d | \n",cs->id,cs->nproducts, (double) cs->time/1000, (double) cs->timeq/1000, cs->queuechecked);
    pthread_mutex_unlock(&filemutex);

    free(cs);
    return NULL; 
}

void * smcheckout(void *arg) { //Cashiers 

    struct timespec spec;
    struct timespec spec2;
    long time1,time2;

    clock_gettime(CLOCK_REALTIME, &spec); //Timestamp of the smcheckout open
    time1 = (spec.tv_sec)*1000 + (spec.tv_nsec) / 1000000;

    supermarketcheckout * smdata=((supermarketcheckout*)arg);
    smdata->hasbeenopened=1;
    int id = smdata->id-1;
    unsigned int seed=smdata->id+t; //Creating seed
    long randomtime,servicetime;
    int exittime=0; //If the cashier has to close the smcheckout

    pthread_mutex_lock(&queuemutex[id]);
    qs[id]->queueopen=1;
    pthread_mutex_unlock(&queuemutex[id]);

    if (smdata->randomtime==0) {
        while ((randomtime = rand_r(&seed) % 80)<20); //Random number of time interval: 20-80
        smdata->randomtime=randomtime;
    }
    
    
    pthread_t clock; 
    if (pthread_create(&clock,NULL,clockT,(void*) (intptr_t) (id))!=0) {
        fprintf(stderr,"clock %d: thread creation, failed!",id);
        exit(EXIT_FAILURE);
    }
    
    if(DEBUG) { DEBUG_PRINT(("----------------------SUPERMARKET CHECKOUT %d OPENED!--------------------\n",id)); fflush(stdout); }
    while(1){
        pthread_mutex_lock(&queuemutex[id]);
        while(queuelength(qs[id],id)==0 && exittime==0) { 
            if (sigquit==1) exittime=1;
            if (exitbroadcast==1) exittime=1;
            pthread_mutex_lock(&smexitmutex[id]);
            if (smexit[id]==1) exittime=1;
            pthread_mutex_unlock(&smexitmutex[id]);
            if (exittime!=1) { 
                if (DEBUG) { DEBUG_PRINT(("Cashier %d: Waiting customers\n",id)); fflush(stdout); }
                pthread_cond_wait(&smcond[id],&queuemutex[id]);
            }
            if (sigquit==1) exittime=1;
            if (exitbroadcast==1) exittime=1;
            pthread_mutex_lock(&smexitmutex[id]);
            if (smexit[id]==1) exittime=1;
            pthread_mutex_unlock(&smexitmutex[id]);
        } //Wait until the queue is empty or the queue has to close
        if (exittime!=1)
        {
            if(DEBUG) printQueue(qs[id],id); 
            customer * qcs=removecustomer(&qs[id],id); //Serves the customer that is the first in the queue    
            if (DEBUG) { DEBUG_PRINT(("Cashier %d: Serving customer: %d\n",id,qcs->id)); fflush(stdout); }
            pthread_mutex_unlock(&queuemutex[id]);
            //Number of time to scan the product and let the customer pay
            
            servicetime=smdata->randomtime+(cf->S*qcs->nproducts);
            struct timespec t={(servicetime/1000),((servicetime%1000)*1000000)};
            nanosleep(&t,NULL); //Sleeping randomtime mseconds

            //Signal to the customer that the cashier has done
            if (DEBUG) { DEBUG_PRINT(("Customer %d has paid!\n",qcs->id)); fflush(stdout); }
            smdata->nproducts+=qcs->nproducts;
            smdata->ncustomers++;
            //printf("%f",((smdata->servicetime*smdata->ncustomers)+servicetime)/smdata->ncustomers++);
            //printf("%ld \n",servicetime);
            smdata->servicetime=smdata->servicetime+((servicetime-(smdata->servicetime))/smdata->ncustomers);
            if (DEBUG) { DEBUG_PRINT(("CASHIER DATA %d: ", id)); printsm(*smdata); fflush(stdout); }

            qcs->queuedone=1;
            pthread_mutex_lock(&queuemutex[id]);
            pthread_cond_broadcast(&queuecond[id]);

            pthread_mutex_lock(&smexitmutex[id]);
            if (smexit[id]==1) exittime=1;
            pthread_mutex_unlock(&smexitmutex[id]);
        }
        if (exittime==1 || sigquit==1) {  
            qs[id]->queueopen=0;
            pthread_mutex_unlock(&queuemutex[id]);
            if (pthread_join(clock,NULL) == -1 ) {
                fprintf(stderr,"DirectorSMControl: thread join, failed!");
            }
            if (DEBUG) { DEBUG_PRINT(("--------------- SUPERMARKET CHECKOUT %d CLOSED ------------------\n",(id))); fflush(stdout); }
            pthread_mutex_lock(&smexitmutex[id]);
            smexit[id]=0;
            pthread_mutex_unlock(&smexitmutex[id]);

            pthread_mutex_lock(&queuemutex[id]);
            if (DEBUG) printQueue(qs[id],id);
            pthread_cond_broadcast(&queuecond[id]);
            resetQueue(&qs[id],id);
            if (DEBUG) printQueue(qs[id],id);
            pthread_mutex_unlock(&queuemutex[id]);

            clock_gettime(CLOCK_REALTIME, &spec2);
            time2 = (spec2.tv_sec)*1000 + (spec2.tv_nsec) / 1000000;
            smdata->time+=time2-time1;
            smdata->nclosure++;
            return NULL;
        }
        pthread_mutex_unlock(&queuemutex[id]);
    }
}

void * DirectorSMcontrol(void *arg) {

    pthread_t * smchecks;
    int smopen=cf->smopen; //smcheckouts opened

    smchecks = malloc(cf->K*sizeof(pthread_t)); //Creating smopen threads
    if (!smchecks){
        fprintf(stderr, "malloc fallita\n");
	    exit(EXIT_FAILURE);
    }

    //Starting smopen smcheckouts!
    for (int i=0;i<cf->smopen;i++){
        pthread_mutex_lock(&queuemutex[i]);
            qs[i]->queueopen=1;
        pthread_mutex_unlock(&queuemutex[i]);
        if (pthread_create(&smchecks[i],NULL,smcheckout,&((supermarketcheckout*)arg)[i])!=0) {
            fprintf(stderr,"supermarketcheckout %d: thread creation, failed!",i);
            exit(EXIT_FAILURE);
        }
    }
    
    int check; //If queueopen and number of cashier that have updated are equal -> Check if close or open a smcheckout
    int check1; //Counting the number of queue that has at most 1 customer
    int check2; //Checking if there is a queue with more then cf->S2 customers
    int index; //Index of sm checkout to open
    int counter; //Counts the number of sm checkouts opened
    int counter1; //Number of cashier that have updated queue's length
    int queueopen; //Number of queues open
    int j; //index to cicle
    int closeoropen=0; //One cicle director controls queue to close, in the other director controls queue to open

    while(sigquit!=1 && sighup!=1){
        check1=0;
        check2=0;
        pthread_mutex_lock(&updatelock);
        while(check1<cf->S1 && check2==0 && sigquit!=1 && sighup!=1){
            
            if (sigquit!=1 && sighup!=1) { pthread_cond_wait(&updatecond,&updatelock); }
            if (sigquit!=1 && sighup!=1) {
                check=0;
                queueopen=0;
                counter1=0; //Number of cashier that have updated queue's length
                for (int i=0;i<cf->K;i++) {
                    pthread_mutex_lock(&queuemutex[i]);
                    pthread_mutex_lock(&smexitmutex[i]);
                    if (qs[i]->queueopen==1 && smexit[i]!=1) queueopen++; //Checking how much queues are open
                    pthread_mutex_unlock(&smexitmutex[i]);
                    pthread_mutex_unlock(&queuemutex[i]);
                    pthread_mutex_lock(&upvarlock[i]);
                    if (updatevariable[i]==1) counter1++; //Incrementing Number of cashier that have updated queue's length
                    pthread_mutex_unlock(&upvarlock[i]);
                }
                if(DEBUG) { DEBUG_PRINT(("QUEUEOPEN=%d , COUNTER=%d\n",queueopen,counter1)); fflush(stdout); } 
                if (queueopen==counter1) check++; //If queueopen and number of cashier that have updated are equal -> Check if close or open a smcheckout

                if (check!=0){ //Director checks the queues lengths to decide if close or open some sm checkouts
                    for (int i=0;i<cf->K;i++){
                    pthread_mutex_lock(&qslengthmutex[i]);
                    pthread_mutex_lock(&queuemutex[i]);
                    if(qslength[i]<=1 && qs[i]->queueopen==1) check1++; //Counting the number of queue that has at most 1 customer
                    pthread_mutex_unlock(&queuemutex[i]);
                    if (qslength[i]>=cf->S2) check2++; //Checking if there is a queue with more then cf->S2 customers
                    pthread_mutex_unlock(&qslengthmutex[i]);
                    }
                }
                            
            }
        }
        pthread_mutex_unlock(&updatelock);
        
        for (int i=0;i<cf->K;i++) {
                pthread_mutex_lock(&upvarlock[i]);
                updatevariable[i]=0; //Reset the update variable after have checked them
                pthread_mutex_unlock(&upvarlock[i]);
        }
        
        if (sigquit!=1 && sighup!=1)
        {
            
            counter=0; //Number of queues just closed
            j=0; //index to cicle
            if (check1>0 && smopen>1 && closeoropen==0){
                while(counter<(check1-cf->S1+1) && j<cf->K){ //While you doesnt have closed the right number of sm checkouts
                    pthread_mutex_lock(&qslengthmutex[j]);
                    pthread_mutex_lock(&queuemutex[j]);
                    if(qslength[j]<=1 && qs[j]->queueopen==1) { //Check if the jth queue matches the conditions to close
                        pthread_mutex_lock(&smexitmutex[j]);
                        smexit[j]=1; //Set the jth queue has to close
                        pthread_mutex_unlock(&smexitmutex[j]);
                        pthread_cond_signal(&queuecond[j]);
                        counter++; //Number of queue just closed
                        smopen--; //Supermarket checkouts that are still open   
                    }
                    pthread_mutex_unlock(&queuemutex[j]);
                    pthread_mutex_unlock(&qslengthmutex[j]);
                    j++;
                }
            }
            //TODO: VARIABILE PER VERIFICARE SE SI E' CHIUSO DEF IL THREAD O NO
            if (check2>0 && closeoropen==1) {
                index=-1;
                for (int i=0;i<cf->K;i++){
                    pthread_mutex_lock(&queuemutex[i]);
                    if (DEBUG) { DEBUG_PRINT(("%d \n",qs[i]->queueopen)); fflush(stdout); }
                    pthread_mutex_lock(&smexitmutex[i]);
                    if(qs[i]->queueopen==0 && index==-1 && smexit[i]!=1) { index=i; } //if the ith queue matches the codntions to open take the index
                    pthread_mutex_unlock(&smexitmutex[i]);
                    pthread_mutex_unlock(&queuemutex[i]);
                }
                if (index!=-1) {
                    if (DEBUG) { DEBUG_PRINT(("INDEXXXXXXXXX:%d\n",index)); fflush(stdout); }
                    pthread_mutex_lock(&queuemutex[index]);
                    qs[index]->queueopen=1; //Set queue open
                    pthread_mutex_unlock(&queuemutex[index]);
                    if (pthread_create(&smchecks[index],NULL,smcheckout,&((supermarketcheckout*)arg)[index])!=0) { //Start cashier thread
                        fprintf(stderr,"supermarketcheckout %d: thread creation, failed!",index);
                    exit(EXIT_FAILURE);
                    }
                    smopen++;
                } 
            }
            if(closeoropen==0) closeoropen=1; else closeoropen=0; 
        }
    }

    if (sigquit==1) {
        for (int i=0;i<cf->K;i++) { //To wake up cashiers in case of a SIGQUIT and they are in wait
            pthread_mutex_lock(&queuemutex[i]);
            pthread_cond_signal(&smcond[i]);
            pthread_mutex_unlock(&queuemutex[i]);
        }   
    }

    for (int i=0;i<cf->K;i++){
        if(((supermarketcheckout*)arg)[i].hasbeenopened==1) {
            if (pthread_join(smchecks[i],NULL) == -1 ) {
                fprintf(stderr,"SMcheckout: thread join, failed!");
            }
        } 
    }

    if (DEBUG) { DEBUG_PRINT(("ALL CASHIER TERMINATED\n")); fflush(stdout); }

    if (DEBUG) { DEBUG_PRINT(("SM CONTROL TERMINATED\n")); fflush(stdout); }

    free(smchecks);
    return NULL;
}

void * DirectorCustomersControl (void *arg){
   
    pthread_t * cs;
    int customerscreated; //Number of all customers created from the start of the supermarket
    int activecustomers;//Number of active customers in the supermarket
    int i,j;
    int mallocsize;
    
    customerscreated=activecustomers=mallocsize=cf->C;

    //Creating C customers
    cs = malloc(mallocsize*sizeof(pthread_t)); //index 0-49
    if (!cs){
        fprintf(stderr, "malloc fallita\n");
	    exit(EXIT_FAILURE);
    }

    for (i=0;i<cf->C;i++) {
        if (pthread_create(&cs[i],NULL,customerT,(void*) (intptr_t) i)!=0) {
            fprintf(stderr,"customerT %d: thread creation, failed!",i);
            exit(EXIT_FAILURE);
        }
    }

    //Da implementare: caso di SIGHUP
    while(activecustomers!=0){
        pthread_mutex_lock(&okmutex); 
        while (activecustomers!=0) {
           if(queuelength(csexitq,-1)==0) pthread_cond_wait(&DirectorCond,&okmutex); //Get waken when a customer wants to get out of the supermarket 
           //Say to the customer that can exit
           customer * qcs = removecustomer(&csexitq,-1);
           qcs->exitok=1;
           activecustomers--;
           pthread_cond_signal(&okcond);
           if (DEBUG) { DEBUG_PRINT(("ACTIVE CUSTOMERS: %d\n",activecustomers)); fflush(stdout); }
           if (activecustomers==0) {
               for (int i=0;i<cf->K;i++) {
                    pthread_mutex_lock(&queuemutex[i]);
                    exitbroadcast=1;
                    pthread_cond_signal(&smcond[i]);
                    pthread_mutex_unlock(&queuemutex[i]);
               }   
           }
           if(activecustomers==(cf->C-cf->E) && sighup!=1 && sigquit!=1) break;
        }
        pthread_mutex_unlock(&okmutex);

        //If number of customers is equal to C-E ==> wake up E customers and let them enter the supermarket
        if (activecustomers==(cf->C-cf->E) && sighup!=1 && sigquit!=1) { 
            mallocsize+=cf->E;
            if((cs=realloc(cs,mallocsize*sizeof(pthread_t)))==NULL) {
                fprintf(stderr,"1° realloc failed");
                exit(EXIT_FAILURE);
            }
            j=customerscreated;
            if (DEBUG) { DEBUG_PRINT(("MALLOCSIZE:%d\n",mallocsize)); fflush(stdout); }
            for(i=0;i<cf->E;i++){
                if (pthread_create(&cs[j+i],NULL,customerT,(void*) (intptr_t) i+j)!=0) {
                    fprintf(stderr,"customerT %d: thread creation, failed!",j+i);
                    exit(EXIT_FAILURE);
                }
                customerscreated++;
                activecustomers++;
             }  
        }
    }
    
    for (i=0;i<mallocsize;i++){
        if (pthread_join(cs[i],NULL) == -1 ) {
            fprintf(stderr,"DirectorSMControl: thread join, failed!");
        }
    }

    if (DEBUG) { DEBUG_PRINT(("ALL CUSTOMERS THREAD TERMINATED \n")); fflush(stdout); }
    free(cs);
    return NULL;
}


void * directorT(void *arg) {

    
    //Creating subthread of director that manages the supermarket checkouts
    pthread_t DirectorSM;

    if (pthread_create(&DirectorSM,NULL,DirectorSMcontrol, (void*) arg)) {
        fprintf(stderr,"DirectorSMcontrol: thread creation, failed!");
        exit(EXIT_FAILURE);
    }

    //Creating subthread of director that manages the customers entry and exit
    pthread_t DirectorCustomers;
    if (pthread_create(&DirectorCustomers,NULL,DirectorCustomersControl,NULL)) {
        fprintf(stderr,"DirectorCustomersControl: thread creation, failed!");
        exit(EXIT_FAILURE);
    }

    
    //JOINS
    if (pthread_join(DirectorSM,NULL) == -1 ) {
            fprintf(stderr,"DirectorSMControl: thread join, failed!");
    }


    if (pthread_join(DirectorCustomers,NULL) == -1 ) {
            fprintf(stderr,"DirectorSMControl: thread join, failed!");
    }
    //JOIN'S END
    if (DEBUG) { DEBUG_PRINT(("DIRECTOR TERMINATED \n")); fflush(stdout); }
    //Free heap memory used!
    return NULL;
    //return (void*) csdata;
}

int main(int argc, char const *argv[])
{
    supermarketcheckout * smdata;
    struct sigaction s;
    t=time(NULL);
    long totalcustomers=0, totalproducts=0;

    if (argc!=2) {
        fprintf(stderr,"use: %s /testfile/config.txt \n",argv[0]);
        exit(EXIT_FAILURE);
    }

    if (strcmp(argv[1],"testfile/config.txt")==0){
        //Parameter's configure
        if ((cf=test(argv[1]))==NULL){
            exit(EXIT_FAILURE);
        }
    }
    else {
        fprintf(stderr,"%s: You need to insert the config file as argoument: /testfile/config.txt \n",argv[0]);
        exit(EXIT_FAILURE);
    }

    CreateQueueManagement(); //Create queues and mutex/condition vars to control them with threads.

    //Creating the "container" for cashier
    
    smdata = malloc(cf->K*sizeof(supermarketcheckout));
    for (int i=0;i<cf->K;i++){
        setupsm(&smdata[i],i);
    }

    if (!smdata ){
        fprintf(stderr, "malloc fallita\n");
	    exit(EXIT_FAILURE);
    }

    memset(&s,0,sizeof(s));
    s.sa_handler=handler;
    if (sigaction(SIGHUP,&s,NULL)==-1) {
        fprintf(stderr,"Handler error");
    }
    if (sigaction(SIGQUIT,&s,NULL)==-1) {
        fprintf(stderr,"Handler error");
    }


    if ((statsfile = fopen("statsfile.log", "w")) == NULL) { 
	fprintf(stderr, "Stats file opening failed");
	exit(EXIT_FAILURE);
    }


    //Creating director thread
    pthread_t director;

    if (pthread_create(&director,NULL,directorT,(void*)smdata)!=0) {
        fprintf(stderr,"Director thread creation, failed!");
            exit(EXIT_FAILURE);
    }

    if (pthread_join(director,NULL) == -1 ) {
            fprintf(stderr,"Director thread join, failed!");
        }

    for (int i=0;i<cf->K;i++) {
        fprintf(statsfile, "CASHIER -> | id:%d | n. bought products:%d | n. customers:%d | time opened: %0.3f s | avg service time: %0.3f s | number of clousure:%d |\n",smdata[i].id, smdata[i].nproducts, smdata[i].ncustomers, (double) smdata[i].time/1000, smdata[i].servicetime/1000, smdata[i].nclosure);
        totalcustomers+=smdata[i].ncustomers;
        totalproducts+=smdata[i].nproducts;
    }

    fprintf(statsfile, "TOTAL CUSTOMERS SERVED: %ld\n",totalcustomers);
    fprintf(statsfile, "TOTAL PRODUCTS BOUGHT: %ld\n", totalproducts);

    printf("PROGRAM FINISHED\n");
    

    fclose(statsfile); free(smdata); QueueFree(); free(cf);
    return 0;
}


void CreateQueueManagement(){

    //Creating queues for the K supermarket checkouts
    if ((qs=(queue**) malloc((cf->K)*sizeof(queue*)))==NULL) {
        fprintf(stderr, "malloc failed\n");
        exit(EXIT_FAILURE);  
    }
    for (int i=0;i<cf->K; i++) {qs[i]=createqueues(i);}
   

    //Creating the mutex for the queues
    if((queuemutex=malloc(cf->K*sizeof(pthread_mutex_t)))==NULL){
        fprintf(stderr, "malloc failed\n");
        exit(EXIT_FAILURE);  
    }
    for (int i=0;i<cf->K;i++){
        if (pthread_mutex_init(&queuemutex[i], NULL) != 0) {
            fprintf(stderr, "pthread_mutex_init queue failed\n");
            exit(EXIT_FAILURE);                   
        }
    }

    //Creating the condition variable for the K queues
    if((queuecond=malloc(cf->K*sizeof(pthread_cond_t)))==NULL){
        fprintf(stderr, "malloc failed\n");
        exit(EXIT_FAILURE); 
    }
    for (int i=0;i<cf->K;i++){
        if (pthread_cond_init(&queuecond[i], NULL) != 0) {
            fprintf(stderr, "pthread_cond_init queue failed\n");
            exit(EXIT_FAILURE);                   
        }
    }
    
    csexitq=createqueues(-1);

    if((smcond=malloc(cf->K*sizeof(pthread_cond_t)))==NULL){
        fprintf(stderr, "malloc failed\n");
        exit(EXIT_FAILURE); 
    }
    for (int i=0;i<cf->K;i++){
        if (pthread_cond_init(&smcond[i], NULL) != 0) {
            fprintf(stderr, "pthread_cond_init queue failed\n");
            exit(EXIT_FAILURE);                   
        }
    }


    //Creating the mutex for the queues
    if((qslengthmutex=malloc(cf->K*sizeof(pthread_mutex_t)))==NULL){
        fprintf(stderr, "malloc failed\n");
        exit(EXIT_FAILURE);  
    }
    for (int i=0;i<cf->K;i++){
        if (pthread_mutex_init(&qslengthmutex[i], NULL) != 0) {
            fprintf(stderr, "pthread_mutex_init queue failed\n");
            exit(EXIT_FAILURE);                   
        }
    }

    if((qslengthcond=malloc(cf->K*sizeof(pthread_cond_t)))==NULL){
        fprintf(stderr, "malloc failed\n");
        exit(EXIT_FAILURE); 
    }
    for (int i=0;i<cf->K;i++){
        if (pthread_cond_init(&qslengthcond[i], NULL) != 0) {
            fprintf(stderr, "pthread_cond_init queue failed\n");
            exit(EXIT_FAILURE);                   
        }
    }

    if ((qslength=malloc((cf->K)*sizeof(int)))==NULL) {
        fprintf(stderr, "malloc failed\n");
        exit(EXIT_FAILURE);  
    }
    for (int i=0;i<cf->K;i++) qslength[i]=queuelength(qs[i],i);


    if((smexitmutex=malloc(cf->K*sizeof(pthread_mutex_t)))==NULL){
        fprintf(stderr, "malloc failed\n");
        exit(EXIT_FAILURE);  
    }
    for (int i=0;i<cf->K;i++){
        if (pthread_mutex_init(&smexitmutex[i], NULL) != 0) {
            fprintf(stderr, "pthread_mutex_init queue failed\n");
            exit(EXIT_FAILURE);                   
        }
    }

    if ((smexit=malloc((cf->K)*sizeof(int)))==NULL) {
        fprintf(stderr, "malloc failed\n");
        exit(EXIT_FAILURE);  
    }
    for (int i=0;i<cf->K;i++) smexit[i]=0;

    if ((updatevariable=malloc((cf->K)*sizeof(int)))==NULL) {
        fprintf(stderr, "malloc failed\n");
        exit(EXIT_FAILURE);  
    }
    for (int i=0;i<cf->K;i++) updatevariable[i]=0;


    if((upvarlock=malloc(cf->K*sizeof(pthread_mutex_t)))==NULL){
        fprintf(stderr, "malloc failed\n");
        exit(EXIT_FAILURE);  
    }
    for (int i=0;i<cf->K;i++){
        if (pthread_mutex_init(&upvarlock[i], NULL) != 0) {
            fprintf(stderr, "pthread_mutex_init upvarlock failed\n");
            exit(EXIT_FAILURE);                   
        }
    }

}


void QueueFree(){

    for (int i=0;i<cf->K; i++) free(qs[i]);
    free(qs); 
    free(queuemutex); free(queuecond); free(smcond); free(csexitq); free(qslengthmutex);
    free(qslengthcond); free(qslength); free(smexitmutex); free(smexit); free(updatevariable);
    free(upvarlock);
}

void setupsm(supermarketcheckout * sm, int i) {
    sm->id = (i+1);
    sm->nproducts=0;
    sm->ncustomers=0;
    sm->time=0;
    sm->servicetime=0;
    sm->nclosure=0; 
    sm->randomtime=0; 
    sm->hasbeenopened=0; 
}

void printsm(supermarketcheckout sm) {
    printf("%d %d %d %d %f %d\n",sm.id,sm.nproducts,sm.ncustomers,sm.time,sm.servicetime,sm.nclosure);
}


int confcheck(config * cf) {
    if (!(cf->P>=0 && cf->T>10 && cf->K>0 && cf->S>0 && (cf->E>0 && cf->E<cf->C) && cf->C>1 && cf->S1>0  && cf->S2>0 && cf->S1<cf->S2 && cf->smopen>0 && cf->smopen<=cf->K && cf->directornews>cf->T)){
        fprintf(stderr,"conf not valid, constraints: P>=0, T>10, K>0, S>0, 0<E<C, C>1, S1>0, S1<S2, smopen>0, smopen<=K, directornews>T \n");
        return -1;
    }
    else return 1;
}

config * test(const char* configfile) {
    int control,i=0,j;
    config * cf;
    FILE *fd=NULL;
    char *buffer;
    char *cpy;

    if ((fd=fopen(configfile, "r")) == NULL) {
        fclose(fd);
	    return NULL;
    }

    if ((cf=malloc(sizeof(config))) == NULL) {
        fclose(fd); free(cf);
	    return NULL;    
    }

    if ((buffer=malloc(MAX_LEN*sizeof(char))) == NULL) {
         fclose(fd); free(cf); free(buffer);
         return NULL;
    }

    while(fgets(buffer, MAX_LEN, fd) != NULL) {
        j=0;
        cpy=buffer;
        while(*buffer!='=') { buffer++; j++;}
        buffer++;
        switch (i)
        {
            case 0: cf->K=atoi(buffer);
                break;
            case 1: cf->C=atoi(buffer);
                break;
            case 2: cf->E=atoi(buffer);
                break;
            case 3: cf->T=atoi(buffer);
                break;
            case 4: cf->P=atoi(buffer);
                break;
            case 5: cf->S=atoi(buffer);
                break;
            case 6: cf->S1=atoi(buffer);
                break;
            case 7: cf->S2=atoi(buffer);
                break;
            case 8: cf->smopen=atoi(buffer);
                break;
            case 9: cf->directornews=atoi(buffer);
                break;

            default:
                break;
        }
        i++;
        buffer=cpy;
    }

    if ((control=confcheck(cf))==-1) {
        fclose(fd); free(cf); free(buffer);
        return NULL;
    }
    free(buffer);
    fclose(fd); 
    
    return cf;
}

void printconf(config configvalues) {
    printf("%d %d %d %d %d %d %d %d %d %d\n", configvalues.K, configvalues.C, configvalues.E, configvalues.T, configvalues.P, configvalues.S, configvalues.S1, configvalues.S2, configvalues.smopen, configvalues.directornews);
}


