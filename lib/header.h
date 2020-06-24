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

// Mutex e condizioni per l'update da parte del direttore.
static pthread_mutex_t *MlengthCode;
static pthread_cond_t *ClengthCode;

// Mutex e condizioni per l'update da parte delle casse
static pthread_mutex_t MupdateCasse = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t CupdateCasse = PTHREAD_COND_INITIALIZER;
static pthread_mutex_t *McassaUpdateInfo;

// FILE MUTEX
static pthread_mutex_t Mfile = PTHREAD_MUTEX_INITIALIZER;

static int *lenghtCode; // Array contenente la lunghezza delle code al momento dell'update

// Array contenente le informazioni relative alla chiusura delle casse da parte del direttore
// {0} -> Rimani aperta {1} -> Chiudi
static int *aUpdateCassa;

// Quando viene ricevuto il segnale Sig_HUP notifico a tutte le casse che possono uscire.
static int sigUPCassaExit = 0;

static long globalTime;
static FILE *fileLog;

volatile sig_atomic_t sig_HUP = 0;
volatile sig_atomic_t sig_QUIT = 0;

#endif