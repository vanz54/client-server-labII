// Autore -> Tommaso Vanz
#define _GNU_SOURCE
#include "rwunfair.h"
#include "xerrori.h"
#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#include <search.h>
#include <semaphore.h>
#include <signal.h>
#include <stdatomic.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <unistd.h>
// Costanti utilizzate in archivio
#define QUI __LINE__, __FILE__
#define Num_elem 1000000
#define PC_buffer_len 10
#define Max_sequence_length 2048

// Struct che serve per la lista di ENTRYs: ENTRY ha un attributo 'key' che è la
// stringa relativa alla ENTRY e un attributo 'data' che è la struct qui sotto,
// ossia una coppia "valore" che indica le occorrenze di 'key' nella ht e un
// "next" che è un puntatore alla prossima ENTRY (vedi disegno per chiarimenti)
typedef struct {
  int valore;  // Numero di occorrenze della stringa
  ENTRY *next; // Puntatore alla prossima entry
} coppia;

// Variabile globale per memorizzare la testa della lista delle entry
ENTRY *testa_lista_entry = NULL;

// ------ Funzione di stampa delle della lista ------
void stampa_entry(ENTRY *e) {
  coppia *c = (coppia *)e->data;
  printf("%s ----------- %d\n", e->key, c->valore);
} // Non serve stampare il campo next

void stampa_lista_entry(ENTRY *lis) {
  if (lis == NULL) {
    printf("Lista vuota\n");
  }
  // In input do il puntatore al primo elemento che chiamo lis
  while (lis != NULL) {
    coppia *c = (coppia *)lis->data;
    stampa_entry(lis);
    lis = c->next;
  }
}

// ------ Funzione di distruzione delle entry fornita dal professore ------
void distruggi_entry(ENTRY *e) {
  free(e->key);
  free(e->data);
  free(e);
}

// ------ Funzione di distruzione della ht ------
void hash_distruggi(ENTRY *lis) {
  if (lis != NULL) {
    coppia *c = lis->data;
    hash_distruggi(c->next); // Prima distrugge il resto
    // printf("%s\n",lis->key);
    distruggi_entry(lis);
  }
}

// Creo la struct globalmente per la gestione della concorrenza della hash table
// che farò attraverso lo schema lettori-scrittori visto a lezione, uso le
// stesse funzioni fatte dal professore a lezione che si trovano nel file
// rwunfair
rwHT struct_rwHT;

// Creo la variabile globale per memorizzare il numero di stringhe inserite
// nella hash table Serve che sia volatile perché nell'handler non è garantito
// l'accesso safe alle variabili globali
atomic_int tot_stringhe_inHT = 0;

// ------ Funzione che crea un oggetto di tipo ENTRY con chiave s e valore n e
// next==NULL, fornito dal professore ------
ENTRY *crea_entry(char *s, int n) {
  ENTRY *e = malloc(sizeof(ENTRY));
  if (e == NULL)
    xtermina("[ARCHIVIO] Errore 1 malloc crea_entry", QUI);
  e->key = strdup(s); // Salva copia di s
  e->data = malloc(sizeof(coppia));
  if (e->key == NULL || e->data == NULL)
    xtermina("[ARCHIVIO] Errore 2 malloc crea_entry", QUI);
  // Inizializzo coppia
  coppia *c = (coppia *)e->data; // Cast obbligatorio
  c->valore = n;
  c->next = NULL;
  return e;
}

// $$$$$$$$$$$$  AGGIUNGI  $$$$$$$$$$$$
// ------ Funzione chiamata dagli scrittori che aggiungono gli elementi sulla
// hash table ------
void aggiungi(char *s) {
  // printf("Thread scrittore %d aggiunge %s\n", gettid(), s);
  // Di base creo la entry e la cerco nella tabella
  ENTRY *e = crea_entry(s, 1);
  ENTRY *r = hsearch(*e, FIND);
  if (r == NULL) {          // Se la stringa è nuova nella ht
    r = hsearch(*e, ENTER); // Inserisco la entry creata nella ht
    if (r == NULL)
      xtermina("$-AGGIUNGI-$ Errore o tabella piena", QUI);
    // La metto anche in cima alla lista delle entry inserite
    coppia *c = (coppia *)e->data;
    // Salvo la vecchia lista dentro c->next
    c->next = testa_lista_entry;
    // e diventa la testa della lista
    testa_lista_entry = e;
    // Incremento anche il numero di stringhe totali distinte inserite nella ht
    tot_stringhe_inHT += 1;
  } else {
    // Altrimenti la stringa è già presente incremento solo il valore
    assert(strcmp(e->key, r->key) == 0);
    coppia *c = (coppia *)r->data;
    c->valore += 1;
    distruggi_entry(e); // Questa non la devo memorizzare
  }
}

