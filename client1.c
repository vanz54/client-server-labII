//Autore -> Tommaso Vanz
#define  _GNU_SOURCE
#include <arpa/inet.h>
#include <errno.h>
#include <netinet/in.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h> 
#include <unistd.h>
#include <sys/socket.h>
// Costanti utilizzate in client1
#define QUI __LINE__, __FILE__
#define HOST "127.0.0.1"
#define PORT 50897 
#define Max_sequence_length 2048

// Funzione di terminazione
void termina(const char *messaggio) {
  if(errno==0)  fprintf(stderr,"== %d == %s\n",getpid(), messaggio);
  else fprintf(stderr,"== %d == %s: %s\n",getpid(), messaggio, strerror(errno));
  exit(1);
}

// Funzione mostrata dal prof a lezione, in modo che non devo essere io che ogni volta mi metto lì a vedere se i dati sono scritti tutti: gli chiedo di scrivere tot byte e lui va avanti finché non riesce a scriverli tutti, analoga per leggere
ssize_t writen(int fd, void *ptr, size_t n) {
  size_t nleft;
  ssize_t nwritten;

  nleft = n;
  while (nleft > 0) {
    if ((nwritten = write(fd, ptr, nleft)) < 0) {
      if (nleft == n)
        return -1; 
      else
        break; 
    } else if (nwritten == 0)
      break;
    nleft -= nwritten;
    ptr += nwritten;
  }
  return (n - nleft); /* return >= 0 */
}

int main(int argc, char *argv[]) {
  if (argc != 2) {
    fprintf(stderr, "Uso: ./client1.out <nomefile>\n");
    exit(1);
  }

  // Apertura file
  FILE *file = fopen(argv[1], "r");
  if (file == NULL)
    termina("[CLIENT1] Errore apertura file\n");

  // Inizializzazione variabili per poter mandare le righe al server con ogni connessione
  char *linea = NULL;
  size_t lunghezza = 0;
  ssize_t letti;
  size_t e;
  int tmp;

  while ((letti = getline(&linea, &lunghezza, file)) != -1) {
    if (strlen(linea) == 1 && linea[0] == '\n') {
        //printf("[CLIENT1] Linea vuota, la skippo\n");
        continue;
    }
    if (strlen(linea) > 2048) {
      // printf("[CLIENT1] Lunghezza sequenza troppo grande\n");
      continue;
    }
    // Creazione socket
    int client_socket = socket(AF_INET, SOCK_STREAM, 0);
    if (client_socket == -1)
      termina("[CLIENT1] Errore creazione client socket\n");
    
    // Connessione al server da parte di client1
    struct sockaddr_in server_addr;
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = inet_addr(HOST);
    server_addr.sin_port = htons(PORT);
  
    if (connect(client_socket, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0) {
      printf("[CLIENT1] Errore connessione client1 sulla porta %d\n", htons(PORT));
      fclose(file);
      close(client_socket);
      exit(1);
    }
  
    // Mi identifico come client1 mandando al server un identificatore del tipo "client_tipoX"
    char *client_id = "client_tipo1";
    if (send(client_socket, client_id, strlen(client_id), 0) < 0)
      termina("[CLIENT1] Errore invio identificatore 'client_tipo1'\n");
    
    // Invio lunghezza riga
    tmp = htonl(strlen(linea));
    e = writen(client_socket, &tmp, sizeof(tmp));
    if (e != sizeof(int))
      termina("[CLIENT1] Errore write lunghezza riga\n");

    // Invio sequenza di caratteri
    if (send(client_socket, linea, letti, 0) < 0)
      termina("[CLIENT1] Errore invio riga\n");

    // Chiudo la socket in modo che possa essere aperta un'altra per la prossima riga
    close(client_socket);
  }

  // Chiudo il file quando finisco di leggerlo e dealloco
  fclose(file);
  free(linea);

  printf("<[CLIENT1 COMPLETATO]>\n");
  return 0;
}
