#include <stdlib.h>
#include <string.h>
#include <limits.h>
#include "intent.h"
#include "rescuers.h"
#include "logger.h"
#include "worker_thread.h"


// Funzione che inizializza la tabella degli intenti
void init_intent_table(intent_table_t *table) {
    // Inizializza la dimensione a 0 (nessun intent presente)
    table->size = 0;
    // Inizializza il mutex associato alla tabella
    mtx_init(&table->mutex, mtx_plain);
    // Imposta tutti gli slot della tabella a NULL
    for (int i = 0; i < MAX_INTENT_ENTRIES; ++i) {
        table->items[i] = NULL;
    }
}

// Funzione che registra un nuovo intento nella intent table
// table: puntatore alla tabella degli intent
// intent: puntatore all'intent da aggiungere
// Ritorna 0 se l'aggiunta ha successo, -1 in caso di errore o tabella piena
int register_intent(intent_table_t *table, intent_t *intent) {
    if (!intent || !table) return -1;
    // Acquisisce il lock per accedere in mutua esclusione alla tabella
    mtx_lock(&table->mutex);
    // Verifica se c'è spazio nella tabella
    if (table->size >= MAX_INTENT_ENTRIES) {
        mtx_unlock(&table->mutex);
        return -1;
    }
    // Inserisce l'intento nella prima posizione libera e incrementa la dimensione
    table->items[table->size++] = intent;
    // Rilascia il lock
    mtx_unlock(&table->mutex);
    return 0;
}

// Funzione che aggiorna un intent esistente nella intent table
// table: puntatore alla tabella degli intent
// new_intent: nuovo intent con lo stesso ID di uno già presente
// Ritorna 0 se l'aggiornamento ha successo, -1 se l'intent non viene trovato
int update_intent(intent_table_t *table, intent_t *new_intent) {
    if (!table || !new_intent) return -1;
    // Acquisisce il lock per accesso esclusivo alla tabella
    mtx_lock(&table->mutex);
    // Cerca un intent con lo stesso ID
    for (int i = 0; i < table->size; ++i) {
        if (table->items[i] && table->items[i]->id == new_intent->id) {
            // Sostituisce il vecchio intent con quello nuovo
            free(table->items[i]); // Libera memoria del vecchio intent
            table->items[i] = new_intent; // Assegna il nuovo intent
            mtx_unlock(&table->mutex);
            return 0;
        }
    }
    // Intent con quell'ID non trovato
    mtx_unlock(&table->mutex);
    return -1; 
}

// Funzione per registrare o aggiornare un intent nella intent table
// table: puntatore alla tabella degli intenti
// e: emergenza da cui creare l'intent
// rdata: dati dei soccorritori disponibili
// first_time: 1 se si tratta di una nuova registrazione, 0 se è un aggiornamento
// Ritorna 0 in caso di successo, -1 in caso di errore
int refresh_intent(intent_table_t *table, emergency_withID_t *e, rescuer_data_t *rdata, int first_time) {
    // Crea un nuovo intento basato sull'emergenza corrente
    intent_t *intent = create_intent_from_emergency(e, rdata);
    if (!intent) {
        log_event_id(e->id, "INTENT", "Creazione intent fallita.");
        return -1;
    }

    int res;
    if (first_time) {
        // Prima registrazione dell'intent
        res = register_intent(table, intent);
        if (res != 0) {
            log_event_id(e->id, "INTENT", "Registrazione intent fallita.");
            free(intent);
            return -1;
        }
    } else {
        // Aggiornamento dell'intent esistente
        res = update_intent(table, intent);
        if (res != 0) {
            log_event_id(e->id, "INTENT", "Aggiornamento intent fallito.");
            free(intent);
            return -1;
        }
    }
    return 0;
}


// Funzione che rimuove un intento dalla intent table dato l'ID dell'emergenza
// table: puntatore alla tabella degli intent
// emergency_id: ID dell'emergenza da disregistrare
void unregister_intent(intent_table_t *table, int emergency_id) {
    mtx_lock(&table->mutex);
    // Cerca l'intent con l'ID specificato
    for (int i = 0; i < table->size; ++i) {
        if (table->items[i] && table->items[i]->id == emergency_id) {
            // Libera la memoria dell'intento (allocato da funzione 
            // create_intent_from_emergency)
            free(table->items[i]);
            // Riempie il buco spostando l'ultimo elemento in questa posizione
            table->items[i] = table->items[table->size - 1];
            table->items[table->size - 1] = NULL;
            table->size--;
            break;
        }
    }
    mtx_unlock(&table->mutex);
}


