#if !defined(HEADER_VAR_H)
#define HEADER_VAR_H
#include <stdio.h>
#include <pthread.h>
#include <signal.h>

// Mutex e condizioni relative alle code delle Casse.
static pthread_mutex_t *McodaClienti;
static pthread_cond_t *CcodaClienti;
static pthread_cond_t *CcodaClientiNotEmpty;

// Mutex per l'accesso alla variabile aChiudiCassa per il controllo delle casse.
static pthread_mutex_t *MChiudiCassa;
// Array di K posizioni che contengono l'informazione riguardo la chiusura da parte del direttore
static int *aChiudiCassa;

// MUTEX del Direttore e della sua coda
static pthread_mutex_t McodaDirettore = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t CcodaDirettoreClienteEsce = PTHREAD_COND_INITIALIZER;
static pthread_cond_t CcodaDirettoreNotEmpty = PTHREAD_COND_INITIALIZER;

// Used to syncronized the variable lenghtCode for the queues length (Used by
// director to open/close the supermarket checkouts)
static pthread_mutex_t *MlengthCode;
static pthread_cond_t *ClengthCode;

// Lock and condition variable to syncronize the update by the cashiers
static pthread_mutex_t MupdateCasse = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t CupdateCasse = PTHREAD_COND_INITIALIZER;
static pthread_mutex_t *McassaUpdateInfo;

// FILE MUTEX
static pthread_mutex_t Mfile = PTHREAD_MUTEX_INITIALIZER;

static int *lenghtCode; // Array that contains queue's length

// Array that contains if the queue is going to be closed or not
static int *aUpdateCassa;      // Used to update the director from cashier!
static int sigUPCassaExit = 0; // Used when activecustomers are = 0 to send a
                               // signal to cashier in case of a SIGHUP
static long globalTime;        // TIME
static FILE *fileLog;          // File containing stats of the execution

volatile sig_atomic_t sig_HUP = 0;
volatile sig_atomic_t sig_QUIT = 0;

#endif