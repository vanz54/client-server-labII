#! /usr/bin/env python3
import socket, sys, argparse, subprocess, time, signal, os, errno, time, struct, logging
from concurrent.futures import ThreadPoolExecutor

# Autore -> Tommaso Vanz

# Inizializzo la socket del server e la variabile che conterrà il processo archivio a globali per poterle usare nelle funzioni effettuate dal server
server_socket = None
archivio_subprocess = None

# ///////////////////// GESTIONE CLIENT1 E CLIENT2 \\\\\\\\\\\\\\\\\\\\\\
def clients(client_socket, addr, capolet, caposc):   
    # Switcha sul tipo di client guardando che identificatore mi arriva prima delle sequenze e lo salvo in una variabile
    tipo_client = client_socket.recv(12).decode('utf-8')
    if tipo_client == "client_tipo1":
        # Il client1 instaura una connessione per ogni linea letta dal file che riceve
        print(f"[SERVER] Client 1 si connette con una connessione di tipo A sulla porta {addr[1]}")
        # Mi mantengo il numero totale di byte che mando a caposc (lunghezze & stringhe mandate in byte)
        num_byte_mandati_pipe = 0
        lunghezza=0
        while True:
            # Riceve la lunghezza della riga
            lunghezza_inbyte = recv_all(client_socket,4)
            if lunghezza_inbyte == 0:
                break
            lunghezza  = struct.unpack('!i',lunghezza_inbyte[:4])[0]
            if not lunghezza:
                break
            # Riceve la riga in byte
            linea_inbyte = recv_all(client_socket,lunghezza)
            if not linea_inbyte:
                break
            # Decodifica la riga
            linea = linea_inbyte.decode('utf-8')
            lunghezza_inbyte = struct.pack('i',lunghezza)

            # Invia la riga al processo archivio su capolet
            #print(f"len -> {lunghezza} string -> {linea}")
            os.write(capolet, lunghezza_inbyte)
            os.write(capolet, linea_inbyte)  
            #Sommo le lunghezze in byte per poi stamparle su server.log
            num_byte_mandati_pipe += len(lunghezza_inbyte)
            num_byte_mandati_pipe += len(linea_inbyte)
        # Registra le informazioni sulla connessione nel file di log
        logging.info("Connessione di tipo A - byte scritti su 'capolet' ->: %d", num_byte_mandati_pipe)
        #Chiudo la socket appena finisco di leggere tutto
        client_socket.close()
    
    elif tipo_client == "client_tipo2":
        # Il client2 instaura una connessione per ogni file (e quindi thread) che riceve da linea di comando  
        print(f"[SERVER] Client 2 si connette con una connessione di tipo B sulla porta {addr[1]}")
        # Mi mantengo il numero totale di byte che mando a caposc (lunghezze & stringhe mandate in byte) e il totale di sequenze ricevute
        num_byte_mandati_pipe = 0
        tot_seq_ricevute = 0
        while True:
            # Riceve la lunghezza della riga
            lunghezza_inbyte = recv_all(client_socket,4)
            if lunghezza_inbyte == b'\x00\x00\x00\x00': # Se ricevo 4 byte di 0 significa che il client ha finito di mandare le righe, mi torna bene così riesco a catturarlo assieme alle lunghezze
                client_socket.sendall(struct.pack('!i', tot_seq_ricevute))
                break
            lunghezza  = struct.unpack('!i',lunghezza_inbyte[:4])[0]
            if not lunghezza:
                break
            # Riceve la riga in byte
            linea_inbyte = recv_all(client_socket,lunghezza)
            if not linea_inbyte:
                break
            # Decodifica la riga
            linea = linea_inbyte.decode('utf-8')
            lunghezza_inbyte = struct.pack('i',lunghezza)
            # Invia la riga al processo archivio su caposc
            os.write(caposc, lunghezza_inbyte)
            os.write(caposc, linea_inbyte)
            num_byte_mandati_pipe += len(lunghezza_inbyte)
            num_byte_mandati_pipe += len(linea_inbyte)
            tot_seq_ricevute += 1 # Sommo ogni volta al totale la sequenza che ricevo, per dire al client quante sequenze ho ricevuto
        
        # Registra le informazioni sulla connessione nel file di log
        logging.info("Connessione di tipo B - byte scritti su 'caposc' ->: %d", num_byte_mandati_pipe)
        # Resetto il numero di byte mandati a caposc
        num_byte_mandati_pipe = 0
        #Chiudo la socket appena finisco di leggere tutto
        client_socket.close()
                

def recv_all(conn,n):
  # Funzione mostrata a lezione, riceve esattamente n byte dal socket conn e li restituisce
  # il tipo restituto è "bytes": una sequenza immutabile di valori 0-255
  # Questa funzione è analoga alla readn che abbiamo visto nel C
  chunks = b''
  bytes_recd = 0
  while bytes_recd < n:
    chunk = conn.recv(min(n - bytes_recd, 2048))
    if len(chunk) == 0:
      return 0
    chunks += chunk
    bytes_recd = bytes_recd + len(chunk)
  return chunks        

