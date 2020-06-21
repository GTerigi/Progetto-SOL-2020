#define _POSIX_C_SOURCE 199309L

#include <Queue/queue.h>
#include <errno.h>
#include <header.h>
#include <inttypes.h>
#include <math.h>
#include <pthread.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#define MAX_LEN 256

typedef struct infoCassa_ {
  int IDcassa;
  int prodElaborati;
  int clientiProcessati;
  int tempoOpen;
  float tempoServizio;
  int Nchiusure;
  long tempoElabProdotto;
  int HBO; // Has Been Opened. Controlla se la cassa è mai stata aperta o meno.
} infoCassa;

void initInfoCassa(infoCassa *sm, int i);
void printsm(infoCassa sm);

typedef struct config {
  int K_cassieri;
  int C_clienti;
  int E_min;
  int T_buyTimeCliente;
  int P_maxProdCliente;
  int S_timeElabCassa;
  int S1;
  int S2;
  int startCasse;
  int timeNotifica;
} config;

static config *globalParamSupermercato;
static queue **codaCassa;
static queue *codaDirettore;

// === DIRETTORE === //

// === CASSE === //

void *updateDirT(void *arg);
void *mainCassa(void *arg);

// === CLIENTE === //

void *mainCliente(void *arg);

// === UTILITY === //

void SetSignalHandler();
static void SignalHandeler(int SignalNumber);
long msecond_timespec_diff(struct timespec *start, struct timespec *end);

int confcheck(config *globalParamSupermercato);
config *test(const char *configfile);
// void printconf(config configvalues);

void CreateQueueManagement();
void QueueFree();

void *DirectorSMcontrol(void *arg) {

  pthread_t *smchecks;
  int smopen = globalParamSupermercato->startCasse; // smcheckouts opened

  smchecks = malloc(globalParamSupermercato->K_cassieri *
                    sizeof(pthread_t)); // Creating smopen threads
  if (!smchecks) {
    fprintf(stderr, "malloc fallita\n");
    exit(EXIT_FAILURE);
  }

  // Starting smopen smcheckouts!
  for (int i = 0; i < globalParamSupermercato->startCasse; i++) {
    pthread_mutex_lock(&McodaClienti[i]);
    codaCassa[i]->aperta = 1;
    pthread_mutex_unlock(&McodaClienti[i]);
    if (pthread_create(&smchecks[i], NULL, mainCassa, &((infoCassa *)arg)[i]) !=
        0) {
      fprintf(stderr, "infoCassa %d: thread creation, failed!", i);
      exit(EXIT_FAILURE);
    }
  }

  int check;   // If queueopen and number of cashier that have updated are equal
               // -> Check if close or open a mainCassa
  int check1;  // Counting the number of queue that has at most 1 customer
  int check2;  // Checking if there is a queue with more then
               // globalParamSupermercato->S2 customers
  int index;   // Index of sm checkout to open
  int counter; // Counts the number of sm checkouts opened
  int counter1;        // Number of cashier that have updated queue's length
  int queueopen;       // Number of queues open
  int j;               // index to cicle
  int closeoropen = 0; // One cicle director controls queue to close, in the
                       // other director controls queue to open

  while (sig_QUIT != 1 && sig_HUP != 1) {
    check1 = 0;
    check2 = 0;
    pthread_mutex_lock(&MupdateCasse);
    while (check1 < globalParamSupermercato->S1 && check2 == 0 &&
           sig_QUIT != 1 && sig_HUP != 1) {

      if (sig_QUIT != 1 && sig_HUP != 1) {
        pthread_cond_wait(&CupdateCasse, &MupdateCasse);
      }
      if (sig_QUIT != 1 && sig_HUP != 1) {
        check = 0;
        queueopen = 0;
        counter1 = 0; // Number of cashier that have updated queue's length
        for (int i = 0; i < globalParamSupermercato->K_cassieri; i++) {
          pthread_mutex_lock(&McodaClienti[i]);
          pthread_mutex_lock(&MChiudiCassa[i]);
          if (codaCassa[i]->aperta == 1 && aChiudiCassa[i] != 1)
            queueopen++; // Checking how much queues are open
          pthread_mutex_unlock(&MChiudiCassa[i]);
          pthread_mutex_unlock(&McodaClienti[i]);
          pthread_mutex_lock(&McassaUpdateInfo[i]);
          if (aUpdateCassa[i] == 1)
            counter1++; // Incrementing Number of cashier that have updated
                        // queue's length
          pthread_mutex_unlock(&McassaUpdateInfo[i]);
        }
        if (queueopen == counter1)
          check++; // If queueopen and number of cashier that have updated are
                   // equal -> Check if close or open a mainCassa

        if (check != 0) { // Director checks the queues lengths to decide if
                          // close or open some sm checkouts
          for (int i = 0; i < globalParamSupermercato->K_cassieri; i++) {
            pthread_mutex_lock(&MlengthCode[i]);
            pthread_mutex_lock(&McodaClienti[i]);
            if (lenghtCode[i] <= 1 && codaCassa[i]->aperta == 1)
              check1++; // Counting the number of queue that has at most 1
                        // customer
            pthread_mutex_unlock(&McodaClienti[i]);
            if (lenghtCode[i] >= globalParamSupermercato->S2)
              check2++; // Checking if there is a queue with more then
                        // globalParamSupermercato->S2 customers
            pthread_mutex_unlock(&MlengthCode[i]);
          }
        }
      }
    }
    pthread_mutex_unlock(&MupdateCasse);

    for (int i = 0; i < globalParamSupermercato->K_cassieri; i++) {
      pthread_mutex_lock(&McassaUpdateInfo[i]);
      aUpdateCassa[i] = 0; // Reset the update variable after have checked
                           // them
      pthread_mutex_unlock(&McassaUpdateInfo[i]);
    }

    if (sig_QUIT != 1 && sig_HUP != 1) {

      counter = 0; // Number of queues just closed
      j = 0;       // index to cicle
      if (check1 > 0 && smopen > 1 && closeoropen == 0) {
        while (counter < (check1 - globalParamSupermercato->S1 + 1) &&
               j < globalParamSupermercato
                       ->K_cassieri) { // While you doesnt have closed the right
                                       // number
                                       // of sm checkouts
          pthread_mutex_lock(&MlengthCode[j]);
          pthread_mutex_lock(&McodaClienti[j]);
          if (lenghtCode[j] <= 1 && codaCassa[j]->aperta == 1) {
            // Check if the jth queue matches the conditions to close
            pthread_mutex_lock(&MChiudiCassa[j]);
            aChiudiCassa[j] = 1; // Set the jth queue has to close
            pthread_mutex_unlock(&MChiudiCassa[j]);
            pthread_cond_signal(&CcodaClienti[j]);
            counter++; // Number of queue just closed
            smopen--;  // Supermarket checkouts that are still open
          }
          pthread_mutex_unlock(&McodaClienti[j]);
          pthread_mutex_unlock(&MlengthCode[j]);
          j++;
        }
      }
      // TODO: VARIABILE PER VERIFICARE SE SI E' CHIUSO DEF IL THREAD O NO
      if (check2 > 0 && closeoropen == 1) {
        index = -1;
        for (int i = 0; i < globalParamSupermercato->K_cassieri; i++) {
          pthread_mutex_lock(&McodaClienti[i]);
          pthread_mutex_lock(&MChiudiCassa[i]);
          if (codaCassa[i]->aperta == 0 && index == -1 &&
              aChiudiCassa[i] != 1) {
            index = i;
          } // if the ith queue matches the codntions to open take the index
          pthread_mutex_unlock(&MChiudiCassa[i]);
          pthread_mutex_unlock(&McodaClienti[i]);
        }
        if (index != -1) {
          pthread_mutex_lock(&McodaClienti[index]);
          codaCassa[index]->aperta = 1; // Set queue open
          pthread_mutex_unlock(&McodaClienti[index]);
          if (pthread_create(&smchecks[index], NULL, mainCassa,
                             &((infoCassa *)arg)[index]) !=
              0) { // Start cashier thread
            fprintf(stderr, "infoCassa %d: thread creation, failed!", index);
            exit(EXIT_FAILURE);
          }
          smopen++;
        }
      }
      if (closeoropen == 0)
        closeoropen = 1;
      else
        closeoropen = 0;
    }
  }

  if (sig_QUIT == 1) {
    for (int i = 0; i < globalParamSupermercato->K_cassieri;
         i++) { // To wake up cashiers in case of a SIGQUIT and they are in wait
      pthread_mutex_lock(&McodaClienti[i]);
      pthread_cond_signal(&CcodaClientiNotEmpty[i]);
      pthread_mutex_unlock(&McodaClienti[i]);
    }
  }

  for (int i = 0; i < globalParamSupermercato->K_cassieri; i++) {
    if (((infoCassa *)arg)[i].HBO == 1) {
      if (pthread_join(smchecks[i], NULL) == -1) {
        fprintf(stderr, "SMcheckout: thread join, failed!");
      }
    }
  }

  free(smchecks);
  return NULL;
}

