#define _POSIX_C_SOURCE 200809L
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include "scall.h"
#include "logger.h"
#include "rescuers.h"
#include "emergency_types.h"

#define MAX_EMERGENCIES 256
#define MAX_REQ_PER_EMERGENCY 16
#define NAME_SIZE 64
#define RESCUER_LENGTH 256
#define MSG_SIZE 256

// Funzione che parse il file emergency_types.conf.
int parse_emergency_types(const char *filename, const rescuer_data_t *rescuer_data, emergency_data_t *emergency_data) {

    // Apre il file in sola lettura utilizzando open + fdopen (gestione più flessibile degli errori)
    int fd = open(filename, O_RDONLY);
    if (fd == -1) {
        perror("open failed");
        exit(EXIT_FAILURE);
    }
    FILE *file = fdopen(fd, "r");
    if (!file) {
        perror("fdopen failed");
        close(fd); 
        exit(EXIT_FAILURE);
    }

    log_event("parse_emergency_types.c", "FILE_PARSING", "Apertura del file emergency_types.conf");

    // Alloca il vettore dei tipi di emergenza
    emergency_type_t *types;
    SNCALL(types, malloc(sizeof(emergency_type_t) * MAX_EMERGENCIES), "malloc emergency types");

    int count = 0; // Contatore dei tipi validi
    char *line = NULL;
    size_t len = 0;
    int riga = 1;
    char msg[MSG_SIZE];

    // Ciclo di lettura riga per riga
    while (getline(&line, &len, file) != -1 && count < MAX_EMERGENCIES) {
        char name[NAME_SIZE], rescuer_spec[RESCUER_LENGTH];
        short priority;

        // Rimuove newline finale (sia \r\n che \n)
        line[strcspn(line, "\r\n")] = 0;

        // Parsea una riga con formato: [nome] [priorità] tipo1:q,d;tipo2:q,d;...
        if (sscanf(line, "[%63[^]]] [%hd] %[^\n]", name, &priority, rescuer_spec) == 3) {
            
            // Crea struttura temporanea etype per questa riga
            emergency_type_t etype_temp;
            etype_temp.priority = priority;
            SNCALL(etype_temp.emergency_desc, strdup(name), "strdup emergency_desc");
            etype_temp.rescuers_req_number = 0;
            SNCALL(etype_temp.rescuers, malloc(sizeof(rescuer_request_t) * MAX_REQ_PER_EMERGENCY), "malloc rescuers");

            // Copia sicura della parte rescuer per strtok
            char spec_copy[RESCUER_LENGTH];
            strncpy(spec_copy, rescuer_spec, sizeof(spec_copy));
            spec_copy[sizeof(spec_copy) - 1] = '\0';

            // Divide la stringa con strtok usando il separatore ;
            char *entry = strtok(spec_copy, ";");
            while (entry && etype_temp.rescuers_req_number < MAX_REQ_PER_EMERGENCY) {
                char rescuer_name[NAME_SIZE];
                int quantity, duration;

                // Parsea ogni voce: nome:q,d
                if (sscanf(entry, "%63[^:]:%d,%d", rescuer_name, &quantity, &duration) == 3) {
                    rescuer_type_t *matched = NULL;

                    // Cerca corrispondenza tra i tipi di rescuer disponibili
                    for (int i = 0; i < rescuer_data->num_types; i++) {
                        if (strcmp(rescuer_data->types[i]->rescuer_type_name, rescuer_name) == 0) {
                            matched = rescuer_data->types[i];
                            break;
                        }
                    }

                    // Se trovato, inserisce nella lista dei rescuer richiesti
                    if (matched) {
                        rescuer_request_t *req = &etype_temp.rescuers[etype_temp.rescuers_req_number++];
                        req->type = matched;
                        req->required_count = quantity;
                        req->time_to_manage = duration;
                    } else {
                        snprintf(msg, MSG_SIZE, "Riga %d: Tipo rescuer sconosciuto: %s", riga, rescuer_name);
                        log_event("emergency_types.conf", "FILE_PARSING", msg);
                    }
                }

                entry = strtok(NULL, ";");
            }

            // Se almeno un rescuer valido è stato trovato, memorizza il tipo
            if (etype_temp.rescuers_req_number > 0) {
                types[count++] = etype_temp;
                snprintf(msg, MSG_SIZE, "Riga %d: %s", riga, line);
                log_event("emergency_types.conf", "FILE_PARSING", msg);
            } else {
                // Altrimenti, ignora e libera la memoria allocata
                free(etype_temp.emergency_desc);
                free(etype_temp.rescuers);
                snprintf(msg, MSG_SIZE, "Riga %d ignorata: nessun rescuer valido", riga);
                log_event("emergency_types.conf", "FILE_PARSING", msg);
            }

        } else {
            // Riga malformata (non riconosciuta da sscanf)
            snprintf(msg, MSG_SIZE, "Riga %d ignorata: %s", riga, line);
            log_event("emergency_types.conf", "FILE_PARSING", msg);
        }

        riga++;
    }

    log_event("parse_emergency_types.c", "FILE_PARSING", "Parsing completato con successo");

    // Libera buffer getline e chiude il file
    if (line) free(line);
    fclose(file);
    log_event("parse_emergency_types.c", "FILE_PARSING", "Chiusura del file emergency_types.conf");

    // Scrive i dati raccolti nella struttura di output
    emergency_data->types = types;
    emergency_data->num_types = count;

    return 0;
}


// Funzione che libera tutta la memoria associata alla struttura emergency_data_t.
void free_emergency_types(emergency_data_t *data) {
    // Controlla se il puntatore è valido
    if (!data || !data->types) return;

    // Libera ogni tipo di emergenza contenuto nell'array
    for (int i = 0; i < data->num_types; ++i) {
        emergency_type_t *etype = &data->types[i];
        // Libera la descrizione dell'emergenza (stringa)
        if (etype->emergency_desc) {
            free(etype->emergency_desc);
            etype->emergency_desc = NULL;
        }
        // Libera l'array dei rescuer richiesti
        if (etype->rescuers) {
            free(etype->rescuers);
            etype->rescuers = NULL;
        }
    }

    // Libera l'array dei tipi di emergenza
    free(data->types);
}


// Funzione che stampa a schermo tutti i tipi di emergenza 
// contenuti nella struttura emergency_data_t.
void print_emergency_types(const emergency_data_t *data) {
    if (!data || !data->types || data->num_types == 0) {
        printf("Nessun tipo di emergenza da stampare.\n");
        return;
    }
    printf("===== Elenco dei tipi di emergenza =====\n");
    for (int i = 0; i < data->num_types; ++i) {
        const emergency_type_t *etype = &data->types[i];

        printf("Emergenza Tipo %d: %s\n", i + 1, etype->emergency_desc);
        printf("  Priorità: %d\n", etype->priority);
        printf("  Richieste di soccorritori:\n");

        for (int j = 0; j < etype->rescuers_req_number; ++j) {
            const rescuer_request_t *req = &etype->rescuers[j];
            printf("    - %s: %d unità, durata %d secondi\n",
                   req->type->rescuer_type_name,
                   req->required_count,
                   req->time_to_manage);
        }
        printf("\n");
    }
}