// Funzione  che verifica se due intent sono in conflitto
// a, b: puntatori ai due intent da confrontare
// Due intent sono in conflitto se condividono almeno un twin_id
// Ritorna 1 se esiste conflitto, 0 altrimenti
int has_conflict(intent_t *a, intent_t *b) {
    for (int i = 0; i < a->twin_count; ++i) {
        for (int j = 0; j < b->twin_count; ++j) {
            if (a->twin_ids[i] == b->twin_ids[j]) {
                return 1;
            }
        }
    }
    return 0;
}


// Funzione che verifica se un'emergenza può procedere con l'assegnazione delle risorse
// table: puntatore alla intent table
// emergency_id: ID dell'emergenza da valutare
// Ritorna 1 se può procedere, 0 se deve aspettare per conflitti o priorità inferiori
int can_proceed(intent_table_t *table, int emergency_id) {
    int res = 1;
    intent_t *candidate = NULL;
    
    mtx_lock(&table->mutex);

    // Trova l'intent corrispondente all'ID dell'emergenza
    for (int i = 0; i < table->size; ++i) {
        if (table->items[i] && table->items[i]->id == emergency_id) {
            candidate = table->items[i];
            break;
        }
    }
    // Se l'intent non esiste, non può procedere
    if (!candidate) {
        mtx_unlock(&table->mutex);
        return 0;
    }

    // Controlla se ci sono conflitti con altri intent
    for (int i = 0; i < table->size; ++i) {
        intent_t *other = table->items[i];
        if (!other || other->id == candidate->id) continue;

        // Se c'è conflitto tra twin_ids
        if (has_conflict(candidate, other)) {
            // Bloccato se l'altro ha priorità maggiore
            if (other->priority > candidate->priority) {
                res = 0;
                break;
            }
            // Se hanno la stessa priorità, applica criterio FIFO
            if (other->priority == candidate->priority) {
                // Se l'altro ha timestamp minore ed è ancora nel periodo di precedenza
                if (other->timestamp < candidate->timestamp &&
                    candidate->timestamp - other->timestamp < WINDOW_PERIOD_SEC) {
                    res = 0;
                    break;
                }
                // Altrimenti, si può procedere
            }
        }
    }

    mtx_unlock(&table->mutex);
    return res;
}


// Funzione che genera un nuovo intent a partire da una specifica emergenza
// e: puntatore all'emergenza con ID e informazioni necessarie
// rdata: puntatore alla lista dei rescuers disponibili (digital twins)
// Ritorna un puntatore all'intent appena creato, o NULL in caso di errore
intent_t *create_intent_from_emergency(const emergency_withID_t *e, const rescuer_data_t *rdata) {
    if (!e || !rdata) return NULL;

    // Alloca memoria per il nuovo intent
    intent_t *intent = malloc(sizeof(intent_t));
    if (!intent) return NULL;

    // Inizializza i campi principali dell'intent
    intent->id = e->id;
    intent->priority = e->emergency.type.priority;
    intent->timestamp = e->emergency.time;
    intent->twin_count = 0;

    // Calcola la deadline in base alla priorità dell'emergenza
    time_t deadline = e->emergency.time +
        (intent->priority == 1 ? TIMEOUT_PRIORITY_1 :
         intent->priority == 2 ? TIMEOUT_PRIORITY_2 : TIMEOUT_MAX);

    time_t now = time(NULL);  

    // Scorre tutti i rescuers digital twins
    for (int i = 0; i < rdata->num_twins; ++i) {
        rescuer_digital_twin_t *t = &rdata->twins[i];

        // Considera solo i rescuers del tipo richiesto dall'emergenza
        int tipo_richiesto = 0;
        for (int j = 0; j < e->emergency.type.rescuers_req_number; ++j) {
            const char *richiesto = e->emergency.type.rescuers[j].type->rescuer_type_name;
            if (strcmp(t->rescuer->rescuer_type_name, richiesto) == 0) {
                tipo_richiesto = 1;
                break;
            }
        }
        if (!tipo_richiesto) continue;

        // Verifica se il rescuer può arrivare entro la deadline
        int dist = abs(t->x - e->emergency.x) + abs(t->y - e->emergency.y);
        int travel_time = (dist + t->rescuer->speed - 1) / t->rescuer->speed;

        if (now + travel_time <= deadline && intent->twin_count < MAX_TWINS) {
            intent->twin_ids[intent->twin_count++] = t->id;
        }
    }

    return intent;
}



// Funzione che libera tutta la memoria associata alla tabella degli intent
void free_intent_table(intent_table_t *table) {
    if (!table) return;

    // Blocca l'accesso concorrente alla tabella
    mtx_lock(&table->mutex);

    for (int i = 0; i < table->size; ++i) {
        if (table->items[i]) {
            free(table->items[i]);
            table->items[i] = NULL;
        }
    }
    table->size = 0;
    mtx_unlock(&table->mutex);
    mtx_destroy(&table->mutex);
}
