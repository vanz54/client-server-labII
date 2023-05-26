# Progetto-Laboratorio-II
### Relazione progetto laboratorio II 22/23 - Tommaso Vanz 600897
---------------------------------------------------------------------
## Introduzione
Il progetto completo di Laboratorio II è un programma (multi)client-server per la gestione di sequenze di byte lette in dei files dai clients e gestite tramite una tabella hash da un sottoprogramma archivio in comunicazione (e lanciato) col server tramite delle pipe. Ha come componenti principali:
- `server.py` -> Il server in Python che interagisce con i client di tipo 1 e 2, prende come input il numero di lettori e scrittori da passare al programma archivio.c, lo lancia e scrive le sequenze ricevute su due pipe.
- `client1.c` -> Client in C, prende un file di testo da linea di comando e legge una per volta le linee (saltando quelle vuote) e le manda al server.
- `client2` -> Client in Python, prende uno o più file di testo che assegna rispettivamente a dei thread, tali thread si connettono al server e gli mandano le sequenze lette dai file, ricevono in risposta dal server il numero di sequenze inviate.
- `archivio.c` -> Sottoprocesso del server, scritto in C, riceve da due pipe (una per ogni tipo di client) le sequenze dei file, ha dei processi capi e workers relativi alle due pipe, il compito dei capi è di tokenizzare le sequenze ricevute dalle pipe e di duplicarle in un buffer ciclico, quello dei workers è di estrarle dal buffer e per ogni stringa ottenuta fare delle operazioni in relazione alla tabella hash.
- `rwunfair.c` -> File in C contenente le funzioni per l'accesso concorrente alla hash table (metodo unfair per gli scrittori), nel `rwunfair.h` si trova la struct utilizzata per applicare la concorrenza sulla ht e i prototipi.
- `xerrori.c` -> File in C contenente le funzioni fornite dal professore per una gestione corretta degli errori nelle funzioni.


## Scelte implementative

### Server
Il server ha una visibilità globale sulla socket del server e sul sottoprocesso archivio, utilizza il modulo argparse per prendere i valori opportuni da linea di comando, una volta presi i valori obbligatori e non, lancia la funzione principale del server ('mainServer'), questa funzione mette il server in ascolto sulla porta 50897, lancia il Pool di thread per la gestione dei clients, crea le pipe con la modalità di default 0o0666 in caso non esistessero già e le apre lato server, poi lancia la funzione che inizializza il sottoprocesso archivio.c (con valgrind o meno in base ai parametri passati), inizializza il log file che tiene traccia delle connessioni con tutti i client che il server ha e avrà per tutti i suoi utilizzi, infine si mette in ascolto di connessioni che affiderà ai threads del Pool e non appena riceve un segnale SIGINT avvia la funzione di terminazione.
La funzione 'clients' è quella responsabile della gestione dei client e che esegue il thread del Pool, tale funzione aspetta finché non riceve l'identificatore 'client_tipo<NUMERO_CLIENT>' dal relativo client, successivamente si mette in ascolto delle sequenze in arrivo dai client con 'recv_all' mostrata dal professore a lezione, per ogni client riceve prima la lunghezza della sequenza e dopo la sequenza stessa, le manda sulle rispettive pipe, stampando il log ed eventualmente risponde con le sequenze ricevute (non appena riceve b'\x00\x00\x00\x00' dal client 2).
Infine una volta premuto ctrl+C parte la funzione 'shutdown_server' che chiude la socket, cancella le pipe e manda il segnale SIGTERM al sottoprocesso che aveva precedentemente avviato, terminando in maniera pulita.

### Client 1
Il client 1 prende da linea di comando un file, inizia un ciclo while dove legge dal file finché non arriva in fondo allo stesso, per ogni linea letta con getline(), instaura una connessione col server e manda come prima cosa un identificatore per chiarire al server che è un client di tipo 1, successivamente manda (con writen vista a lezione) la lunghezza della riga e la sequenza stessa, infine chiude il file e libera la memoria utilizzata da getline.