void *DirectorCustomersControl(void *arg) {

  pthread_t *cs;
  int customerscreated; // Number of all customers created from the start of the
                        // supermarket
  int activecustomers;  // Number of active customers in the supermarket
  int i, j;
  int mallocsize;

  customerscreated = activecustomers = mallocsize =
      globalParamSupermercato->C_clienti;

  // Creating C customers
  cs = malloc(mallocsize * sizeof(pthread_t)); // index 0-49
  if (!cs) {
    fprintf(stderr, "malloc fallita\n");
    exit(EXIT_FAILURE);
  }

  for (i = 0; i < globalParamSupermercato->C_clienti; i++) {
    if (pthread_create(&cs[i], NULL, mainCliente, (void *)(intptr_t)i) != 0) {
      fprintf(stderr, "mainCliente %d: thread creation, failed!", i);
      exit(EXIT_FAILURE);
    }
  }

  // Da implementare: caso di SIGHUP
  while (activecustomers != 0) {
    pthread_mutex_lock(&McodaDirettore);
    while (activecustomers != 0) {
      if (codaDirettore->length == 0)
        pthread_cond_wait(&CcodaDirettoreNotEmpty,
                          &McodaDirettore); // Get waken when a customer wants
                                            // to get out of the supermarket
      // Say to the customer that can exit
      Cliente *cliente = removecustomer(&codaDirettore, -1);
      cliente->possoUscire = 1;
      activecustomers--;
      pthread_cond_signal(&CcodaDirettoreClienteEsce);
      if (activecustomers == 0) {
        for (int i = 0; i < globalParamSupermercato->K_cassieri; i++) {
          pthread_mutex_lock(&McodaClienti[i]);
          sigUPCassaExit = 1;
          pthread_cond_signal(&CcodaClientiNotEmpty[i]);
          pthread_mutex_unlock(&McodaClienti[i]);
        }
      }
      if (activecustomers == (globalParamSupermercato->C_clienti -
                              globalParamSupermercato->E_min) &&
          sig_HUP != 1 && sig_QUIT != 1)
        break;
    }
    pthread_mutex_unlock(&McodaDirettore);

    // If number of customers is equal to C-E ==> wake up E customers and let
    // them enter the supermarket
    if (activecustomers == (globalParamSupermercato->C_clienti -
                            globalParamSupermercato->E_min) &&
        sig_HUP != 1 && sig_QUIT != 1) {
      mallocsize += globalParamSupermercato->E_min;
      if ((cs = realloc(cs, mallocsize * sizeof(pthread_t))) == NULL) {
        fprintf(stderr, "1° realloc failed");
        exit(EXIT_FAILURE);
      }
      j = customerscreated;

      for (i = 0; i < globalParamSupermercato->E_min; i++) {
        if (pthread_create(&cs[j + i], NULL, mainCliente,
                           (void *)(intptr_t)i + j) != 0) {
          fprintf(stderr, "mainCliente %d: thread creation, failed!", j + i);
          exit(EXIT_FAILURE);
        }
        customerscreated++;
        activecustomers++;
      }
    }
  }

  for (i = 0; i < mallocsize; i++) {
    if (pthread_join(cs[i], NULL) == -1) {
      fprintf(stderr, "DirectorSMControl: thread join, failed!");
    }
  }

  free(cs);
  return NULL;
}