// $$$$$$$$$$$$  CONTA  $$$$$$$$$$$$
// ------ Funzione chiamata dai lettori che conta le occorrenze di una relativa
// stringa sulla hash table, restituisce il numero di occorrenze della stringa s
// ------
int conta(char *s) {
  int tmp;
  // printf("Thread lettore %d conta %s\n", gettid(), s);
  ENTRY *e = crea_entry(s, 1);
  ENTRY *r = hsearch(*e, FIND);
  if (r == NULL) { // Se non c'è la stringa nella ht restituisco 0
    printf("%s -> error 404: not found in ht\n", s);
    tmp = 0;
  } else {
    printf("%s -> %d\n", s, *((int *)r->data));
    tmp = *((int *)r->data);
  }
  // Distruggo la entry 'creata' perché non va allocata
  distruggi_entry(e);
  return tmp;
}

// <  STRUCT CAPOLETTORE - LETTORI  >
typedef struct {
  pthread_cond_t *condCapoLet; // Condition variable capolettore
  pthread_cond_t *condLettori; // Condition variable lettori
  pthread_mutex_t
      *mutexLettori; // Mutex lettori associata alla condition variable lettori
  char **bufferLettori; // Buffer capolettore-lettori
  int next_inLettori;   // Indice successivo di inserimento nel buffer, non è
                        // condiviso in quanto c'è un solo 'produttore'
                        // (->capolettore)
  int *totinBufferLettori; // Numero di stringhe presenti nel buffer
                           // capolettore-lettori
  int *next_outLettori;    // Indice successivo di estrazione dal buffer
  int capoletPipe;  // Per ricezione dalla pipe gli passo la pipe via struct
  FILE *lettoriLog; // File di log dei lettori
} structLettori;