### Client 2
Il client 2 prende da linea di comando >=1 file/s, ne crea una lista e per ogni file ricevuto da linea di comando manda in esecuzione un thread con la funzione 'funzione_client2', il quale successivamente viene messo in un array (utile in fondo per fare la join()). La funzione eseguita dal thread prende quindi in ingresso il file, lo apre e instaura la connessione col server (quindi avrò una connessione per ogni thread), successivamente si identifica come client di tipo 2 e finché non finisce il relativo file manda la lunghezza della sequenza e la sequenza stessa, quando ha finito le righe del file manda il byte b'\x00\x00\x00\x00' per specificare che quel thread non ha più nulla da inviare e aspetta di ricevere 4 byte che indichino il numero di sequenze ricevute dal server; una volta che tutti i thread son terminati, client 2 termina.

### Archivio
Archivio è il sottoprocesso lanciato dal server che si occupa completamente dell'interazione delle sequenze con l'hash table, il server gli passa su argv[1] il numero di lettori, e su argv[2] quello degli scrittori, crea la hash table e inizializza la struct per la concorrenza sulla hash table definita in 'rwunfair.h'.
Successivamente apro il file di log 'lettori.log', che verrà chiuso alla fine, in quanto voglio che tenga traccia delle 'conta' fatte in una singola sessione di archivio, differentemente da quanto accadeva in server.log che teneva traccia di tutte le run del progetto; apro le pipe in lettura per poter poi iniziare la comunicazione col server.

Come primo thread avvio il gestore di segnali, prima però inizializzo la maschera dell'archivio aggiungendo i segnali a cui sono interessato in modo che sia visibile anche a tutti i thread successivi; al gestore passo tramite struct gli identificatori dei thread capi, in quanto uno dei compiti del gestore è fare la join() sui capi, infine nella funzione fatta dal gestore si inizializza una maschera sua e con degli if si gestisce i segnali non appena arrivano (catturandoli con sigwait()).

Successivamente il main avvia i capi (capolettore\caposcrittore) e i rispettivi workers (lettori\scrittori) e inizializza propriamente la struct che passa a entrambe le categorie, le scelte implementativa tra i due capi e i relativi workers sono le medesime, salvo per le operazioni che svolgono che devono seguire la richiesta del progetto:
il capo ha 2 buffer importanti: un buffer su cui mette le sequenze di caratteri che riceve dalla pipe e che tokenizzerà e l'altro in cui inserisce il token duplicato, ogni capo ha la sua pipe e vi leggono prima la lunghezza della sequenza e poi la sequenza stessa (letta usando la lunghezza), se riceve dalla read della lunghezza il valore 0 vuol dire che il server ha chiuso la pipe (infatti il capo avrà letto 0 byte), in tal caso il capo riempie il buffer che ha con i workers di valore di terminazione "3X1T" e termina. Finché non termina, il capo tokenizza la sequenza e duplica ogni token ottenuto dentro al buffer ciclico concorrente con i workers, l'accesso concorrente a questo buffer è fatto utilizzando le condition variables e le mutex (precedentemente passate via struct), la variabile su cui si svegliano capo e workers è il numero totale di elementi nel buffer (se pieno il capo aspetta, se vuoto i workers aspettano);
i workers invece estraggono i token dal buffer ciclico concorrente, se vedono la stringa "3X1T", unlockano e terminano, altrimenti subentra una seconda concorrenza (tra lettori e scrittori sulla hash table) gestita con le rispettive funzioni di lock e unlock (read o write [presenti in rwunfair.c]) che utilizzano condition variables e mutex per rendere safe le rispettive funzioni sulla hash table (conta o aggiungi), in modo che non succeda che un lettore legga un valore vecchio dalla hash table.

Una volta premuto ctr+C per la terminazione pulita del programma, il server chiuderà le pipe, ciò farà riempire il buffer di valori di terminazione dai capi, che si chiuderanno, di conseguenza si chiuderà il thread gestore e infine il main aspetterà la chiusura di tutti i workers prima di deallocare i buffer e chiudere in lettura le pipes.


