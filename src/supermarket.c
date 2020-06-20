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

// === CLIENTE === //

void *customerT(void *arg);

int confcheck(config *globalParamSupermercato);
config *test(const char *configfile);
// void printconf(config configvalues);

void CreateQueueManagement();
void QueueFree();

void *smcheckout(void *arg) { // Cashiers

  struct timespec spec;
  struct timespec spec2;
  long time1, time2;

  clock_gettime(CLOCK_REALTIME, &spec); // Timestamp of the smcheckout open
  time1 = (spec.tv_sec) * 1000 + (spec.tv_nsec) / 1000000;

  infoCassa *smdata = ((infoCassa *)arg);
  smdata->HBO = 1;
  int IDcassa = smdata->IDcassa - 1;
  unsigned int seed = smdata->IDcassa + globalTime; // Creating seed
  long randomtime, servicetime;
  int exittime = 0; // If the cashier has to close the smcheckout

  pthread_mutex_lock(&McodaClienti[IDcassa]);
  codaCassa[IDcassa]->queueopen = 1;
  pthread_mutex_unlock(&McodaClienti[IDcassa]);

  if (smdata->tempoElabProdotto == 0) {
    while ((randomtime = rand_r(&seed) % 80) < 20)
      ; // Random number of time interval: 20-80
    smdata->tempoElabProdotto = randomtime;
  }

  pthread_t clock;
  if (pthread_create(&clock, NULL, updateDirT, (void *)(intptr_t)(IDcassa)) !=
      0) {
    fprintf(stderr, "clock %d: thread creation, failed!", IDcassa);
    exit(EXIT_FAILURE);
  }

  while (1) {
    pthread_mutex_lock(&McodaClienti[IDcassa]);
    while (codaCassa[IDcassa]->length == 0 && exittime == 0) {
      if (sig_QUIT == 1)
        exittime = 1;
      if (sigUPCassaExit == 1)
        exittime = 1;
      pthread_mutex_lock(&MChiudiCassa[IDcassa]);
      if (aChiudiCassa[IDcassa] == 1)
        exittime = 1;
      pthread_mutex_unlock(&MChiudiCassa[IDcassa]);
      if (exittime != 1) {
        pthread_cond_wait(&CcodaClientiNotEmpty[IDcassa],
                          &McodaClienti[IDcassa]);
      }
      if (sig_QUIT == 1)
        exittime = 1;
      if (sigUPCassaExit == 1)
        exittime = 1;
      pthread_mutex_lock(&MChiudiCassa[IDcassa]);
      if (aChiudiCassa[IDcassa] == 1)
        exittime = 1;
      pthread_mutex_unlock(&MChiudiCassa[IDcassa]);
    } // Wait until the queue is empty or the queue has to close
    if (exittime != 1) {
      Cliente *qcs = removecustomer(
          &codaCassa[IDcassa],
          IDcassa); // Serves the customer that is the first in the queue

      pthread_mutex_unlock(&McodaClienti[IDcassa]);
      // Number of time to scan the product and let the customer pay

      servicetime =
          smdata->tempoElabProdotto +
          (globalParamSupermercato->S_timeElabCassa * qcs->ProdComprati);
      struct timespec t = {(servicetime / 1000),
                           ((servicetime % 1000) * 1000000)};
      nanosleep(&t, NULL); // Sleeping randomtime mseconds

      // Signal to the customer that the cashier has done
      smdata->prodElaborati += qcs->ProdComprati;
      smdata->clientiProcessati++;
      // printf("%f",((smdata->servicetime*smdata->clientiProcessati)+servicetime)/smdata->clientiProcessati++);
      // printf("%ld \n",servicetime);
      smdata->tempoServizio =
          smdata->tempoServizio +
          ((servicetime - (smdata->tempoServizio)) / smdata->clientiProcessati);
      qcs->uscitaCoda = 1;
      pthread_mutex_lock(&McodaClienti[IDcassa]);
      pthread_cond_broadcast(&CcodaClienti[IDcassa]);

      pthread_mutex_lock(&MChiudiCassa[IDcassa]);
      if (aChiudiCassa[IDcassa] == 1)
        exittime = 1;
      pthread_mutex_unlock(&MChiudiCassa[IDcassa]);
    }
    if (exittime == 1 || sig_QUIT == 1) {
      codaCassa[IDcassa]->queueopen = 0;
      pthread_mutex_unlock(&McodaClienti[IDcassa]);
      if (pthread_join(clock, NULL) == -1) {
        fprintf(stderr, "DirectorSMControl: thread join, failed!");
      }

      pthread_mutex_lock(&MChiudiCassa[IDcassa]);
      aChiudiCassa[IDcassa] = 0;
      pthread_mutex_unlock(&MChiudiCassa[IDcassa]);

      pthread_mutex_lock(&McodaClienti[IDcassa]);

      pthread_cond_broadcast(&CcodaClienti[IDcassa]);
      resetQueue(&codaCassa[IDcassa], IDcassa);

      pthread_mutex_unlock(&McodaClienti[IDcassa]);

      clock_gettime(CLOCK_REALTIME, &spec2);
      time2 = (spec2.tv_sec) * 1000 + (spec2.tv_nsec) / 1000000;
      smdata->tempoOpen += time2 - time1;
      smdata->Nchiusure++;
      return NULL;
    }
    pthread_mutex_unlock(&McodaClienti[IDcassa]);
  }
}

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
    codaCassa[i]->queueopen = 1;
    pthread_mutex_unlock(&McodaClienti[i]);
    if (pthread_create(&smchecks[i], NULL, smcheckout,
                       &((infoCassa *)arg)[i]) != 0) {
      fprintf(stderr, "infoCassa %d: thread creation, failed!", i);
      exit(EXIT_FAILURE);
    }
  }

  int check;   // If queueopen and number of cashier that have updated are equal
               // -> Check if close or open a smcheckout
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
          if (codaCassa[i]->queueopen == 1 && aChiudiCassa[i] != 1)
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
                   // equal -> Check if close or open a smcheckout

        if (check != 0) { // Director checks the queues lengths to decide if
                          // close or open some sm checkouts
          for (int i = 0; i < globalParamSupermercato->K_cassieri; i++) {
            pthread_mutex_lock(&MlengthCode[i]);
            pthread_mutex_lock(&McodaClienti[i]);
            if (lenghtCode[i] <= 1 && codaCassa[i]->queueopen == 1)
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
          if (lenghtCode[j] <= 1 &&
              codaCassa[j]->queueopen ==
                  1) { // Check if the jth queue matches the conditions to close
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
          if (codaCassa[i]->queueopen == 0 && index == -1 &&
              aChiudiCassa[i] != 1) {
            index = i;
          } // if the ith queue matches the codntions to open take the index
          pthread_mutex_unlock(&MChiudiCassa[i]);
          pthread_mutex_unlock(&McodaClienti[i]);
        }
        if (index != -1) {
          pthread_mutex_lock(&McodaClienti[index]);
          codaCassa[index]->queueopen = 1; // Set queue open
          pthread_mutex_unlock(&McodaClienti[index]);
          if (pthread_create(&smchecks[index], NULL, smcheckout,
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
    if (pthread_create(&cs[i], NULL, customerT, (void *)(intptr_t)i) != 0) {
      fprintf(stderr, "customerT %d: thread creation, failed!", i);
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
      Cliente *qcs = removecustomer(&codaDirettore, -1);
      qcs->possoUscire = 1;
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
        if (pthread_create(&cs[j + i], NULL, customerT,
                           (void *)(intptr_t)i + j) != 0) {
          fprintf(stderr, "customerT %d: thread creation, failed!", j + i);
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

void SetSignalHandler();
static void SignalHandeler(int SignalNumber);

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
            Casse[i].clientiProcessati, (double)Casse[i].tempoOpen / 1000,
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
      0, (globalParamSupermercato->timeNotifica * 1000)};
  // 0s + timeNotifica*10³ ns = timeNotifica ms
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

// === CLIENTE === //

void *customerT(void *arg) {

  struct timespec timeEntrata;
  struct timespec timeUscita;
  struct timespec timeInsideCoda;
  struct timespec timeExitCoda;
  long timeTotMS, timeCodaMS, time3 = -1, time4 = -1;

  clock_gettime(CLOCK_REALTIME,
                &timeEntrata); // Timestamp when customer enters the supermarket
  timeTotMS = (timeEntrata.tv_sec) * 1000 + (timeEntrata.tv_nsec) / 1000000;

  Cliente *cs = malloc(sizeof(Cliente));          // Cliente data
  setupcs(cs, (int)(intptr_t)(arg));              // Set up struct
  unsigned int seed = cs->IDcliente + globalTime; // Creating seed
  long randomtime;     // Random tempoOpen to buy products
  int nqueue;          // Queue chosen
  int check;           // If chosen a valid queue
  int changequeue = 0; // If the queue has been closed and need to change queue
  int change =
      0; // Controls if the Cliente is switching supermarket checkout or not
  // Cliente's buy tempoOpen
  cs->ProdComprati =
      rand_r(&seed) % (globalParamSupermercato->P_maxProdCliente);
  // Random number of products: 0<nproducts<=P
  while ((randomtime =
              rand_r(&seed) % (globalParamSupermercato->T_buyTimeCliente)) < 10)
    ; // Random number of buy tempoOpen
  struct timespec t = {(randomtime / 1000), ((randomtime % 1000) * 1000000)};
  nanosleep(&t, NULL); // Sleeping randomtime mseconds
  if ((cs->ProdComprati) != 0) {
    do {
      changequeue = 0; // Reset change queue value
      check = 0;
      do {
        nqueue = rand_r(&seed) %
                 (globalParamSupermercato->K_cassieri); // Random queue number
        pthread_mutex_lock(&McodaClienti[nqueue]);
        if (codaCassa[nqueue]->queueopen != 0)
          check = 1; // Check if the queue is open
        pthread_mutex_unlock(&McodaClienti[nqueue]);
        if (sig_QUIT == 1)
          break;
      } while (check == 0);
      if (sig_QUIT != 1) {
        pthread_mutex_lock(&McodaClienti[nqueue]);
        if (change == 0) {
          cs->nCodeScelte = 1;
        } // First tempoOpen that the cs is in a queue
        else {
          cs->nCodeScelte++;
        } // Not the first tempoOpen that the cs is in a queue
        fflush(stdout);

        clock_gettime(CLOCK_REALTIME,
                      &timeInsideCoda); // timestamp for calculating tempoOpen
                                        // waited in queue
        time3 =
            (timeInsideCoda.tv_sec) * 1000 + (timeInsideCoda.tv_nsec) / 1000000;
        if ((joinqueue(&codaCassa[nqueue], &cs, nqueue)) ==
            -1) { // Joining the queue chosen
          fprintf(stderr, "malloc failed\n");
          exit(EXIT_FAILURE);
        }
        pthread_cond_signal(
            &CcodaClientiNotEmpty[nqueue]); // Signal to the queue to warn
                                            // that a new Cliente is in queue
        while ((cs->uscitaCoda) == 0 &&
               changequeue == 0) { // While the Cliente hasnt paid or needs to
                                   // change queue 'cause it has been closed
          pthread_cond_wait(&CcodaClienti[nqueue], &McodaClienti[nqueue]);
          if (codaCassa[nqueue]->queueopen == 0)
            changequeue = 1; // If the Cliente has been waken and he hasn't done
                             // the queue --> sm closed and changes the queue
        }

        // DA IMPLEMENTARE: SE SIGQUIT SETTATO -> THREAD EXIT
        pthread_mutex_unlock(&McodaClienti[nqueue]);
        change++;
      } else
        break;
    } while (cs->uscitaCoda == 0);
    if (time3 != -1)
      clock_gettime(CLOCK_REALTIME,
                    &timeExitCoda); // Get timestamp when Cliente has paid
    time4 = (timeExitCoda.tv_sec) * 1000 + (timeExitCoda.tv_nsec) / 1000000;
  }

  pthread_mutex_lock(&McodaDirettore);
  if ((joinqueue(&codaDirettore, &cs, -1)) == -1) {
    fprintf(stderr, "malloc failed\n");
  }
  pthread_cond_signal(&CcodaDirettoreNotEmpty); // Signal to the director that
                                                // someone wants to exit
  while (cs->possoUscire != 1) {
    pthread_cond_wait(&CcodaDirettoreClienteEsce, &McodaDirettore);
  } // While director doesnt allow the exit --> wait
  pthread_mutex_unlock(&McodaDirettore);
  if (cs->uscitaCoda != 1)
    cs->ProdComprati = 0;

  clock_gettime(CLOCK_REALTIME,
                &timeUscita); // Timestamp of the exit from the supermarket
  timeCodaMS = (timeUscita.tv_sec) * 1000 + (timeUscita.tv_nsec) / 1000000;
  cs->tempoInside = timeCodaMS - timeTotMS; // Time passed in the supermarket
  if (time3 != -1 && time4 != -1)
    cs->tempoCoda = time4 - time3; // Time passed in queue

  pthread_mutex_lock(&Mfile);
  fprintf(fileLog,
          "Cliente -> | id Cliente:%d | n. bought products:%d | time in the "
          "supermarket: %0.3f s | time in queue: %0.3f s | n. queues checked: "
          "%d | \n",
          cs->IDcliente, cs->ProdComprati, (double)cs->tempoInside / 1000,
          (double)cs->tempoCoda / 1000, cs->nCodeScelte);
  pthread_mutex_unlock(&Mfile);

  free(cs);
  return NULL;
}