// (((((((((((((((((^^^  CAPOLETTORE  ^^^)))))))))))))))))
// ------ Funzione eseguita dal capolettore, legge dalla pipe capolet finché non
// è chiusa e inserisce le stringhe nel buffer che condivide con i threads
// lettori, quando il buffer è pieno sveglia i lettori e si mette in attesa,
// quando la pipe viene chiusa inserisce nel buffer 3X1T per far capire ai
// thread lettori di terminare ------
void *funzione_capolettore(void *arg) {
  structLettori *struct_lettori = (structLettori *)arg;
  int capolet = struct_lettori->capoletPipe;
  char **bufferLettori = struct_lettori->bufferLettori;
  int numberfromCapolet; // Inizializzo la lunghezza che riceverò da capolet
                         // prima di ogni sequenza

  // Alloco dinamicamente il buffer per la sequenza di caratteri letta dalla
  // pipe
  char *bufferfromCapolet = malloc(Max_sequence_length * sizeof(char));
  if (bufferfromCapolet == NULL)
    xtermina("[ARCHIVIO] Errore allocazione bufferfromCapolet\n", QUI);
  // Alloco dinamicamente il buffer per i delimitatori da usare con la
  // tokenizzazione
  char *delimitatoriLettori = malloc(9 * sizeof(char));
  if (bufferfromCapolet == NULL)
    xtermina("[ARCHIVIO] Errore allocazione delimitatori capolet\n", QUI);
  strcpy(delimitatoriLettori, ".,:; \n\r\t");
  // Inizializzo il terminatore con una stringa che mi ricorda l'uscita
  char *terminatore = "3X1T";

  while (1) {
    // Leggo la lunghezza dalla pipe
    ssize_t num_reads =
        read(capolet, &numberfromCapolet, sizeof(numberfromCapolet));
    // printf("(CAPOLETTORE) Intero : %d\n", numberfromCapolet);
    if (num_reads == -1) {
      xtermina("[CAPOLETTORE] Errore read capolet\n", QUI);
    }
    if (num_reads ==
        0) { // Se non leggo più nulla (0), la pipe è stata chiusa dal server
      int indicexit =
          struct_lettori->next_inLettori; // quindi il capo deve chiudersi
      for (int i = 0; i < 10;
           i++) { // Prima però riempio il buffer di valori terminatore
        bufferLettori[indicexit++ % PC_buffer_len] = terminatore;
        *(struct_lettori->totinBufferLettori) += 1;
        pthread_cond_signal(struct_lettori->condLettori);
      }
      // Dealloco i buffer utilizzati ed esco
      free(delimitatoriLettori);
      free(bufferfromCapolet);
      pthread_exit(NULL);
    }
    // Se ho letto una lunghezza, vuol dire che la pipe è ancora aperta e leggo
    // la sequenza dalla pipe, mettendoci \0 in fondo
    ssize_t bytes_read = read(capolet, bufferfromCapolet, numberfromCapolet);
    bufferfromCapolet[bytes_read] = 0;
    // printf("(CAPOLETTORE) Stringa : %s\n", bufferfromCapolet);

    // Tokenizzo la stringa ricevuta per metterla sul buffer che ho con i
    // lettori
    char *token = strtok(bufferfromCapolet, delimitatoriLettori);

    while (token != NULL) {
      xpthread_mutex_lock(struct_lettori->mutexLettori, QUI);
      while (*struct_lettori->totinBufferLettori ==
             10) { // Se il buffer è pieno attendo
        pthread_cond_wait(struct_lettori->condCapoLet,
                          struct_lettori->mutexLettori);
      }
      // Inserisco l'elemento nel buffer ciclico calcolandomi opportunamente
      // l'indice
      int indice = (struct_lettori->next_inLettori++ % PC_buffer_len);
      bufferLettori[indice] = strdup(token);
      *(struct_lettori->totinBufferLettori) += 1;
      // Una volta inserito l'elemento, sveglio i lettori e sblocco il mutex
      xpthread_mutex_unlock(struct_lettori->mutexLettori, QUI);
      pthread_cond_broadcast(struct_lettori->condLettori);

      // printf("(CAPOLETTORE) Riempio buffer[%d]\n", indice);
      token = strtok(NULL, delimitatoriLettori);
    }
    // Stampa buffer
    // for(int i = 0;i<PC_buffer_length;i++){
    // printf("(CAPOLETTORE) BUFFER[%d]: %s\n", i,
    // struct_lettori->bufferLettori[i]);
    //}
  }
}

// (((((((((((((((((  LETTORI  )))))))))))))))))
// ------ Funzione eseguita dai lettori, leggono dal buffer e fanno conta finché
// non vedono un valore di terminazione 3X1T, quando lo vedono terminano ------
void *funzione_lettori(void *arg) {
  // Inizializzo il terminatore con una stringa che mi ricorda l'uscita, la
  // struct in accordo con il capolettore e la struct globale per la concorrenza
  // della hash table
  char *terminatore = "3X1T";
  structLettori *struct_lettori = (structLettori *)arg;
  rwHT *rwHT = &struct_rwHT;
  char **bufferLettori = struct_lettori->bufferLettori;
  char *outputLettori;

  while (1) {
    xpthread_mutex_lock(struct_lettori->mutexLettori, QUI);
    while (*struct_lettori->totinBufferLettori ==
           0) { // Se il buffer è vuoto attendo
      pthread_cond_wait(struct_lettori->condLettori,
                        struct_lettori->mutexLettori);
    }

    // Leggo il buffer ciclico calcolandomi opportunamente l'indice in output,
    // che stavolta è 'condiviso' con gli altri lettori per permettere
    // concorrenza
    int indiceOutLettore = *(struct_lettori->next_outLettori) % PC_buffer_len;
    outputLettori = bufferLettori[indiceOutLettore];
    // printf("{LETTORE %lu} Leggo buffer[%d] -> %s\n", pthread_self(),
    // indiceOutLettore, bufferLettori[indiceOutLettore]);

    if (outputLettori == terminatore) { // Se leggo il terminatore, termino
      // printf("{LETTORE %lu} Termino\n", pthread_self());
      *(struct_lettori->totinBufferLettori) -= 1;
      // Sveglio gli altri lettori e termino
      pthread_cond_signal(struct_lettori->condCapoLet);
      xpthread_mutex_unlock(struct_lettori->mutexLettori, QUI);
      pthread_exit(NULL);
    }

    // Dopo aver letto l'elemento, diminuisco gli elementi nel buffer lettori e
    // mi preparo a contare le occorrenze della stringa appena letta nella hash
    // table, usando la concorrenza lettori-scrittori unfair
    *(struct_lettori->totinBufferLettori) -= 1;
    read_lock(rwHT);
    int occorrenze_outputLettori = conta(outputLettori);
    // Conto le occorrenze della stringa nella ht e le stampo su lettori.log
    fprintf(struct_lettori->lettoriLog, "%s %d\n", outputLettori,
            occorrenze_outputLettori);
    read_unlock(rwHT);
    *(struct_lettori->next_outLettori) +=
        1; // Incremento il puntatore di estrazione del buffer

    // Dealloco la stringa letta che era stata precedentemente duplicata
    free(struct_lettori->bufferLettori[indiceOutLettore]);

    pthread_cond_signal(struct_lettori->condCapoLet);
    xpthread_mutex_unlock(struct_lettori->mutexLettori, QUI);
  }
}