void *directorT(void *arg) {

  // Creating subthread of director that manages the supermarket checkouts
  pthread_t DirectorSM;

  if (pthread_create(&DirectorSM, NULL, DirectorSMcontrol, (void *)arg)) {
    fprintf(stderr, "DirectorSMcontrol: thread creation, failed!");
    exit(EXIT_FAILURE);
  }

  // Creating subthread of director that manages the customers entry and exit
  pthread_t DirectorCustomers;
  if (pthread_create(&DirectorCustomers, NULL, DirectorCustomersControl,
                     NULL)) {
    fprintf(stderr, "DirectorCustomersControl: thread creation, failed!");
    exit(EXIT_FAILURE);
  }

  // JOINS
  if (pthread_join(DirectorSM, NULL) == -1) {
    fprintf(stderr, "DirectorSMControl: thread join, failed!");
  }

  if (pthread_join(DirectorCustomers, NULL) == -1) {
    fprintf(stderr, "DirectorSMControl: thread join, failed!");
  }
  // JOIN'S END
  // Free heap memory used!
  return NULL;
  // return (void*) csdata;
}

int main(int argc, char const *argv[]) {
  infoCassa *Casse;

  SetSignalHandler();
  globalTime = time(NULL);
  long totalcustomers = 0, totalproducts = 0;

  if (argc != 2 && argc != 1) {
    fprintf(stderr, "Errore: Usare %s {Path/To/config.txt}\n", argv[0]);
    exit(EXIT_FAILURE);
  }

  if (argc == 2) {
    // Parameter's configure
    if ((globalParamSupermercato = test(argv[1])) == NULL) {
      exit(EXIT_FAILURE);
    }
  } else {
    if ((globalParamSupermercato = test("config.txt")) == NULL) {
      exit(EXIT_FAILURE);
    }
  }

  CreateQueueManagement(); // Create queues and mutex/condition vars to control
                           // them with threads.

  // Creating the "container" for cashier

  Casse = malloc(globalParamSupermercato->K_cassieri * sizeof(infoCassa));
  for (int i = 0; i < globalParamSupermercato->K_cassieri; i++) {
    initInfoCassa(&Casse[i], i);
  }

  if (!Casse) {
    fprintf(stderr, "malloc fallita\n");
    exit(EXIT_FAILURE);
  }
  // Aggiungo la maschera per i segnali

  if ((fileLog = fopen("statsfile.log", "w")) == NULL) {
    fprintf(stderr, "Stats file opening failed");
    exit(EXIT_FAILURE);
  }

  // Creating director thread
  pthread_t director;

  if (pthread_create(&director, NULL, directorT, (void *)Casse) != 0) {
    fprintf(stderr, "Director thread creation, failed!");
    exit(EXIT_FAILURE);
  }

  if (pthread_join(director, NULL) == -1) {
    fprintf(stderr, "Director thread join, failed!");
  }

  for (int i = 0; i < globalParamSupermercato->K_cassieri; i++) {
    fprintf(fileLog,
            "CASHIER -> | ID:%d | n. bought products:%d | n. customers:%d | "
            "time opened: %0.3f s | avg service time: %0.3f s | number of "
            "clousure:%d |\n",
            Casse[i].IDcassa, Casse[i].prodElaborati,
            Casse[i].clientiProcessati, (double)Casse[i].tempoOpen / pow(10, 6),
            Casse[i].tempoServizio / 1000, Casse[i].Nchiusure);
    totalcustomers += Casse[i].clientiProcessati;
    totalproducts += Casse[i].prodElaborati;
  }

  fprintf(fileLog, "TOTAL CUSTOMERS SERVED: %ld\n", totalcustomers);
  fprintf(fileLog, "TOTAL PRODUCTS BOUGHT: %ld\n", totalproducts);

  printf("PROGRAM FINISHED\n");

  fclose(fileLog);
  free(Casse);
  QueueFree();
  free(globalParamSupermercato);
  return 0;
}

