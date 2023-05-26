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
Per quanto riguarda la hash table, la lista che aiuta a tenerne traccia è composta nel medesimo modo proposto dal professore:



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

Descrizione del progetto dettagliata del professore -> [istruzioni.md](https://github.com/vanz54/Progetto-LAB2/files/11578472/istruzioni.md)

