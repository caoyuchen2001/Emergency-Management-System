#define _POSIX_C_SOURCE 200809L
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "logger.h"
#include "scall.h"
#include "emergency.h"
#include "env.h"

#define MSG_SIZE 512

// Funzione che estrae una richiesta di emergenza da una stringa
// msg: stringa ricevuta dalla coda di messaggi (es. "Incendio 100 200 1715153512")
// req: puntatore alla struttura da riempire
// Ritorna 0 in caso di successo, -1 in caso di errore di formato
int parse_MQrequest(const char *msg, emergency_request_withID_t *req) {
    if (!msg || !req) {
        log_event("N/A", "MESSAGE_QUEUE", "Messaggio nullo o struttura req nulla");
        return -1;
    }

    // Log della ricezione
    char log_msg[MSG_SIZE];
    snprintf(log_msg, sizeof(log_msg), "Ricevuto messaggio MQ: %s", msg);
    log_event_id(req->id, "MESSAGE_QUEUE", log_msg);

    // Variabili temporanee
    char name[NAME_SIZE];
    int x, y;
    time_t timestamp;

    // Parsing del messaggio
    if (sscanf(msg, "%63s %d %d %ld", name, &x, &y, &timestamp) != 4) {
        snprintf(log_msg, sizeof(log_msg), "Formato messaggio non valido: %s", msg);
        log_event_id(req->id, "MESSAGE_QUEUE", log_msg);
        return -1;
    }

    // Copia dei valori nella struttura
    strcpy(req->req.emergency_name, name);
    req->req.x = x;
    req->req.y = y;
    req->req.timestamp = timestamp;

    snprintf(log_msg, sizeof(log_msg),
             "ID %d: Estratti -> tipo: %s, coordinate: (%d,%d), timestamp: %ld",
             req->id, req->req.emergency_name, x, y, timestamp);
    log_event_id(req->id, "MESSAGE_QUEUE", log_msg);

    return 0;
}



// Funzione che valida una richiesta di emergenza ricevuta dalla coda
// Controlla se il tipo di emergenza esiste, se le coordinate sono valide
// e se il timestamp non è nel futuro
// Ritorna 0 se la richiesta è valida, -1 altrimenti
int validate_MQrequest(const emergency_request_withID_t *req,
                       emergency_type_t *types,
                       int num_types,
                       const env_config_t *env) {
    char msg[MSG_SIZE];

    if (!req || !types || !env) {
        log_event("N/A", "MESSAGE_QUEUE", "Parametri nulli in validate_MQrequest");
        return -1;
    }

    const emergency_request_t *r = &req->req;

    // Controllo tipo emergenza
    int found = 0;
    for (int i = 0; i < num_types; ++i) {
        if (strcmp(r->emergency_name, types[i].emergency_desc) == 0) {
            found = 1;
            break;
        }
    }
    if (!found) {
        snprintf(msg, sizeof(msg), "Tipo emergenza sconosciuto: %s", r->emergency_name);
        log_event_id(req->id, "MESSAGE_QUEUE", msg);
        return -1;
    }

    // Controllo coordinate
    if (r->x < 0 || r->x > env->height || r->y < 0 || r->y > env->width) {
        snprintf(msg, sizeof(msg), "Coordinate fuori dai limiti: (%d,%d)", r->x, r->y);
        log_event_id(req->id, "MESSAGE_QUEUE", msg);
        return -1;
    }

    // Controllo timestamp
    time_t now = time(NULL);
    if (r->timestamp > now) {
        snprintf(msg, sizeof(msg), "Timestamp futuro non valido: %ld (ora: %ld)", r->timestamp, now);
        log_event_id(req->id, "MESSAGE_QUEUE", msg);
        return -1;
    }

    // Validazione completata con successo
    snprintf(msg, sizeof(msg), "Richiesta validata con successo");
    log_event_id(req->id, "MESSAGE_QUEUE", msg);

    return 0;
}