void CreateQueueManagement() {

  // Creating queues for the K supermarket checkouts
  if ((codaCassa = (queue **)malloc((globalParamSupermercato->K_cassieri) *
                                    sizeof(queue *))) == NULL) {
    fprintf(stderr, "malloc failed\n");
    exit(EXIT_FAILURE);
  }
  for (int i = 0; i < globalParamSupermercato->K_cassieri; i++) {
    codaCassa[i] = createqueues(i);
  }

  // Creating the mutex for the queues
  if ((McodaClienti = malloc(globalParamSupermercato->K_cassieri *
                             sizeof(pthread_mutex_t))) == NULL) {
    fprintf(stderr, "malloc failed\n");
    exit(EXIT_FAILURE);
  }
  for (int i = 0; i < globalParamSupermercato->K_cassieri; i++) {
    if (pthread_mutex_init(&McodaClienti[i], NULL) != 0) {
      fprintf(stderr, "pthread_mutex_init queue failed\n");
      exit(EXIT_FAILURE);
    }
  }

  // Creating the condition variable for the K queues
  if ((CcodaClienti = malloc(globalParamSupermercato->K_cassieri *
                             sizeof(pthread_cond_t))) == NULL) {
    fprintf(stderr, "malloc failed\n");
    exit(EXIT_FAILURE);
  }
  for (int i = 0; i < globalParamSupermercato->K_cassieri; i++) {
    if (pthread_cond_init(&CcodaClienti[i], NULL) != 0) {
      fprintf(stderr, "pthread_cond_init queue failed\n");
      exit(EXIT_FAILURE);
    }
  }

  codaDirettore = createqueues(-1);

  if ((CcodaClientiNotEmpty = malloc(globalParamSupermercato->K_cassieri *
                                     sizeof(pthread_cond_t))) == NULL) {
    fprintf(stderr, "malloc failed\n");
    exit(EXIT_FAILURE);
  }
  for (int i = 0; i < globalParamSupermercato->K_cassieri; i++) {
    if (pthread_cond_init(&CcodaClientiNotEmpty[i], NULL) != 0) {
      fprintf(stderr, "pthread_cond_init queue failed\n");
      exit(EXIT_FAILURE);
    }
  }

  // Creating the mutex for the queues
  if ((MlengthCode = malloc(globalParamSupermercato->K_cassieri *
                            sizeof(pthread_mutex_t))) == NULL) {
    fprintf(stderr, "malloc failed\n");
    exit(EXIT_FAILURE);
  }
  for (int i = 0; i < globalParamSupermercato->K_cassieri; i++) {
    if (pthread_mutex_init(&MlengthCode[i], NULL) != 0) {
      fprintf(stderr, "pthread_mutex_init queue failed\n");
      exit(EXIT_FAILURE);
    }
  }

  if ((ClengthCode = malloc(globalParamSupermercato->K_cassieri *
                            sizeof(pthread_cond_t))) == NULL) {
    fprintf(stderr, "malloc failed\n");
    exit(EXIT_FAILURE);
  }
  for (int i = 0; i < globalParamSupermercato->K_cassieri; i++) {
    if (pthread_cond_init(&ClengthCode[i], NULL) != 0) {
      fprintf(stderr, "pthread_cond_init queue failed\n");
      exit(EXIT_FAILURE);
    }
  }

  if ((lenghtCode = malloc((globalParamSupermercato->K_cassieri) *
                           sizeof(int))) == NULL) {
    fprintf(stderr, "malloc failed\n");
    exit(EXIT_FAILURE);
  }
  for (int i = 0; i < globalParamSupermercato->K_cassieri; i++)
    lenghtCode[i] = codaCassa[i]->length;

  if ((MChiudiCassa = malloc(globalParamSupermercato->K_cassieri *
                             sizeof(pthread_mutex_t))) == NULL) {
    fprintf(stderr, "malloc failed\n");
    exit(EXIT_FAILURE);
  }
  for (int i = 0; i < globalParamSupermercato->K_cassieri; i++) {
    if (pthread_mutex_init(&MChiudiCassa[i], NULL) != 0) {
      fprintf(stderr, "pthread_mutex_init queue failed\n");
      exit(EXIT_FAILURE);
    }
  }

  if ((aChiudiCassa = malloc((globalParamSupermercato->K_cassieri) *
                             sizeof(int))) == NULL) {
    fprintf(stderr, "malloc failed\n");
    exit(EXIT_FAILURE);
  }
  for (int i = 0; i < globalParamSupermercato->K_cassieri; i++)
    aChiudiCassa[i] = 0;

  if ((aUpdateCassa = malloc((globalParamSupermercato->K_cassieri) *
                             sizeof(int))) == NULL) {
    fprintf(stderr, "malloc failed\n");
    exit(EXIT_FAILURE);
  }
  for (int i = 0; i < globalParamSupermercato->K_cassieri; i++)
    aUpdateCassa[i] = 0;

  if ((McassaUpdateInfo = malloc(globalParamSupermercato->K_cassieri *
                                 sizeof(pthread_mutex_t))) == NULL) {
    fprintf(stderr, "malloc failed\n");
    exit(EXIT_FAILURE);
  }
  for (int i = 0; i < globalParamSupermercato->K_cassieri; i++) {
    if (pthread_mutex_init(&McassaUpdateInfo[i], NULL) != 0) {
      fprintf(stderr, "pthread_mutex_init McassaUpdateInfo failed\n");
      exit(EXIT_FAILURE);
    }
  }
}

