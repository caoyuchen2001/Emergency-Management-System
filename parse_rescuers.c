#define _POSIX_C_SOURCE 200809L
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include "rescuers.h"
#include "scall.h"
#include "logger.h"

#define BUFFER_SIZE 65536
#define NAME_SIZE 64
#define MESSAGE_SIZE 256

// Funzione che parse il file rescuers.conf e popola la struttura rescuer_data_t
int parse_rescuers(const char *filename, rescuer_data_t *data) {

    // ID progressivo globale per assegnare univocamente i twin
    int global_twin_id =1;
    // Buffer per i messaggi di log
    char msg[MESSAGE_SIZE];

    // Apertura del file con SC open
    int fd;
    log_event("parse_rescuers.c", "FILE_PARSING", "Apertura del file rescuers.conf");
    SCALL(fd, open(filename, O_RDONLY), "errore in open rescuers.conf");

    // Lettura del file in buffer 
    char *buf;
    SNCALL(buf, malloc(BUFFER_SIZE), "malloc buffer");

    // Lettura del file in buffer e chiusura del file descriptor
    ssize_t bytes_read = read(fd, buf, BUFFER_SIZE - 1);
    if (bytes_read < 0)
    {
        perror("errore in read rescuers.conf");
        free(buf);
        close(fd);
        exit(EXIT_FAILURE);
    }
    buf[bytes_read] = '\0';
    close(fd);
    log_event("parse_rescuers.c", "FILE_PARSING", "Chiusura del file rescuers.conf");

    // Alloca spazio per tipi e twins
    data->types = NULL;
    data->twins = NULL;
    data->num_types = 0;
    data->num_twins = 0;
    SNCALL(data->types, malloc(sizeof(rescuer_type_t*) * MAX_TYPES), "errore in malloc types");
    SNCALL(data->twins, malloc(sizeof(rescuer_digital_twin_t) * MAX_TWINS), "errore in malloc twins");
    memset(data->twins, 0, sizeof(rescuer_digital_twin_t) * MAX_TWINS);

    // Parsing riga per riga del contenuto letto
    char *line = strtok(buf, "\n");
    int riga = 1;
    while (line) {
        char name[NAME_SIZE];
        int count, speed, x, y;

        // Estrae quadrupla: legge fino al carattere ']', 
        // poi acquisisce quattro interi
        if (sscanf(line, "[%63[^]]][%d][%d][%d;%d]", name, &count, &speed, &x, &y) == 5) {
            // alloca tipo
            rescuer_type_t *type;
            SNCALL(type, malloc(sizeof(rescuer_type_t)), "errore in malloc rescuer_type_t");
            SNCALL(type->rescuer_type_name, strdup(name), "errore in strdup rescuer_type_name");

            type->speed = speed;
            type->x = x;
            type->y = y;

            data->types[data->num_types++] = type;

            // Log riga valida
            snprintf(msg, sizeof(msg), "Riga %d: rescuer_nome=%s, quantita'=%d, velocita'=%d, base=(%d,%d)",
                     riga, name, count, speed, x, y);
            log_event("rescuers.conf", "FILE_PARSING", msg);

            // Crea gemelli digitali
            for (int i = 0; i < count; ++i) {
                if (data->num_twins >= MAX_TWINS) {
                    log_event("rescuers.conf", "FILE_PARSING", "Limite gemelli digitali superato");
                    free(buf);
                    exit(EXIT_FAILURE);
                }

                rescuer_digital_twin_t *twin = &data->twins[data->num_twins++];// &(*(data->twins+data->num_twins++))
                twin->id = global_twin_id++;
                twin->x = x;
                twin->y = y;
                twin->rescuer = type;
                twin->status = IDLE;

            }

        } else {
            // Riga malformata
            snprintf(msg, sizeof(msg), "Riga %d ignorata: %s", riga, line);
            log_event("rescuers.conf", "FILE_PARSING", msg);
        }

        riga++;
        line = strtok(NULL, "\n");
    }

    // Fine parsing
    log_event("parse_rescuers.c", "FILE_PARSING", "Parsing completato con successo");
    return 0;
}

// Funzione che libera la memoria dinamicamente allocata per i dati dei soccorritori.
void free_rescuers_data(rescuer_data_t *data) {
    if(data!=NULL){
        // Libera ogni tipo di soccorritore
        for (int i = 0; i < data->num_types; ++i) {
        free(data->types[i]->rescuer_type_name);
        free(data->types[i]);
    }
    // Libera array dei tipi e dei twin
    free(data->types);
    free(data->twins);

    }
}

const char* twin_status_to_string(rescuer_status_t status) {
    switch (status) {
        case IDLE: return "IDLE";
        case EN_ROUTE_TO_SCENE: return "EN_ROUTE_TO_SCENE";
        case ON_SCENE: return "ON_SCENE";
        case RETURNING_TO_BASE: return "RETURNING_TO_BASE";
        default: return "IDLE";
    }
}

// Funzione che stampa tutti i gemelli digitali
void print_rescuer_data(rescuer_data_t *data) {
    if (!data) {
        printf("Dati dei soccorritori nulli.\n");
        return;
    }

    printf("\n===== Gemelli digitali =====\n");
    for (int i = 0; i < data->num_twins; ++i) {
        rescuer_digital_twin_t *twin = &data->twins[i];

        printf("Create Twin ID %d: tipo=%s, posizione=(%d, %d), stato=%s\n",
               twin->id,
               twin->rescuer->rescuer_type_name,
               twin->x, twin->y,
               twin_status_to_string(twin->status));
    }
}