// <  STRUCT CAPOSCRITTORE - SCRITTORI  >
typedef struct {
  pthread_cond_t *condCapoSc;      // Condition variable caposcrittore
  pthread_cond_t *condScrittori;   // Condition variable scrittori
  pthread_mutex_t *mutexScrittori; // Mutex lettori associata alla condition
                                   // variable scrittori
  char **bufferScrittori;          // Buffer scrittori
  int next_inScrittori; // Indice successivo di inserimento nel buffer, non è
                        // condiviso perché caposcrittore è l'unico 'produttore'
  int *totinBufferScrittori;
  int *next_outScrittori; // Indice successivo di estrazione dal buffer
  int caposcPipe;         // Per ricezione dalla pipe
} structScrittori;

// (((((((((((((((((^^^  CAPOSCRITTORE  ^^^)))))))))))))))))
// ------ Funzione eseguita dal caposcrittore, legge dalla pipe caposc finché
// non è chiusa e inserisce le stringhe nel buffer che condivide con i threads
// scrittori, quando il buffer è pieno sveglia gli scrittori e si mette in
// attesa, quando la pipe viene chiusa inserisce nel buffer 3X1T per far capire
// ai thread scrittori di terminare ------ Analogo al comportamento di
// capolettore soltanto che si interfaccia coi thread scrittori
void *funzione_caposcrittore(void *arg) {
  structScrittori *struct_scrittori = (structScrittori *)arg;
  int caposc = struct_scrittori->caposcPipe;
  char **bufferScrittori = struct_scrittori->bufferScrittori;
  int numberfromCaposc; // Inizializzo la lunghezza che riceverò da capolet
                        // prima di ogni sequenza

  // Alloco dinamicamente il buffer per la sequenza di caratteri letta dalla
  // pipe
  char *bufferfromCaposc = malloc(Max_sequence_length * sizeof(char));
  if (bufferfromCaposc == NULL)
    xtermina("[ARCHIVIO] Errore allocazione bufferfromCaposc\n", QUI);
  // Alloco dinamicamente il buffer per i delimitatori da usare con strtok
  char *delimitatoriScrittori = malloc(9 * sizeof(char));
  if (delimitatoriScrittori == NULL)
    xtermina("[ARCHIVIO] Errore allocazione delimitatori caposc\n", QUI);
  strcpy(delimitatoriScrittori, ".,:; \n\r\t");
  // Inizializzo  il terminatore con una stringa scelta arbitrariamente
  char *terminatore = "3X1T";

  while (1) {
    // Leggo la lunghezza dalla pipe
    ssize_t num_reads =
        read(caposc, &numberfromCaposc, sizeof(numberfromCaposc));
    // printf("(CAPOSCRITTORE) Intero : %d\n", numberfromCaposc);
    if (num_reads == -1) {
      xtermina("(CAPOSCRITTORE) Errore read caposc\n", QUI);
    }
    if (num_reads == 0) { // Se la pipe è chiusa dal server riempio il buffer
                          // con gli scrittori di valori di terminazione
      int indicexit = struct_scrittori->next_inScrittori;
      for (int i = 0; i < 10; i++) {
        bufferScrittori[indicexit++ % PC_buffer_len] = terminatore;
        *(struct_scrittori->totinBufferScrittori) += 1;
        pthread_cond_signal(struct_scrittori->condScrittori);
      }
      // Sveglio gli scrittori e termino
      free(delimitatoriScrittori);
      free(bufferfromCaposc);
      pthread_exit(NULL);
    }

    // Leggo la stringa dalla pipe, ci metto 0 alla fine per evitare problemi
    // con strtok
    ssize_t bytes_read = read(caposc, bufferfromCaposc, numberfromCaposc);
    bufferfromCaposc[bytes_read] = 0;
    // printf("(CAPOSCRITTORE) Stringa : %s\n", bufferfromCaposc);

    // Tokenizzo la sequenza ricevuta per metterne i token sulla hash table
    char *token = strtok(bufferfromCaposc, delimitatoriScrittori);

    while (token != NULL) {
      xpthread_mutex_lock(struct_scrittori->mutexScrittori, QUI);
      while (*struct_scrittori->totinBufferScrittori ==
             10) { // Se il buffer è pieno aspetto
        pthread_cond_wait(struct_scrittori->condCapoSc,
                          struct_scrittori->mutexScrittori);
      }
      // Inserisco l'elemento nel buffer ciclico
      int indice = (struct_scrittori->next_inScrittori++ % PC_buffer_len);
      bufferScrittori[indice] = strdup(token);
      *(struct_scrittori->totinBufferScrittori) += 1;

      // Sveglio gli scrittori che magari erano in attesa sul buffer vuoto
      xpthread_mutex_unlock(struct_scrittori->mutexScrittori, QUI);
      pthread_cond_broadcast(struct_scrittori->condScrittori);

      // printf("(CAPOSCRITTORE) Riempio buffer[%d]\n", indice);
      token = strtok(NULL, delimitatoriScrittori);
    }
  }
}