void QueueFree() {

  for (int i = 0; i < globalParamSupermercato->K_cassieri; i++)
    free(codaCassa[i]);
  free(codaCassa);
  free(McodaClienti);
  free(CcodaClienti);
  free(CcodaClientiNotEmpty);
  free(codaDirettore);
  free(MlengthCode);
  free(ClengthCode);
  free(lenghtCode);
  free(MChiudiCassa);
  free(aChiudiCassa);
  free(aUpdateCassa);
  free(McassaUpdateInfo);
}

void initInfoCassa(infoCassa *sm, int i) {
  sm->IDcassa = (i + 1);
  sm->prodElaborati = 0;
  sm->clientiProcessati = 0;
  sm->tempoOpen = 0;
  sm->tempoServizio = 0;
  sm->Nchiusure = 0;
  sm->tempoElabProdotto = 0;
  sm->HBO = 0;
}

void printsm(infoCassa sm) {
  printf("%d %d %d %d %f %d\n", sm.IDcassa, sm.prodElaborati,
         sm.clientiProcessati, sm.tempoOpen, sm.tempoServizio, sm.Nchiusure);
}

int confcheck(config *globalParamSupermercato) {
  if (!(globalParamSupermercato->P_maxProdCliente >= 0 &&
        globalParamSupermercato->T_buyTimeCliente > 10 &&
        globalParamSupermercato->K_cassieri > 0 &&
        globalParamSupermercato->S_timeElabCassa > 0 &&
        (globalParamSupermercato->E_min > 0 &&
         globalParamSupermercato->E_min < globalParamSupermercato->C_clienti) &&
        globalParamSupermercato->C_clienti > 1 &&
        globalParamSupermercato->S1 > 0 && globalParamSupermercato->S2 > 0 &&
        globalParamSupermercato->S1 < globalParamSupermercato->S2 &&
        globalParamSupermercato->startCasse > 0 &&
        globalParamSupermercato->startCasse <=
            globalParamSupermercato->K_cassieri &&
        globalParamSupermercato->timeNotifica >
            globalParamSupermercato->T_buyTimeCliente)) {
    fprintf(stderr, "conf not valid, constraints: P>=0, T>10, K>0, S>0, 0<E<C, "
                    "C>1, S1>0, S1<S2, smopen>0, smopen<=K, directornews>T \n");
    return -1;
  } else
    return 1;
}

config *test(const char *configfile) {
  int control, i = 0, j;
  config *globalParamSupermercato;
  FILE *fd = NULL;
  char *buffer;
  char *cpy;

  if ((fd = fopen(configfile, "r")) == NULL) {
    fclose(fd);
    return NULL;
  }

  if ((globalParamSupermercato = malloc(sizeof(config))) == NULL) {
    fclose(fd);
    free(globalParamSupermercato);
    return NULL;
  }

  if ((buffer = malloc(MAX_LEN * sizeof(char))) == NULL) {
    fclose(fd);
    free(globalParamSupermercato);
    free(buffer);
    return NULL;
  }

  while (fgets(buffer, MAX_LEN, fd) != NULL) {
    j = 0;
    cpy = buffer;
    while (*buffer != '=') {
      buffer++;
      j++;
    }
    buffer++;
    switch (i) {
    case 0:
      globalParamSupermercato->K_cassieri = atoi(buffer);
      break;
    case 1:
      globalParamSupermercato->C_clienti = atoi(buffer);
      break;
    case 2:
      globalParamSupermercato->E_min = atoi(buffer);
      break;
    case 3:
      globalParamSupermercato->T_buyTimeCliente = atoi(buffer);
      break;
    case 4:
      globalParamSupermercato->P_maxProdCliente = atoi(buffer);
      break;
    case 5:
      globalParamSupermercato->S_timeElabCassa = atoi(buffer);
      break;
    case 6:
      globalParamSupermercato->S1 = atoi(buffer);
      break;
    case 7:
      globalParamSupermercato->S2 = atoi(buffer);
      break;
    case 8:
      globalParamSupermercato->startCasse = atoi(buffer);
      break;
    case 9:
      globalParamSupermercato->timeNotifica = atoi(buffer);
      break;

    default:
      break;
    }
    i++;
    buffer = cpy;
  }

  if ((control = confcheck(globalParamSupermercato)) == -1) {
    fclose(fd);
    free(globalParamSupermercato);
    free(buffer);
    return NULL;
  }
  free(buffer);
  fclose(fd);

  return globalParamSupermercato;
}

void printconf(config configvalues) {
  printf("%d %d %d %d %d %d %d %d %d %d\n", configvalues.K_cassieri,
         configvalues.C_clienti, configvalues.E_min,
         configvalues.T_buyTimeCliente, configvalues.P_maxProdCliente,
         configvalues.S_timeElabCassa, configvalues.S1, configvalues.S2,
         configvalues.startCasse, configvalues.timeNotifica);
}

// === DIRETTORE === //

// === CASSE === //