// Funzione che crea un'istanza di emergenza a partire da una richiesta con ID
// instance: puntatore alla struttura da inizializzare
// req: richiesta di emergenza ricevuta dalla coda
// types: array dei tipi di emergenza conosciuti
// num_types: numero di elementi in types
// Ritorna 0 in caso di successo, -1 in caso di errore
int create_emergency_instance(emergency_withID_t *instance,
                               const emergency_request_withID_t *req,
                               emergency_type_t *types,
                               int num_types) {
                                
    // Controlla validità dei parametri                           
    if (!instance || !req || !types || num_types <= 0) {
        log_event("N/A", "MESSAGE_QUEUE", "Parametri nulli in create_emergency_instance");
        return -1;
    }

    const emergency_request_t *r = &req->req;
    emergency_type_t *matched_type = NULL;

    // Cerca il tipo di emergenza corrispondente alla descrizione
    for (int i = 0; i < num_types; ++i) {
        if (strcmp(r->emergency_name, types[i].emergency_desc) == 0) {
            matched_type = &types[i];
            break;
        }
    }
    // Se il tipo non viene trovato, logga errore e termina
    if (!matched_type) {
        log_event_id(req->id, "MESSAGE_QUEUE", "Tipo emergenza non trovato");
        return -1;
    }

    // Copia del tipo nella nuova istanza
    emergency_type_t *copied_type = &instance->emergency.type;
    copied_type->priority = matched_type->priority;

    // Copia dinamica della descrizione dell'emergenza
    copied_type->emergency_desc = strdup(matched_type->emergency_desc);
    if (!copied_type->emergency_desc) {
        log_event_id(req->id, "MESSAGE_QUEUE", "malloc fallita per emergency_desc");
        return -1;
    }
    // Alloca spazio per l'array dei soccorritori richiesti
    copied_type->rescuers_req_number = matched_type->rescuers_req_number;
    copied_type->rescuers = malloc(sizeof(rescuer_request_t) * copied_type->rescuers_req_number);
    if (!copied_type->rescuers) {
        free(copied_type->emergency_desc);
        log_event_id(req->id, "ERROR", "malloc fallita per rescuers");
        return -1;
    }
    // Copia le informazioni per ciascun tipo di soccorritore richiesto
    for (int i = 0; i < copied_type->rescuers_req_number; ++i) {
        copied_type->rescuers[i].required_count = matched_type->rescuers[i].required_count;
        copied_type->rescuers[i].time_to_manage = matched_type->rescuers[i].time_to_manage;
        copied_type->rescuers[i].type = matched_type->rescuers[i].type;
    }

    // Inizializza gli altri campi della struttura emergency
    instance->id = req->id;
    instance->emergency.x = r->x;
    instance->emergency.y = r->y;
    instance->emergency.time = r->timestamp;
    instance->emergency.status = WAITING;
    instance->emergency.rescuer_count = 0;
    instance->emergency.rescuers_dt = NULL;

    // Logga la creazione dell'emergenza
    char msg[MSG_SIZE];
    snprintf(msg, sizeof(msg),
             "Creato oggetto emergency con tipo='%s', coord=(%d,%d), tempo=%ld",
             r->emergency_name, r->x, r->y, r->timestamp);
    log_event_id(req->id, "MESSAGE_QUEUE", msg);

    return 0;
}


// Funzione che converte uno stato di emergenza nel corrispondente nome testuale
// status: valore dell'enumerazione emergency_status_t
// Ritorna una stringa costante rappresentante lo stato
const char* emergency_status_str(emergency_status_t status) {
    switch (status) {
        case WAITING: return "WAITING";
        case ASSIGNED: return "ASSIGNED";
        case IN_PROGRESS: return "IN_PROGRESS";
        case PAUSED: return "PAUSED";
        case COMPLETED: return "COMPLETED";
        case CANCELED: return "CANCELED";
        case TIMEOUT: return "TIMEOUT";
        default: return "UNKNOWN";
    }
}

// Funzione che stampa a schermo le informazioni di una singola emergenza
// e: puntatore alla struttura contenente l'emergenza da stampare
// Se il puntatore è NULL, viene stampato un messaggio di errore
void print_emergency_instance(const emergency_withID_t *e) {
    if (!e) {
        printf("Istanza emergenza nulla.\n");
        return;
    }
    // Stampa i dettagli principali dell'emergenza
    printf("===== EMERGENZA ID %d =====\n", e->id);
    printf("Tipo:        %s\n", e->emergency.type.emergency_desc);
    printf("Priorità:    %d\n", e->emergency.type.priority);
    printf("Stato:       %s\n", emergency_status_str(e->emergency.status));
    printf("Coordinate:  (%d, %d)\n", e->emergency.x, e->emergency.y);
    printf("Timestamp:   %ld\n", e->emergency.time);
    printf("Rescuers:    %d assegnati\n", e->emergency.rescuer_count);
    printf("===========================\n\n");
}

// Funzione che libera la memoria allocata per un'istanza di emergenza
// e: puntatore all'istanza da deallocare
void free_emergency_instance(emergency_withID_t *e) {
    if (!e) return;
    // Libera l'array di digital twin assegnati (se presente)
    if (e->emergency.rescuers_dt) {
        free(e->emergency.rescuers_dt);
    }

    // Libera l'array dei requisiti di soccorritori
    if (e->emergency.type.rescuers) {
        free(e->emergency.type.rescuers);
    }

    // Libera la stringa che descrive il tipo di emergenza
    if (e->emergency.type.emergency_desc) {
        free(e->emergency.type.emergency_desc);
    }

    // Libera l'intera struttura se è stata allocata con malloc in main
    free(e);
}