// (((((((((((((((((  SCRITTORI  )))))))))))))))))
// ------ Funzione eseguita dagli scrittori, leggono dal buffer e fanno aggiungi
// finché non vedono un valore di terminazione 3X1T, quando lo vedono terminano
// ------
void *funzione_scrittori(void *arg) {
  // Inizializzo il terminatore con una stringa che mi ricorda l'uscita, la
  // struct in accordo con il capolettore e la struct globale per la concorrenza
  // della hash table
  char *terminatore = "3X1T";
  structScrittori *struct_scrittori = (structScrittori *)arg;
  rwHT *rwHT = &struct_rwHT;
  char **bufferScrittori = struct_scrittori->bufferScrittori;
  char *outputScrittori;

  while (1) {
    xpthread_mutex_lock(struct_scrittori->mutexScrittori, QUI);
    while (*struct_scrittori->totinBufferScrittori ==
           0) { // Se il buffer è vuoto aspetto
      pthread_cond_wait(struct_scrittori->condScrittori,
                        struct_scrittori->mutexScrittori);
    }

    int indiceOutScrittore =
        *(struct_scrittori->next_outScrittori) % PC_buffer_len;
    outputScrittori = bufferScrittori[indiceOutScrittore];

    // printf("{SCRITTORE %lu} Leggo buffer[%d] -> %s\n", pthread_self(),
    // indiceOutScrittore,
    // bufferScrittori[indiceOutScrittore]);
    // printf("%s",outputScrittori);

    if (outputScrittori == terminatore) { // Se trovo il terminatore termino
      // printf("{SCRITTORE %lu} Termino\n", pthread_self());
      *(struct_scrittori->totinBufferScrittori) -= 1;
      pthread_cond_signal(struct_scrittori->condCapoSc);
      xpthread_mutex_unlock(struct_scrittori->mutexScrittori, QUI);
      pthread_exit(NULL);
    }

    // Dopo aver letto l'elemento, diminuisco gli elementi nel buffer scrittori
    // e mi preparo a aggiungere la stringa appena letta dalla hash table,
    // usando la concorrenza lettori-scrittori unfair
    *(struct_scrittori->totinBufferScrittori) -= 1;
    write_lock(rwHT);
    aggiungi(outputScrittori);
    write_unlock(rwHT);
    *(struct_scrittori->next_outScrittori) += 1;

    // Dealloco la stringa letta che era stata precedentemente duplicata
    free(struct_scrittori->bufferScrittori[indiceOutScrittore]);

    pthread_cond_signal(struct_scrittori->condCapoSc);
    xpthread_mutex_unlock(struct_scrittori->mutexScrittori, QUI);
  }
}