void *updateDirT(void *arg) {
  // Recupero l'ID della cassa a cui è associato il thread
  int IDcassa = (int)(intptr_t)arg;

  // Variabile Booleana che mi serve per decidere se mandare o meno l'update al
  // direttore.
  int cassaChiusa = 0;

  struct timespec timeThenUpdate = {
      (globalParamSupermercato->timeNotifica / 1000),
      (globalParamSupermercato->timeNotifica % 1000) * 1000};
  while (1) {

    // Controllo che nel frattempo non mi abbiano chiuso la cassa
    pthread_mutex_lock(&MChiudiCassa[IDcassa]);
    if (aChiudiCassa[IDcassa] == 1)
      cassaChiusa = 1;
    pthread_mutex_unlock(&MChiudiCassa[IDcassa]);

    // Se non ho ancora chiuso la cassa allora aspetto timeNotifica millisecondi
    if (cassaChiusa != 1) {
      nanosleep(&timeThenUpdate, NULL);
    }
    // Controllo che nel frattempo non mi abbiano chiuso la cassa
    pthread_mutex_lock(&MChiudiCassa[IDcassa]);
    if (aChiudiCassa[IDcassa] == 1)
      cassaChiusa = 1;
    pthread_mutex_unlock(&MChiudiCassa[IDcassa]);

    // Se la cassa non è chiusa e non ho ricevuto un segnale di chiusura del
    // supermercato
    if (cassaChiusa != 1 && sig_QUIT == 0 && sig_HUP == 0) {

      // Aggiorno la lunghezza della coda per la Cassa IDcassa
      pthread_mutex_lock(&MlengthCode[IDcassa]);
      pthread_mutex_lock(&McodaClienti[IDcassa]);
      lenghtCode[IDcassa] = codaCassa[IDcassa]->length;
      fflush(stdout);
      pthread_mutex_unlock(&McodaClienti[IDcassa]);
      pthread_mutex_unlock(&MlengthCode[IDcassa]);

      // Setto a 1 la posizione dell'array in modo da avvertire il direttore che
      // la cassa IDcassa ha aggiornato le sue informazioni
      pthread_mutex_lock(&MupdateCasse);
      pthread_mutex_lock(&McassaUpdateInfo[IDcassa]);
      aUpdateCassa[IDcassa] = 1;
      pthread_mutex_unlock(&McassaUpdateInfo[IDcassa]);

      // Controllo che nel frattempo non mi abbiano chiuso la cassa
      // Se non è chiusa avverto il direttore con una signal.
      pthread_mutex_lock(&MChiudiCassa[IDcassa]);
      if (aChiudiCassa[IDcassa] == 1)
        cassaChiusa = 1;
      if (cassaChiusa != 1)
        pthread_cond_signal(&CupdateCasse);
      pthread_mutex_unlock(&MChiudiCassa[IDcassa]);

      pthread_mutex_unlock(&MupdateCasse);
    } else {
      // Controllo se sono uscito per colpa di un segnale al supermercato e nel
      // caso avverto il thread che gestisce le casse e le fa uscire.
      if (sig_QUIT == 1 || sig_HUP == 1) {
        pthread_mutex_lock(&MupdateCasse);
        pthread_cond_signal(&CupdateCasse);
        pthread_mutex_unlock(&MupdateCasse);
      }
      pthread_exit(NULL);
    }
  }
  pthread_exit(NULL);
}