Il codice in archivio può risultare leggermente dispersivo essendo anche il codice di lettori e scrittori concettualmente uguale, qui sotto uno schema dell'ordine in cui son scritte le funzioni nel file:
<li>lista e gestione hash table 
<li>struct concorrenza ht
<li>creazione entry
<li>aggiungi
<li>conta

 <li>sezione lettori
    <ul>
      <li>struct</li>
      <li>capolettore</li>
      <li>lettore</li>
    </ul>
  <li>sezione lettori
    <ul>
      <li>struct</li>
      <li>caposcrittore</li>
      <li>scrittore</li>
    </ul>
    <li>sezione gestore segnali
    <ul>
      <li>struct</li>
      <li>funzione gestore</li>
    </ul>
      <li>main</li>
 </li>

---------------------------------------------------------------------
Schemi che ho utilizzato come riferimento oltre alla descrizione dettagliata del progetto del professore.
![image](https://github.com/vanz54/Progetto-LAB2/assets/110528455/8d9114eb-1190-41e9-9bb2-0294d01ee46c)
![image](https://github.com/vanz54/Progetto-LAB2/assets/110528455/762038dd-fce2-4352-9df3-55441e8a69d1)

<details>
<summary>Descrizione del progetto dettagliata del professore</summary>

```
## Costanti

* `Num_elem 1000000` dimensione della tabella hash 

* `PC_buffer_len 10`: lunghezza dei buffer produttori/consumatori

* `PORT 5XXXX`: porta usata dal server dove `XXXX` sono le ultime quattro cifre del vostro numero di matricola

* `Max_sequence_length 2048` massima lunghezza di una sequenza che viene inviata attraverso un socket o pipe


## Il programma C archivio

Il file `archivio.c` deve contenere il codice C di un programma multithread che gestisce la memorizzazione di stringhe in una tabella hash. La tabella hash deve associare ad ogni stringa un intero; le operazioni che devono essere suportate dalla tabella hash sono:

* `void aggiungi(char *s)`: se la stringa `s` non è contenuta nella tabella hash deve essere inserita con valore associato uguale a 1. Se `s` è già contenuta nella tabella allora l'intero associato deve essere incrementato di 1.

* `int conta(char *s)` restituisce l'intero associato ad `s` se è contenuta nella tabella, altrimenti 0.

Le operazioni sulla tabella hash devono essere svolte utilizzando le funzioni descritte su `man hsearch`. Si veda il sorgente `main.c` per un esempio. Si noti che la tabella hash è mantenuta dal sistema in una sorta di variabile globale (infatti ne può esistere soltanto una).

Il programma `archivio` riceve sulla linea di comando due interi che indicano il numero `w` di thread scrittori (che eseguono solo l'operazione `aggiungi`), e il numero `r` di thread lettori (che eseguono solo l'operazione `conta`). L'accesso concorrente di lettori e scrittori alla hash table deve essere fatto utilizzando le condition variables usando lo schema che favorisce i lettori visto nella lezione 40 (o un altro schema più equo a vostra scelta).

Oltre ai thread lettori e scrittori, il programma archivio deve avere:

* un thread "capo scrittore" che distribuisce il lavoro ai thread scrittori mediante il paradigma produttore/consumatori

* un thread "capo lettore" che distribuisce il lavoro ai thread lettori mediante il paradigma produttore/consumatori

* un thread che gestisce i segnali mediante la funzione `sigwait()`


### I thread scrittori e il loro capo 

Il thread "capo scrittore" legge il suo input da una FIFO (named pipe) `caposc`. L'input che riceve sono sequenze di byte, ognuna preceduta dalla sua lunghezza. Per ogni sequenza ricevuta il thread capo scrittore deve aggiungere in fondo un byte uguale a 0; successivamente deve effettuare una tokenizzazione utilizzando `strtok` (o forse `strtok_r`?) utilizzando `".,:; \n\r\t"` come stringa di delimitatori. Una copia (ottenuta con `strdup`) di ogni token deve essere messo su un buffer produttori-consumatori per essere gestito dai thread scrittori (che svolgono il ruolo di consumatori). I thread scrittori devono semplicemente chiamare la funzione `aggiungi` su ognuna delle stringhe che leggono dal buffer.

Il buffer produttori-consumatori consiste quindi di puntatori a `char` e deve essere di lunghezza `PC_buffer_len`. 

Naturalmente tutti gli array intermedi usati nel processo devono essere deallocati. 

Non appena la FIFO `caposc` viene chiusa in scrittura, il thread "capo scrittore" deve mandare un valore di terminazione ai thread scrittori e terminare lui stesso.



### I thread lettori e il loro capo

Il thread "capo lettore" si comporta in maniera simile al "capo scrittore" tranne che:

* Riceve il suo input dalla FIFO `capolet`

* Scrive i token su un buffer (sempre di lunghezza `PC_buffer_len`) che è condiviso con i thread lettori.

I thread lettori devono chiamare la funzione `conta` per ognuna delle stringhe lette dal buffer, e scrivere una linea nel file `lettori.log` contenente la stringa letta e il valore restituito dalla funzione `conta`; ad esempio se `conta("casa")` restituisce 7 il thread deve scrivere la stringa `casa 7` (seguita da un cararattere `\n`) nel file `lettori.log`. 

Non appena la FIFO `capolet` viene chiusa in scrittura, il thread "capo lettore" deve mandare un valore di terminazione ai thread lettori e terminare lui stesso.



### Il thread gestore dei segnali

Tutti i segnali ricevuti dal programma `archivio` devono essere  gestiti da questo thread. 

* Quando viene ricevuto il segnale `SIGINT` il thread deve stampare su `stderr` il numero totale di stringhe distinte contenute dentro la tabella hash (questo richiede che in qualche modo manteniate questo numero durante le operazioni `aggiungi`); il programma non deve terminare. 

* Quando viene ricevuto il segnale `SIGTERM` il thread deve attendere la terminazione dei thread "capo lettore" e "capo scrittore"; successivamente deve stampare su `stdout` il numero totale di stringhe distinte contenute dentro la tabella hash, dellocare la tabella hash (e il suo contenuto per il **progetto completo**, vedere sotto) e far terminare il programma. Questa è l'unica modalità "pulita" con cui deve terminare il programma. Durante queste operazioni di terminazione non devono essere gestiti ulteriori segnali. 

* **[Solo per il progetto completo]**  Quando viene ricevuto un segnale `SIGUSR1` il thread gestore deve ottenere l'accesso in scrittura alla tabella hash, deallocare tutti i dati memorizzati nella tabella, e chiamare le funzioni `hdestroy` seguita da `hcreate(Num_elem)`. In pratica questo corrisponde a cancellare tutti i vecchi dati dalla tabella e ripartire con una tabella vuota.


### Deallocazione della memoria

Il programma deve deallocare tutta la memoria utilizzata (a parte la tabella hash per il progetto ridotto, vedi sotto). Lanciando il server con l'opzione `-v` (vedi sotto) viene generato un file `valgrind-NNN.log` contenente il report della memoria persa con anche l'indicazione del punto in cui la memoria persa era stata allocata. 

La memoria utilizzata dalla tabella hash non viene restituita automaticamente con la chiamata `hdestroy` di conseguenza chi fa il progetto ridotto è normale che, se nella tabella sono state memorizzate *N* stringhe si ritrovi *N* blocchi da 16 byte ciascuno `definitely lost` e *6N* blocchi di dimensione variabile `indirectly lost`. 



**[Solo per il progetto completo]** Quando termina il programma deve deallocare anche la memoria utilizzata per memorizzare gli oggetti nella tabella hash. La funzione `hdestroy` si limita a deallocare la tabella; i dati in essa contenuti devono invece essere deallocati dal vostro programma. A questo scopo è necessario che gli oggetti inseriti nella tabella hash siano mantenuti in una linked list che deve essere usata per deallocare tutti gli oggetti al momento della terminazione del programma (o quando viene ricevuto il segnale `SIGUSR1`). Una possibile struttura di questa linked list è mostrata qui sotto ed è realizzata all'interno del file `main_linked.c`. Con questo modifica l'output di `valgrind` non dovrebbe mostrare nessun blocco perso. 


## Il server 

Il server deve essere scritto in Python e si deve mettere in attesa su `127.0.0.1` sulla porta  `5XXXX` dove `XXXX` sono le ultime quattro cifre del vostro numero di matricola. Ad ogni client che si connette il server deve assegnare un thread dedicato. I client posso essere di due tipi

* **Tipo A**: inviano al server una singola sequenza di byte. Il server deve scrivere tale sequenza nella FIFO `capolet`

* **Tipo B**: inviano al server un numero imprecisato di sequenze di byte; quando il client ha inviato l'ultima sequenza esso segnala che non ce ne sono altre inviandone una di lunghezza 0. Il server deve scrivere ognuna di queste sequenze (tranne quella di lunghezza zero) nella FIFO `caposc`. **Solo per il progetto completo:** successivamente il server deve inviare un intero al client che indica il numero totale di sequenze ricevute durante la sessione.   

Il server deve usare il modulo `logging` per la gestione di un file di log di nome `server.log`. Per ogni connessione, il server deve scrivere sul file di log il tipo della connessione e il numero totale di byte scritti nelle FIFO `capolet` o `caposc`.

Il server deve essere scritto in Python in un file *eseguibile* di nome `server.py` e deve usare il modulo `argparse` per la gestione degli argomenti sulla linea di comando, e deve richiedere come argomento obbligatorio un intero positivo che indica il numero massimo di thread che il server deve utilizzare contemporanemente per la gestione dei client (usate la classe `ThreadPoolExecutor` vista a lezione).


Altre operazioni che deve svolgere il server:

* All'avvio, se non sono già presenti nella directory corrente, deve creare le FIFO `caposc` e `capolet`

* Deve accettare due parametri positivi `-r` e `-w` sulla linea di comando e deve lanciare il programma `archivio` passandogli questi due parametri sulla linea di comando che rappresentano rispettivamente il numero di thread lettori e scrittori (esclusi i capi). Il valore di default per entrambi questi parametri è 3. Usare `subprocess.Popen` per lanciare `archivio`, vedere `manager.py` per un esempio.

* Deve accettare l'opzione `-v` sulla linea di comando che forza il server a chiamare il programma `archivio` mediante `valgrind` con opzioni `valgrind --leak-check=full --show-leak-kinds=all --log-file=valgrind-%p.log`, vedere ancora `manager.py` per un esempio.

* Se viene inviato il segnale `SIGINT`, il server deve terminare l'esecuzione chiudendo il socket con l'istruzione `shutdown`, cancellando (con `os.unlink`) le FIFO `caposc` e `capolet` e inviando il segnale `SIGTERM` al programma `archivio` (il segnale `SIGINT` in Python genera l'eccezione `KeyboardInterrupt`)


## Il client tipo 1

Questo client deve accettare sulla linea di comando il nome di un file di testo e inviare al server, una alla volta, le linee del file di testo con una connessione di tipo A. L'eseguibile si deve chiamare `client1`:

* Per il progetto ridotto questo client può essere scritto in Python; si deve comunque chiamare `client1` senza l'estensione `.py` ed essere un file *eseguibile*.  


* **per il progetto completo:** questo client deve essere scitto in C e usare la funzione `getline` per la lettura delle singole linee del file di testo, e deve deallocare correttamente tutta la memoria utilizzata.



## Il client tipo 2

Questo client deve accettare sulla linea di comando il nome di uno o più file di testo. Per ogni file di testo passato sulla linea di comando deve essere creato un thread che si collega al server e invia una alla volta le linee del file con una connessione di tipo B (si intende una connessione per ogni thread). 

Questo client può esere scritto in C o Python a vostra scelta ma il file eseguibile deve chiamarsi `client2` 

```
</details>
