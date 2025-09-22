#define _POSIX_C_SOURCE 200809L
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include "env.h"
#include "scall.h"
#include "logger.h" 

#define BUF_SIZE 512
#define MESSAGE_SIZE 256
#define KEY_SIZE 64
#define VALUE_SIZE 64
#define CODA_SIZE 128

// Funzione che legge il file env.conf e popola la struttura env_config_t.
// Supporta tre chiavi: queue, width, height. Ignora chiavi sconosciute o righe malformate.
// In caso di errore fatale (open, malloc, strdup), il programma termina con exit.
int parse_env(const char *filename, env_config_t *config) {

    // Buffer per messaggi di log
    char msg[MESSAGE_SIZE];

    // Apertura del file
    int fd;
    log_event("parse_env.c", "FILE_PARSING", "Apertura del file env.conf");
    SCALL(fd, open(filename, O_RDONLY), "errore in open env.conf");

    // Allocazione di un buffer per il contenuto del file
    char* buf;
    SNCALL(buf, malloc(BUF_SIZE), "errore in malloc buffer");

    // Lettura del file nel buffer
    ssize_t bytes_read;
    SCALLREAD(bytes_read, read(fd, buf, BUF_SIZE - 1), buf[bytes_read]='\0', "errore in read env.conf");

    // Chiusura del file
    close(fd);
    log_event("parse_env.c", "FILE_PARSING", "Chiusura del file env.conf");

    // Parsing riga per riga del contenuto letto
    char *line = strtok(buf, "\n");
    int riga = 1;
    while (line != NULL) {
        char key[KEY_SIZE], value[VALUE_SIZE];
        // Estrae coppie chiave=valore: legge fino al carattere '='
        if (sscanf(line, "%63[^=]=%63s", key, value) == 2) {
             // Chiave: queue = nome della coda POSIX
            if (strcmp(key, "queue") == 0) {
                char buff[CODA_SIZE];
                snprintf(buff, sizeof(buff), "/%s", value);
                config->queue_name = strdup(buff);
                snprintf(msg, sizeof(msg), "Riga %d: %s=%s", riga, key, value);
                log_event("env.conf", "FILE_PARSING", msg);
                if (!config->queue_name) {
                    perror("strdup queue_name");
                    log_event("parse_env.c", "FILE_PARSING", "Parsing errore dovuto al strdup");
                    // Liberazione del buffer prima di uscire
                    free(buf);
                    exit(EXIT_FAILURE);
                }
            } 

            // Chiave: width = larghezza della griglia
            else if (strcmp(key, "width") == 0) {
                config->width = atoi(value);
                
                snprintf(msg, sizeof(msg), "Riga %d: %s=%s", riga, key, value); 
                log_event("env.conf", "FILE_PARSING", msg); 
            } 

            // Chiave: height = altezza della griglia
            else if (strcmp(key, "height") == 0) {
                config->height = atoi(value);

                snprintf(msg, sizeof(msg), "Riga %d: %s=%s", riga, key, value); 
                log_event("env.conf", "FILE_PARSING", msg); 
            } 

            // Chiave non riconosciuta
            else {
                dprintf(STDERR_FILENO, "Chiave sconosciuta in env.conf: %s\n", key);
            }
        }
        // Riga malformata (senza '=' o incompleta)
        else{
            snprintf(msg, sizeof(msg), "Riga %d ignorata: %s", riga, line);
            log_event("env.conf", "FILE_PARSING", msg);
        }
        // Passa alla riga successiva
        line = strtok(NULL, "\n");
        riga++;
    }

    // liberazione del buffer dopo il parsing
    free(buf);
    // completamento del parsing
    log_event("parse_env.c", "FILE_PARSING", "Parsing completato con successo");
    return 0;
}



// Funzione che libera la memoria dinamica associata alla struttura env_config_t.
void free_env_config(env_config_t *config) {
    if (config->queue_name != NULL) {
        free(config->queue_name);
    }
}


// Funzione che stampa a schermo i valori contenuti nella struttura env_config_t.
void print_env(const env_config_t *config) {
    if (!config) {
        printf("Configurazione ambiente nulla.\n");
        return;
    }
    printf("===== Configurazione Ambiente =====\n");
    printf("Nome coda messaggi: %s\n", config->queue_name);
    printf("Dimensioni griglia: %d x %d\n", config->height, config->width);
}