void *mainCassa(void *arg) {
  struct timespec timeCreazione;
  struct timespec timeChiusura;
  clock_gettime(CLOCK_REALTIME, &timeCreazione);
  // Mi segno il timestamp di creazione della Cassa

  // Recupero la cassa che mi sono passato come argomento e creo il seed per la
  // scelta random del tempo di elaborazione
  infoCassa *thisCassa = ((infoCassa *)arg);
  thisCassa->HBO = 1;
  unsigned int seed = thisCassa->IDcassa + globalTime;
  thisCassa->tempoElabProdotto = rand_r(&seed) % 80 + 20;

  // index della Coda relativa a questa Cassa
  int indexCoda = thisCassa->IDcassa - 1;

  long timeElabCassa;
  // Variabile decisionale per chiudere o meno la cassa.
  // {0} => aperta {1} => Chiudi
  int chiudiCassa = 0;

  // Setto la coda come aperta.
  pthread_mutex_lock(&McodaClienti[indexCoda]);
  codaCassa[indexCoda]->aperta = 1;
  pthread_mutex_unlock(&McodaClienti[indexCoda]);

  // Setto il Thread che manda gli update al direttore
  pthread_t threadUpdateDir;
  if (pthread_create(&threadUpdateDir, NULL, updateDirT,
                     (void *)(intptr_t)(indexCoda)) != 0) {
    fprintf(stderr, "threadUpdateDir %d: Creazione del Thread Fallita",
            indexCoda);
    exit(EXIT_FAILURE);
  }
  // Core-code della cassa.
  while (1) {

    // Controllo lo stato della coda della Cassa
    pthread_mutex_lock(&McodaClienti[indexCoda]);
    while (codaCassa[indexCoda]->length == 0 && chiudiCassa == 0) {
      // Controllo di non aver ricevuto nessun segnale di chiusura (supermercato
      // o singola cassa)
      if (sig_QUIT == 1)
        chiudiCassa = 1;
      if (sigUPCassaExit == 1)
        chiudiCassa = 1;
      pthread_mutex_lock(&MChiudiCassa[indexCoda]);
      if (aChiudiCassa[indexCoda] == 1)
        chiudiCassa = 1;
      pthread_mutex_unlock(&MChiudiCassa[indexCoda]);

      // Se non devo chiudere la cassa e la coda era semplicemente vuota allora
      // aspetto
      if (chiudiCassa != 1) {
        pthread_cond_wait(&CcodaClientiNotEmpty[indexCoda],
                          &McodaClienti[indexCoda]);
      }

      // Rieseguo i controlli, nel caso sia cambiato qualcosa e il controllo del
      // while risulti negativo.
      if (sig_QUIT == 1)
        chiudiCassa = 1;
      if (sigUPCassaExit == 1)
        chiudiCassa = 1;
      pthread_mutex_lock(&MChiudiCassa[indexCoda]);
      if (aChiudiCassa[indexCoda] == 1)
        chiudiCassa = 1;
      pthread_mutex_unlock(&MChiudiCassa[indexCoda]);
    }

    // Se non sono chiuso recupero il cliente
    if (chiudiCassa != 1) {
      Cliente *cliente = removecustomer(&codaCassa[indexCoda], indexCoda);

      pthread_mutex_unlock(&McodaClienti[indexCoda]);

      // Tempo di elaborazione per il cliente corrente.
      timeElabCassa =
          thisCassa->tempoElabProdotto +
          (globalParamSupermercato->S_timeElabCassa * cliente->ProdComprati);
      struct timespec tElab = {(timeElabCassa / 1000),
                               ((timeElabCassa % 1000) * 1000)};
      nanosleep(&tElab, NULL);

      // Aggiorno i valori della cassa  e segnalo al cliente che può uscire.
      thisCassa->prodElaborati += cliente->ProdComprati;
      thisCassa->clientiProcessati++;
      thisCassa->tempoServizio = thisCassa->tempoServizio +
                                 ((timeElabCassa - (thisCassa->tempoServizio)) /
                                  thisCassa->clientiProcessati);
      cliente->uscitaCoda = 1;
      pthread_mutex_lock(&McodaClienti[indexCoda]);
      pthread_cond_broadcast(&CcodaClienti[indexCoda]);

      // Controllo se devo chiudere la cassa.
      pthread_mutex_lock(&MChiudiCassa[indexCoda]);
      if (aChiudiCassa[indexCoda] == 1)
        chiudiCassa = 1;
      pthread_mutex_unlock(&MChiudiCassa[indexCoda]);
    }

    // Controllo se la cassa è stata chiusa o se ho ricevuto un sig_QUIT
    if (chiudiCassa == 1 || sig_QUIT == 1) {
      // aggiorno lo stato della coda
      codaCassa[indexCoda]->aperta = 0;
      pthread_mutex_unlock(&McodaClienti[indexCoda]);

      // Aspetto la fine del thread di update
      if (pthread_join(threadUpdateDir, NULL) == -1) {
        fprintf(stderr, "DirectorSMControl: thread join, failed!");
      }

      pthread_mutex_lock(&MChiudiCassa[indexCoda]);
      aChiudiCassa[indexCoda] = 0;
      pthread_mutex_unlock(&MChiudiCassa[indexCoda]);

      pthread_mutex_lock(&McodaClienti[indexCoda]);

      pthread_cond_broadcast(&CcodaClienti[indexCoda]);
      resetQueue(&codaCassa[indexCoda], indexCoda);

      pthread_mutex_unlock(&McodaClienti[indexCoda]);

      clock_gettime(CLOCK_REALTIME, &timeChiusura);

      thisCassa->tempoOpen +=
          msecond_timespec_diff(&timeCreazione, &timeChiusura);
      thisCassa->Nchiusure++;
      return NULL;
    }
    pthread_mutex_unlock(&McodaClienti[indexCoda]);
  }
}

// === CLIENTE === //