// <  STRUCT GESTORE  >
typedef struct { // Passo al gestore i capi in modo che ne possa fare la join
  pthread_t *threadCapoLet;
  pthread_t *threadCapoSc;
} structSegnali;

// ______________ GESTORE SEGNALI ______________
// ------ Funzione eseguita dal gestore di segnali, gestisce SIGINT, SIGTERM e
// SIGUSR1 ------
void *funzione_gestore_segnali(void *arg) {
  // Inizializzo la maschera del thread gestore di segnali
  structSegnali *struct_segnali = (structSegnali *)arg;
  sigset_t maskSegnali;
  // Aggiungo i segnali che voglio gestire
  sigemptyset(&maskSegnali);
  sigaddset(&maskSegnali, SIGTERM);
  sigaddset(&maskSegnali, SIGINT);
  sigaddset(&maskSegnali, SIGUSR1);
  int s;
  while (true) {
    int e = sigwait(&maskSegnali, &s);
    if (e != 0)
      xtermina("<GESTORE> Errore sigwait\n", QUI);
    // printf("[THREAD GESTORE] svegliato dal segnale %d\n",s);
    if (s == SIGINT) { // se ricevo SIGINT
      fprintf(stderr, "[SIGINT] Numero di stringhe distinte nella ht -> %d\n",
              tot_stringhe_inHT);
      continue;
    } else if (s == SIGTERM) { // se ricevo SIGTERM (mandato dal server o
                               // direttamente da terminale)

      if (xpthread_join(*(struct_segnali->threadCapoLet), NULL, QUI) != 0)
        xtermina("[SIGTERM] Errore nell'attesa del thread capolettore\n", QUI);
      if (xpthread_join(*(struct_segnali->threadCapoSc), NULL, QUI) != 0)
        xtermina("[SIGTERM] Errore nell'attesa del thread caposcrittore\n",
                 QUI);
      fprintf(stdout, "[SIGTERM] Numero di stringhe distinte nella ht -> %d\n",
              tot_stringhe_inHT);

      // Dealloco tutta la hash map e gli elementi con la lista
      hash_distruggi(testa_lista_entry);
      hdestroy();
      pthread_exit(NULL);
    } else if (s == SIGUSR1) {
      printf("[SIGUSR1] Dealloco e reinizializzo la hash table\n");
      // Distruggo la hash table
      hash_distruggi(testa_lista_entry);
      hdestroy();
      // Ripristino la lista in modo da poter creare una nuova hash table e
      // associarcela, facendo andare avanti comunque il programma
      testa_lista_entry = NULL;
      tot_stringhe_inHT = 0;
      hcreate(Num_elem);
      // stampa_lista_entry(testa_lista_entry);
      continue;
    }
  }
  return NULL;
}

