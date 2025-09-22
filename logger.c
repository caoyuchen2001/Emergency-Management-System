#define _POSIX_C_SOURCE 200809L
#include <stdio.h>      
#include <stdlib.h>     
#include <string.h>     
#include <unistd.h>     
#include <fcntl.h>     
#include <errno.h>     
#include <time.h> 
#include <threads.h>
#include "logger.h"
#include "scall.h"

#define FILE_NAME "emergency.log"

// File descriptor 
static int log_fd = -1;
// Mutex per scrivere su file log
static mtx_t log_mutex;

// Funzione che inizializza il sistema di logging
// Apre il file di log e inizializza il mutex per l'accesso concorrente
void init_log(void) {
    // Apre il file di log (in modalità append, crea se non esiste)
    SCALL(log_fd, open(FILE_NAME, O_CREAT | O_WRONLY | O_APPEND, 0644), "errore in open emergency.log");

    // Scrive un messaggio di avvio nel log 
    dprintf(log_fd, "[%ld] [logger.c] [FILE_PARSING] Inizializzazione sistema di logging\n", time(NULL));

    // Inizializza il mutex associato al log
    if (mtx_init(&log_mutex, mtx_plain) != thrd_success) { 
            perror("errore in init log_mutex"); 
            exit(EXIT_FAILURE); 
    } 
}


// Funzione che scrive un evento nel file di log in modo thread-safe
// id: identificatore dell'origine dell'evento (ID emergenza)
// event_type: tipo dell'evento (es. "INTENT", "ASSIGNMENT")
// message: messaggio descrittivo dell'evento
void log_event(const char *id, const char *event_type, const char *message) {

    // Controlla che i parametri non siano nulli, assegna valori di default se necessario
    if (!id) id = "N/A";
    if (!event_type) event_type = "UNKNOWN";
    if (!message) message = "(null)";

    // Ottiene il tempo corrente
    time_t now = time(NULL);

    // Contatore statico per evitare troppi fsync(), che ridurrebbero le prestazioni
    static int counter = 0;

    // Acquisisce il mutex per accesso concorrente al file di log
    if (mtx_lock(&log_mutex) != thrd_success) {
            perror("errore in lock log_mutex"); 
            exit(EXIT_FAILURE);
    } 

    // Scrive l'evento nel file di log
    dprintf(log_fd, "[%ld] [%s] [%s] %s\n", now, id, event_type, message);

    // Effettua flush su disco ogni 10 scritture per ottimizzare le prestazioni
    counter++;
    if (counter >= 10) {
        fsync(log_fd);
        counter = 0;
    }

    // Rilascia il mutex
    if (mtx_unlock(&log_mutex) != thrd_success) { 
            perror("errore in unlock log_mutex"); 
            exit(EXIT_FAILURE); 
    } 
}


// Funzione che chiude in sicurezza il file di log e distrugge il mutex
void close_log(void) {
    // Chiude il file descriptor in sezione critica per evitare
    // condizioni di race con log_event()
    if (mtx_lock(&log_mutex) != thrd_success) {
            perror("errore in lock log_mutex"); 
            exit(EXIT_FAILURE);
    }

    // Se il file è stato aperto correttamente, lo si chiude
    if (log_fd != -1) {
        close(log_fd);
    }

    // Rilascia il lock prima di distruggere il mutex
    if (mtx_unlock(&log_mutex) != thrd_success) { 
            perror("errore in unlock log_mutex"); 
            exit(EXIT_FAILURE); 
    } 

    // Distrugge il mutex
    mtx_destroy(&log_mutex);
}

// Funzione di supporto che formatta l'ID di emergenza e invoca log_event()
// id: id dell'emergenza
// event_type: tipo dell'evento (es. "ASSIGNMENT", "INTENT")
// message: messaggio descrittivo da scrivere nel log
void log_event_id(int id, const char *event_type, const char *message) {
    char id_str[32];
    snprintf(id_str, sizeof(id_str), "Emergenza %d", id);
    log_event(id_str, event_type, message);
}