void *mainCliente(void *arg) {

  struct timespec timeEntrata;
  struct timespec timeUscita;
  struct timespec timeInsideCoda;
  struct timespec timeExitCoda;

  // Recupero il timestamp di quando il cliente entra nel Supermercato
  clock_gettime(CLOCK_REALTIME, &timeEntrata);

  // Inizializzo le informazioni del Cliente e il suo seed personale (Per la
  // scelta random della cassa)
  int IDCliente = (int)(intptr_t)(arg);
  Cliente *cliente = malloc(sizeof(Cliente));
  initCliente(cliente, IDCliente);
  unsigned int seed = cliente->IDcliente + globalTime;

  // Alcune variabili di controllo per il thread.
  long timeProdotti; // Tempo che il cliente impiega ad acquistare i prodotti
  int numCodaScelta;
  int codaOK;
  int cambiaCoda = 0;
  // int change = 0;
  // Controls if the Cliente is switching supermarket checkout or not

  cliente->ProdComprati =
      rand_r(&seed) % (globalParamSupermercato->P_maxProdCliente);

  // Genera un valore compreso tra 10 e (T_buyTimeCliente + 10), e poi lo
  // moltiplica per il numero di prodotti acquistati. Se acquista 0 prodotti la
  // sleep non avviene
  timeProdotti =
      (rand_r(&seed) % (globalParamSupermercato->T_buyTimeCliente) + 10) *
      cliente->ProdComprati;
  struct timespec tProd = {(timeProdotti / 1000),
                           ((timeProdotti % 1000) * 1000)};
  nanosleep(&tProd, NULL);

  // Caso in cui ho almeno un prodotto
  if ((cliente->ProdComprati) != 0) {
    // Prendo il timestamp di quando incomincia la sua vita in coda.
    clock_gettime(CLOCK_REALTIME, &timeInsideCoda);
    do {
      // Resetto i valori decisionali.
      cambiaCoda = 0;
      codaOK = 0;
      do {
        // Recupero un valore random per la coda e controllo se è aperta.
        // In caso positivo allora la marchio come OK e vado avanti.
        numCodaScelta = rand_r(&seed) % (globalParamSupermercato->K_cassieri);
        pthread_mutex_lock(&McodaClienti[numCodaScelta]);
        if (codaCassa[numCodaScelta]->aperta != 0)
          codaOK = 1;
        pthread_mutex_unlock(&McodaClienti[numCodaScelta]);
        if (sig_QUIT == 1)
          break;
      } while (codaOK == 0);

      // Controllo se ho ricevuto il segnale di Hard-Quit
      if (sig_QUIT != 1) {
        pthread_mutex_lock(&McodaClienti[numCodaScelta]);
        cliente->nCodeScelte++;
        // Incremento il contatore delle code scelte dal Cliente
        if ((joinqueue(&codaCassa[numCodaScelta], &cliente, numCodaScelta)) ==
            -1) {
          fprintf(stderr, "[mainCliente] joinqueue Fallita\n");
          exit(EXIT_FAILURE);
        }

        // Segnala alla cassa che c'è un nuovo cliente in coda.
        pthread_cond_signal(&CcodaClientiNotEmpty[numCodaScelta]);

        // Finchè il cliente non deve cambiare coda o non è uscito aspetto.
        while ((cliente->uscitaCoda) == 0 && cambiaCoda == 0) {
          // Se nel frattempo la coda è stata chiusa.
          if (codaCassa[numCodaScelta]->aperta == 0)
            cambiaCoda = 1;
          pthread_cond_wait(&CcodaClienti[numCodaScelta],
                            &McodaClienti[numCodaScelta]);
        }

        pthread_mutex_unlock(&McodaClienti[numCodaScelta]);
        // Controllo se ho ricevuto il segnale di Hard-Quit
        if (sig_QUIT == 1) {
          cliente->ProdComprati = 0;
          break;
        }
        // Come sopra, Ho ricevuot un segnale di hard-quit
      } else {
        cliente->ProdComprati = 0;
        break;
      }

    } while (cliente->uscitaCoda == 0);
    // Recupero il tempo di uscita del cliente, sia che abbia pagato sia che sia
    // stato forzato ad uscire.
    clock_gettime(CLOCK_REALTIME, &timeExitCoda);
  }

  // Entro nella coda del Direttore per l'approvazione ad uscire.
  pthread_mutex_lock(&McodaDirettore);
  if ((joinqueue(&codaDirettore, &cliente, -1)) == -1) {
    fprintf(stderr, "malloc failed\n");
  }

  // Segnalo al Direttore che ha qualcuno in coda e aspetto la sua conferma.
  pthread_cond_signal(&CcodaDirettoreNotEmpty);
  while (cliente->possoUscire != 1) {
    pthread_cond_wait(&CcodaDirettoreClienteEsce, &McodaDirettore);
  }
  pthread_mutex_unlock(&McodaDirettore);
  if (cliente->uscitaCoda != 1)
    cliente->ProdComprati = 0;

  // timestamp dell'uscita dal supermercato.
  clock_gettime(CLOCK_REALTIME, &timeUscita);

  cliente->tempoInside = msecond_timespec_diff(&timeEntrata, &timeUscita);
  cliente->tempoCoda = msecond_timespec_diff(&timeInsideCoda, &timeExitCoda);
  fflush(stdout);
  pthread_mutex_lock(&Mfile);
  fprintf(fileLog,
          "Cliente -> | id Cliente:%d | n. bought products:%d | time in the "
          "supermarket: %0.3f s | time in queue: %0.3f s | n. queues checked: "
          "%d | \n",
          cliente->IDcliente, cliente->ProdComprati,
          (double)cliente->tempoInside / (pow(10, 6)),
          (double)cliente->tempoCoda / (pow(10, 6)), cliente->nCodeScelte);
  fflush(stdout);
  pthread_mutex_unlock(&Mfile);

  free(cliente);
  return NULL;
}

// === UTILITY === //
void SetSignalHandler() {
  struct sigaction sigAct;
  memset(&sigAct, 0, sizeof(sigAct));
  sigAct.sa_handler = SignalHandeler;

  if (sigaction(SIGHUP, &sigAct, NULL) == -1) {
    fprintf(stderr, "Handler error");
  }
  if (sigaction(SIGQUIT, &sigAct, NULL) == -1) {
    fprintf(stderr, "Handler error");
  }
}

static void SignalHandeler(int SignalNumber) {
  switch (SignalNumber) {
  case SIGHUP:
    sig_HUP = 1;
    fprintf(stdout, "[SignalHandeler] SIGHUP ricevuto\n\t\t\tIl supermercato "
                    "sta per chiudere.\n");
    fflush(stdout);
    break;
  case SIGQUIT:
    sig_QUIT = 1;
    fprintf(stdout, "[SignalHandeler] SIGQUIT ricevuto\n\t\t\tIl "
                    "supermermercato è stato chiuso malamente.\n");
    fflush(stdout);
    break;
  default:
    fprintf(stderr,
            "[SignalHandeler] Errore nella SignalHandeler, segnale %d non "
            "riconosciuto.\n",
            SignalNumber);
    break;
  }
}

long msecond_timespec_diff(struct timespec *start, struct timespec *end) {
  return (end->tv_sec - start->tv_sec) * 1000000 +
         (end->tv_nsec - start->tv_nsec) / 1000;
}