// ===========  MAIN ARCHIVIO  ===========
// Il main crea la hash table, inizializza la struct per la concorrenza sulla
// ht, prende come argomenti il numero di thread lettori e thread scrittori, apre
// il log file e inizializza tutti i thread dell'archivio con le relative
// struct, infine chiude le mutex e cond usate e dealloca i buffer dei
// capi-workers.
int main(int argc, char *argv[]) {
  if (argc != 3) {
    fprintf(stderr, "Uso: ./archivio <num_lettori> <num_scrittori>\n");
    exit(1);
  }

  // Creo la tabella hash
  int ht = hcreate(Num_elem);
  if (ht == 0) {
    xtermina("[ARCHIVIO] Errore creazione HashTable\n", QUI);
  }

  // Assegno i valori opportuni alla struct per la hash table, in modo da poi
  // usarla in conta e aggiungi per la concorrenza sulla hash table
  pthread_mutex_t mutexHT = PTHREAD_MUTEX_INITIALIZER;
  pthread_cond_t condHT = PTHREAD_COND_INITIALIZER;
  struct_rwHT.condHT = condHT;
  struct_rwHT.mutexHT = mutexHT;
  struct_rwHT.writingHT = false;
  struct_rwHT.readersHT = 0;

  // Inizializzo w ed r ricevuti dal server
  int num_thread_lettori = atoi(argv[1]);
  int num_thread_scrittori = atoi(argv[2]);
  assert(num_thread_scrittori > 0);
  assert(num_thread_lettori > 0);

  // Apro il file di log dove ci scrivo il valore che ho letto con le occorrenze
  FILE *lettoriLogFile = xfopen("lettori.log", "w", QUI);

  // Apro le pipe lato archivio
  int capolet = open("capolet", O_RDONLY);
  int caposc = open("caposc", O_RDONLY);

  // Inizializzo tutti i thread di cui avrò bisogno
  pthread_t gestoresegnali_thread;
  pthread_t capolettore_thread;
  pthread_t lettori_threads[num_thread_lettori];
  pthread_t caposcrittore_thread;
  pthread_t scrittori_threads[num_thread_scrittori];

  // --- INIZIONE GESTIONE GESTORE SEGNALI ---
  structSegnali struct_segnali;
  struct_segnali.threadCapoLet = &capolettore_thread;
  struct_segnali.threadCapoSc = &caposcrittore_thread;
  // Inizializzo la maschera del main
  sigset_t maskMain;
  sigemptyset(&maskMain);
  sigaddset(&maskMain, SIGTERM);
  sigaddset(&maskMain, SIGINT);
  sigaddset(&maskMain, SIGUSR1);
  pthread_sigmask(SIG_BLOCK, &maskMain, NULL);

  xpthread_create(&gestoresegnali_thread, NULL, funzione_gestore_segnali,
                  &struct_segnali, QUI);
  printf("[ARCHIVIO] Gestore di segnali iniziato\n");

  // --- INIZIO GESTIONE CAPOLETTORE - LETTORI ---
  // Creo il buffer di capolettore-lettori
  char **bufferLettori = malloc(PC_buffer_len * sizeof(char **));
  if (bufferLettori == NULL)
    xtermina("[ARCHIVIO] Errore malloc buffer lettori", QUI);
  // Inizializzo il numero di elementi nel buffer capolettore-lettori e il
  // puntatore condiviso tra i lettori usato per selezionare il prossimo
  // elemento in uscita
  int totinBufferLettori = 0;
  int next_outLettori = 0;

  // Inizializzo la struct per i lettori
  pthread_mutex_t mutexLettori = PTHREAD_MUTEX_INITIALIZER;
  pthread_cond_t condLettori = PTHREAD_COND_INITIALIZER;
  pthread_cond_t condCapoLet = PTHREAD_COND_INITIALIZER;
  structLettori struct_lettori;
  struct_lettori.condCapoLet = &condCapoLet;
  struct_lettori.condLettori = &condLettori;
  struct_lettori.mutexLettori = &mutexLettori;
  struct_lettori.bufferLettori = bufferLettori;
  struct_lettori.next_inLettori = 0;
  struct_lettori.totinBufferLettori = &totinBufferLettori;
  struct_lettori.next_outLettori = &next_outLettori;
  struct_lettori.capoletPipe = capolet;
  struct_lettori.lettoriLog = lettoriLogFile;

  // Crea il thread capolettore
  if (xpthread_create(&capolettore_thread, NULL, &funzione_capolettore,
                      &struct_lettori, QUI) != 0)
    xtermina("[ARCHIVIO] Errore nella creazione del thread capolettore\n", QUI);
  printf("[ARCHIVIO] Capolettore iniziato\n");

  // Crea i thread lettori che interagiranno con capolettore
  for (int i = 0; i < num_thread_lettori; i++) {
    if (xpthread_create(&lettori_threads[i], NULL, &funzione_lettori,
                        &struct_lettori, QUI) != 0)
      xtermina("[ARCHIVIO] Errore nella creazione del thread lettore\n", QUI);
  }
  printf("[ARCHIVIO] Avvio i %d thread lettori \n", num_thread_lettori);

  // --- INIZIO GESTIONE CAPOSCRITTORE - SCRITTORI ---
  // Creo il buffer caposcrittore - scrittori
  char **bufferScrittori = malloc(PC_buffer_len * sizeof(char **));
  if (bufferScrittori == NULL)
    xtermina("[ARCHIVIO] Errore malloc buffer scrittori", QUI);
  // Inizializzo il numero di elementi nel buffer caposcrittore-scrittori e il
  // puntatore condiviso tra gli scrittori usato per selezionare il prossimo
  // elemento in uscita
  int totinBufferScrittori = 0;
  int next_outScrittori = 0;

  // Inizializzo la struct per i lettori
  pthread_mutex_t mutexScrittori = PTHREAD_MUTEX_INITIALIZER;
  pthread_cond_t condScrittori = PTHREAD_COND_INITIALIZER;
  pthread_cond_t condCapoSc = PTHREAD_COND_INITIALIZER;
  structScrittori struct_scrittori;
  struct_scrittori.condCapoSc = &condCapoSc;
  struct_scrittori.condScrittori = &condScrittori;
  struct_scrittori.mutexScrittori = &mutexScrittori;
  struct_scrittori.bufferScrittori = bufferScrittori;
  struct_scrittori.next_inScrittori = 0;
  struct_scrittori.totinBufferScrittori = &totinBufferScrittori;
  struct_scrittori.next_outScrittori = &next_outScrittori;
  struct_scrittori.caposcPipe = caposc;

  // Crea il thread caposcrittore
  if (xpthread_create(&caposcrittore_thread, NULL, &funzione_caposcrittore,
                      &struct_scrittori, QUI) != 0)
    xtermina("[ARCHIVIO] Errore nella creazione del thread caposcrittore\n",
             QUI);
  printf("[ARCHIVIO] Caposcrittore iniziato\n");

  // Crea i thread scrittori che interagiranno con caposcrittore
  for (int i = 0; i < num_thread_scrittori; i++) {
    if (xpthread_create(&scrittori_threads[i], NULL, &funzione_scrittori,
                        &struct_scrittori, QUI) != 0)
      xtermina("[ARCHIVIO] Errore nella creazione del thread scrittore\n", QUI);
  }
  printf("[ARCHIVIO] Avvio i %d thread scrittori \n", num_thread_scrittori);

  // Attendo la terminazione di lettori, scrittori e gestore segnali, quella dei
  // capi è fatta dal thread gestore di segnali
  for (int i = 0; i < num_thread_lettori; i++) {
    if (xpthread_join(lettori_threads[i], NULL, QUI) != 0)
      xtermina("[ARCHIVIO] Errore nell'attesa dei thread lettori\n", QUI);
  }

  for (int i = 0; i < num_thread_scrittori; i++) {
    if (xpthread_join(scrittori_threads[i], NULL, QUI) != 0)
      xtermina("[ARCHIVIO] Errore nell'attesa dei thread scrittori\n", QUI);
  }

  if (xpthread_join(gestoresegnali_thread, NULL, QUI) != 0)
    xtermina("[ARCHIVIO] Errore nell'attesa del thread gestore segnali\n", QUI);

  // Dealloco cond, mutex e file utilizzati
  pthread_cond_destroy(&condHT);
  xpthread_mutex_destroy(&mutexHT, QUI);
  xpthread_mutex_destroy(&mutexLettori, QUI);
  xpthread_mutex_destroy(&mutexScrittori, QUI);
  pthread_cond_destroy(&condCapoLet);
  pthread_cond_destroy(&condLettori);
  pthread_cond_destroy(&condCapoSc);
  pthread_cond_destroy(&condScrittori);
  fclose(lettoriLogFile);
  xclose(capolet, QUI);
  xclose(caposc, QUI);

  free(bufferLettori);
  free(bufferScrittori);

  printf("<[ARCHIVIO COMPLETATO]>\n");
  return 0;
}