# !!!!!!!!! FUNZIONE DI TERMINAZIONE DEL SERVER CHE ESEGUE IN SEGUITO AL CTRL+C !!!!!!!!!
def shutdown_server(signum, frame):
    global server_socket, archivio_subprocess
    print("[...TERMINAZIONE SERVER...]") 
     
    # Chiude il socket del server
    server_socket.shutdown(socket.SHUT_RDWR)
    server_socket.close()
    
    # Chiudo le pipe caposc e capolet
    if os.path.exists("caposc"):
        os.unlink("caposc")
    if os.path.exists("capolet"):
        os.unlink("capolet")
    
    # Invia il segnale SIGTERM al processo archivio
    archivio_subprocess.send_signal(signal.SIGTERM)

    print("<[SERVER TERMINATO]>")
    exit()
    

# Funzione che lancia archivio in modalità normale passando il numero di lettori e scrittori
def archivio_normale(readers, writers):
    global server_socket, archivio_subprocess
    # Esegue il programma C
    archivio_subprocess = subprocess.Popen(['./archivio.out', str(readers), str(writers)])
    print(f"[SERVER] Lancio il processo archivio {archivio_subprocess.pid} con {readers} lettori e {writers} scrittori")

# Funzione che lancia archivio con valgrind, passando il numero di lettori e scrittori e i parametri che il professore ha utilizzato in manager.py
def archivio_valgrind(readers, writers):
    global server_socket, archivio_subprocess
    # Esegue il programma C passando anche valgrind
    archivio_subprocess = subprocess.Popen(["valgrind","--leak-check=full", "--show-leak-kinds=all",  "--log-file=valgrind-%p.log", "./archivio.out", str(readers), str(writers)])
    print(f"[SERVER] Ho lanciato il processo archivio {archivio_subprocess.pid} con valgrind")

# //////////////  MAIN SERVER  \\\\\\\\\\\\\\\\
def mainServer(thread_count, readers, writers, valgrind):
    global server_socket, archivio_subprocess
    # Configurazione del server (porta-matricola 600897)
    host = 'localhost'
    port = 50897

    # Creazione del socket del server
    server_socket = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    server_socket.bind((host, port))
    server_socket.listen(5) # Specifico il numero di connessioni 
    print(f"[SERVER] Server in ascolto sulla porta {port}")
    
    
    # Creazione di un pool di thread
    executor = ThreadPoolExecutor(thread_count)
    print(f"[SERVER] In uso un pool di {thread_count} threads")
    
    # Inizializzazione delle pipe
    if not os.path.exists("capolet"): 
      os.mkfifo("capolet",0o0666)
      print("[SERVER] Pipe capolet creata")
    if not os.path.exists("caposc"):
      os.mkfifo("caposc",0o0666)
      print("[SERVER] Pipe caposc creata")
      
    # Se uso l'opzione per usare valgrind (-v) NON lancio normalmente archivio ma lo lancio con valgrind
    if valgrind:
      archivio_valgrind(readers, writers)
    # Altrimenti lo lancio normalmente
    else:
      archivio_normale(readers, writers)


    # Se le pipe son già presenti le apro e basta
    capolet = os.open("capolet", os.O_WRONLY)
    caposc = os.open("caposc", os.O_WRONLY)
    print("[SERVER] Pipe aperte")
  
    # Configurazione del logger
    logging.basicConfig(filename='server.log', level=logging.INFO, format='%(asctime)s - %(message)s')

    # Gestisce il segnale SIGINT
    signal.signal(signal.SIGINT, shutdown_server)

    while True:
        # Accetta una connessione dal client
        client_socket, addr = server_socket.accept()
        
        # Assegna un thread del pool per gestire il client
        executor.submit(clients(client_socket, addr, capolet, caposc), client_socket)


# Inizializzazione server
if __name__ == '__main__':
    # Creazione dell'oggetto parser e definizione degli argomenti
    parser = argparse.ArgumentParser(description='Uso: ./server.py max_threadsInPool <scrittori> <lettori> <valgrind>')
    parser.add_argument("thread_count", type=int, help="Numero massimo di thread nel pool")
    parser.add_argument("-r", "--readers", type=int, default=3, help="Numero di lettori")
    parser.add_argument("-w", "--writers", type=int, default=3, help="Numero di scrittori")
    parser.add_argument("-v", "--valgrind", action="store_true", help="Esegui con Valgrind") 
    # Se non scrivo nulla va senza valgrind, altrimenti runna con valgrind
    
    # Parsing degli argomenti da linea di comando
    args = parser.parse_args()
      
    # Chiamata alla funzione principale
    mainServer(args.thread_count, args.readers, args.writers, args.valgrind)
