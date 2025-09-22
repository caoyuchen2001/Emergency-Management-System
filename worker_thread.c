#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <math.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <time.h>
#include <limits.h>
#include <threads.h>
#include "scall.h"
#include "logger.h"
#include "env.h"
#include "rescuers.h"
#include "emergency_types.h"
#include "emergency.h"
#include "worker_thread.h"

#define MAX_MSG_SIZE 512
#define NAME_SIZE 64



// Funzione che verifica se un'emergenza è raggiungibile entro i limiti di tempo definiti dalla priorità.
// Per ogni tipo di soccorritore richiesto, controlla se esiste un numero sufficiente di gemelli digitali
// che possono raggiungere la posizione dell'emergenza prima della scadenza (deadline).
// Se anche un solo tipo non ha abbastanza soccorritori raggiungibili, l'emergenza viene marcata come TIMEOUT.
// Restituisce 1 se tutti i requisiti sono soddisfatti, altrimenti 0.
int check_reachability(emergency_withID_t *e, rescuer_data_t *rdata)
{
    emergency_t *em = &e->emergency;
    emergency_type_t *etype = &em->type;
    // Calcola il tempo massimo disponibile in base alla priorità
    time_t deadline;
    if (etype->priority == 1) {
        deadline = em->time + 30;
    } else if (etype->priority == 2) {
        deadline = em->time + 10;
    } else {
        deadline = INT_MAX;
    }

    time_t now = time(NULL);
    // Per ogni tipo di soccorritore richiesto
    for (int i = 0; i < etype->rescuers_req_number; ++i)
    {
        rescuer_request_t *req = &etype->rescuers[i];
        int reachable_count = 0;
        // Conta quanti twins di quel tipo possono arrivare in tempo
        for (int j = 0; j < rdata->num_twins; ++j)
        {
            rescuer_digital_twin_t *twin = &rdata->twins[j];
            // Salta se non è del tipo richiesto
            if (strcmp(twin->rescuer->rescuer_type_name, req->type->rescuer_type_name) != 0)
                continue;
            // Calcola il tempo di arrivo del twin
            int dist = abs(twin->x - em->x) + abs(twin->y - em->y);
            int tempo_arrivo = (dist + twin->rescuer->speed - 1) / twin->rescuer->speed;

            // Se può arrivare entro la deadline, conta
            if (now + tempo_arrivo <= deadline)
            {
                reachable_count++;
                if (reachable_count >= req->required_count)
                    break;
            }
        }

        // Se non ci sono abbastanza twin raggiungibili per questo tipo -> TIMEOUT
        if (reachable_count < req->required_count)
        {
            char msg[256];
            snprintf(msg, sizeof(msg),
                     "Timeout per distanza, richiesto '%s': %d disponibili entro il limite, trovati %d nella zona",
                     req->type->rescuer_type_name, req->required_count, reachable_count);
            em->status = TIMEOUT;
            log_event_id(e->id, "EMERGENCY_STATUS", msg);
            return 0; // Timeout per distanza
        }
    }
    // Tutti i tipi richiesti sono raggiungibili entro il tempo limite
    return 1;
}


// Funzione che verifica se l'emergenza ha superato il limite massimo di tempo disponibile (deadline).
// Se il tempo attuale supera la deadline, lo stato dell'emergenza viene impostato a TIMEOUT.
// Restituisce 1 se l'emergenza è ancora valida, 0 se è scaduta.
int check_deadline(emergency_withID_t *e)
{
    emergency_t *em = &e->emergency;
    emergency_type_t *etype = &em->type;

    time_t now = time(NULL);
    // Calcola il tempo massimo disponibile in base alla priorità
    time_t deadline;
    if (etype->priority == 1) {
        deadline = em->time + 30;
    } else if (etype->priority == 2) {
        deadline = em->time + 10;
    } else {
        deadline = INT_MAX;
    }

    if (now > deadline)
    {
        log_event_id(e->id, "EMERGENCY_STATUS", "Timeout per carenza, scaduto tempo massimo disponibile");
        em->status = TIMEOUT;
        return 0;
    }

    return 1;
}



// Funzione che assegna un numero sufficiente di gemelli digitali (twin) ad un'emergenza.
// Restituisce 1 se l'assegnazione ha successo, 0 altrimenti.
int assign_rescuers_to_emergency(emergency_withID_t *e,
                                 rescuer_data_t *rdata,
                                 rescuer_digital_twin_t **assigned_twins,
                                 mtx_t *twin_locks){
    emergency_t *em = &e->emergency;
    emergency_type_t *etype = &em->type;

    time_t now = time(NULL);
    time_t deadline;
    char msg[MAX_MSG_SIZE];
    int total_assigned = 0;

    // Calcola la deadline in base alla priorità
    if (etype->priority == 1) {
        deadline = em->time + TIMEOUT_PRIORITY_1;
    } else if (etype->priority == 2) {
        deadline = em->time + TIMEOUT_PRIORITY_2;
    } else {
        deadline = INT_MAX;
    }

    // Step 1: Selezione dei twin IDLE e raggiungibili, ordinati per distanza
    for (int i = 0; i < etype->rescuers_req_number; ++i){
        rescuer_request_t *req = &etype->rescuers[i];
        twin_candidate_t candidates[MAX_TWINS];
        int candidate_count = 0;

        // Raccoglie tutti i twin candidati
        for (int j = 0; j < rdata->num_twins; ++j){
            rescuer_digital_twin_t *twin = &rdata->twins[j];

            if (twin->status != IDLE)
                continue;
            if (strcmp(twin->rescuer->rescuer_type_name, req->type->rescuer_type_name) != 0)
                continue;
            // Calcola tempo stimato di arrivo
            int dist = abs(twin->x - em->x) + abs(twin->y - em->y);
            int travel_t = (dist + twin->rescuer->speed - 1) / twin->rescuer->speed;
            if (now + travel_t > deadline)
                continue;
            // Salva come candidato
            candidates[candidate_count].twin = twin;
            candidates[candidate_count].travel_time = travel_t;
            candidate_count++;
        }
        // Ordina i candidati per tempo di viaggio crescente
        for (int x = 0; x < candidate_count - 1; ++x) {
            for (int y = x + 1; y < candidate_count; ++y) {
                if (candidates[y].travel_time < candidates[x].travel_time){
                    twin_candidate_t temp = candidates[x];
                    candidates[x] = candidates[y];
                    candidates[y] = temp;
                }
            }
        }
        // Verifica se ci sono abbastanza twin disponibili per questo tipo
        if (candidate_count < req->required_count){
            return 0; // Risorse insufficienti
        }
        // Seleziona i primi N twin
        for (int k = 0; k < req->required_count; ++k){
            assigned_twins[total_assigned++] = candidates[k].twin;
        }
    }


    // Step 2: Ordina i twin per ID (evita deadlock nei lock multipli)
    for (int i = 0; i < total_assigned - 1; ++i) {
        for (int j = i + 1; j < total_assigned; ++j) {
            if (assigned_twins[j]->id < assigned_twins[i]->id) {
                rescuer_digital_twin_t *tmp = assigned_twins[i];
                assigned_twins[i] = assigned_twins[j];
                assigned_twins[j] = tmp;
            }
        }
    }

    // Step 3: Prova a prendere tutti i lock (con trylock)
    for (int i = 0; i < total_assigned; ++i) {
        rescuer_digital_twin_t *t = assigned_twins[i];

        if (mtx_trylock(&twin_locks[assigned_twins[i]->id - 1]) != thrd_success) {
            // Fallimento: rilascio dei lock già acquisiti
            printf("Assegnazione fallitaTwin %d occupato\n", assigned_twins[i]->id);
            for (int j = i - 1; j >= 0; j--) {
                mtx_unlock(&twin_locks[assigned_twins[j]->id - 1]);
            }
            return 0;
        }
        // Dopo aver preso il lock, ricontrolla che sia ancora IDLE
        if (t->status != IDLE) {
            printf("Twin %d non è più IDLE\n", t->id);
            for (int j = i; j >= 0; j--){
                mtx_unlock(&twin_locks[assigned_twins[j]->id - 1]);
            }
            return 0;
        }
    }

    // Step 4: Tutti i lock acquisiti con successo: conferma assegnazione
    em->rescuer_count = total_assigned;
    em->status = ASSIGNED;
    // Salva copia dei twin (deep copy)
    em->rescuers_dt = malloc(sizeof(rescuer_digital_twin_t) * total_assigned);
    if (!em->rescuers_dt){
        log_event_id(e->id, "ERROR", "Errore in malloc per rescuers_dt");
        // Rilascia i lock presi prima di uscire
        for (int i = 0; i < total_assigned; ++i){
            mtx_unlock(&twin_locks[assigned_twins[i]->id - 1]);
        }
        return 0;
    }
    for (int i = 0; i < total_assigned; ++i) {
        em->rescuers_dt[i] = *(assigned_twins[i]); // deep copy
    }
    // Log cambio di stato emergenza
    log_event_id(e->id, "EMERGENCY_STATUS", "Stato cambiato a ASSIGNED");


    // Step 5: Raggruppamento per log e aggiornamento stato dei twin
    group_t groups[MAX_TYPES];
    int group_count = 0;

    for (int i = 0; i < total_assigned; ++i) {
        rescuer_digital_twin_t *twin = assigned_twins[i];
        twin->status = EN_ROUTE_TO_SCENE;

        // Log individuale del cambiamento di stato
        char id_str[NAME_SIZE];
        snprintf(id_str, sizeof(id_str), "%s %d", twin->rescuer->rescuer_type_name, twin->id);
        snprintf(msg, sizeof(msg), "Assegnato all'emergenza %d, stato EN_ROUTE_TO_SCENE", e->id);
        log_event(id_str, "RESCUER_STATUS", msg);
        // Raggruppa per tipo
        int found = 0;
        for (int g = 0; g < group_count; ++g) {
            if (strcmp(groups[g].type, twin->rescuer->rescuer_type_name) == 0) {
                groups[g].ids[groups[g].count++] = twin->id;
                found = 1;
                break;
            }
        }
        if (!found) {
            strncpy(groups[group_count].type, twin->rescuer->rescuer_type_name, NAME_SIZE);
            groups[group_count].ids[0] = twin->id;
            groups[group_count].count = 1;
            group_count++;
        }
        // Rilascia lock dopo assegnazione
        mtx_unlock(&twin_locks[twin->id - 1]);
    }

    // Step 6: Log assegnazione in formato {Tipo id,id}{Tipo2 id,id}
    char summary[MAX_MSG_SIZE] = {0};
    for (int g = 0; g < group_count; ++g) {
        strncat(summary, "{", sizeof(summary) - strlen(summary) - 1);
        strncat(summary, groups[g].type, sizeof(summary) - strlen(summary) - 1);
        strncat(summary, " ", sizeof(summary) - strlen(summary) - 1);
        for (int k = 0; k < groups[g].count; ++k) {
            char buf[16];
            snprintf(buf, sizeof(buf), "%d", groups[g].ids[k]);
            strncat(summary, buf, sizeof(summary) - strlen(summary) - 1);
            if (k < groups[g].count - 1) {
                strncat(summary, ",", sizeof(summary) - strlen(summary) - 1);
            }
        }
        strncat(summary, "}", sizeof(summary) - strlen(summary) - 1);
    }
    log_event_id(e->id, "ASSIGNMENT", summary);
    return 1;
}


// Funzione che simula l'intervento una volta assegnati i twin.
// Crea un thread per ogni twin assegnato e un thread per l'emergenza stessa.
// Utilizza una struttura di sincronizzazione condivisa per coordinare l'arrivo e il rientro dei twin.
// Attende la fine di tutti i thread prima di terminare.
void handle_emergency(emergency_withID_t *e,
                      rescuer_digital_twin_t **assigned_twins) {

    // Numero totale di twin assegnati
    int n = e->emergency.rescuer_count;

     // Alloca e inizializza la struttura di sincronizzazione condivisa
    emergency_sync_t *sync = malloc(sizeof(emergency_sync_t));
    sync->arrived = 0; // Contatore dei twin arrivati sul luogo
    sync->returned = 0; // Contatore dei twin tornati alla base
    mtx_init(&sync->mutex, mtx_plain); // Mutex di protezione
    cnd_init(&sync->all_arrived); // Condizione per l'arrivo
    cnd_init(&sync->all_returned); // Condizione per il rientro

    // Crea un thread per ciascun twin assegnato
    thrd_t twin_threads[n];
    for (int i = 0; i < n; ++i) {
        twin_arg_t *arg = malloc(sizeof(twin_arg_t));
        arg->twin = assigned_twins[i]; // Puntatore al twin assegnato
        arg->e = e; // Puntatore all’emergenza condivisa
        arg->sync = sync; // Puntatore alla struttura di sincronizzazione
        // Avvia il thread del twin
        thrd_create(&twin_threads[i], run_twin_task, arg);
    }

    // Crea e avvia un thread separato per la gestione dell’emergenza
    em_arg_t *em_arg = malloc(sizeof(em_arg_t));
    em_arg->e = e;
    em_arg->sync = sync;
    thrd_t e_thread;
    thrd_create(&e_thread, run_emergency_task, em_arg);

    // Attende la terminazione di tutti i thread dei twin
    for (int i = 0; i < n; ++i) {
        thrd_join(twin_threads[i], NULL);
    }
    // Attende la terminazione del thread dell’emergenza
    thrd_join(e_thread, NULL);
}



// Simula il comportamento di un twin durante l'intervento.
// Il twin si muove verso il luogo dell'emergenza, attende che tutti gli altri arrivano,
// lavora per il tempo richiesto, ritorna alla base e ripristina lo stato IDLE.
// Utilizza una struttura di sincronizzazione condivisa per coordinarsi con gli altri twin.
int run_twin_task(void *arg) {
    twin_arg_t *a = (twin_arg_t *)arg;
    rescuer_digital_twin_t *t = a->twin;
    emergency_t *em = &a->e->emergency;
    emergency_sync_t *sync = a->sync;

    char id_str[NAME_SIZE], msg[MAX_MSG_SIZE];
    snprintf(id_str, sizeof(id_str), "%s %d", t->rescuer->rescuer_type_name, t->id);

    // Step 1: Simula lo spostamento verso il luogo dell'emergenza
    int home_x = em->x;
    int home_y = em->y;
    int dist = abs(t->x - em->x) + abs(t->y - em->y);
    int travel_t = (dist + t->rescuer->speed - 1) / t->rescuer->speed;
    sleep(travel_t); // tempo di viaggio simulato

    // Aggiorna posizione e stato ON_SCENE
    t->x = em->x;
    t->y = em->y;
    t->status = ON_SCENE;
    snprintf(msg, sizeof(msg), "Stato cambiato a ON_SCENE per emergenza %d", a->e->id);
    log_event(id_str, "RESCUER_STATUS", msg);

    // Notifica che è arrivato, attende che tutti arrivano
    mtx_lock(&sync->mutex);
    sync->arrived++;
    // L'ultimo che arriva risveglia tutti
    if (sync->arrived == em->rescuer_count){
        cnd_broadcast(&sync->all_arrived); 
        mtx_unlock(&sync->mutex);
    } else { // attende a meno che non sia l'ultimo
        cnd_wait(&sync->all_arrived, &sync->mutex);
        mtx_unlock(&sync->mutex);
    }

    // Step 2: Simula il tempo di intervento sul posto
    int manage_time = 0;
    for (int j = 0; j < em->type.rescuers_req_number; ++j) {
        if (strcmp(em->type.rescuers[j].type->rescuer_type_name, t->rescuer->rescuer_type_name) == 0) {
            manage_time = em->type.rescuers[j].time_to_manage;
            break;
        }
    }
    sleep(manage_time);

    // Step 3: Aggiorna stato: ritorno alla base
    t->status = RETURNING_TO_BASE;
    snprintf(msg, sizeof(msg), "Stato cambiato a RETURNING_TO_BASE per emergenza %d", a->e->id);
    log_event(id_str, "RESCUER_STATUS", msg);

    // Notifica il rientro
    mtx_lock(&sync->mutex);
    sync->returned++;
    // L'ultimo che finisce lavoro segnala il thread dell'emergenza
    if (sync->returned == a->e->emergency.rescuer_count)
        cnd_signal(&sync->all_returned);
    mtx_unlock(&sync->mutex);

    // Step 4: Simula il ritorno alla base
    sleep(travel_t);

    t->x = home_x;
    t->y = home_y;
    t->status = IDLE;
    snprintf(msg, sizeof(msg), "Stato cambiato a IDLE dopo completamento emergenza %d", a->e->id);
    log_event(id_str, "RESCUER_STATUS", msg);

    // Libera argomenti allocati
    free(a);
    return 0;
}



// Simula il comportamento di un'emergenza durante l'intervento.
// Attende che tutti i twin arrivano sul luogo, poi passa lo stato a IN_PROGRESS.
// Quando tutti i twin hanno finito di lavorare, aggiorna lo stato a COMPLETED.
int run_emergency_task(void *arg)
{
    em_arg_t *a = (em_arg_t *)arg;
    emergency_t *em = &a->e->emergency;
    emergency_sync_t *sync = a->sync;

    // Attende che tutti i twin arrivano
    mtx_lock(&sync->mutex);
    while (sync->arrived < em->rescuer_count)
        cnd_wait(&sync->all_arrived, &sync->mutex);
    // Quando tutti i twin sono sul posto -> IN_PROGRESS
    em->status = IN_PROGRESS;
    log_event_id(a->e->id, "EMERGENCY_STATUS", "Stato cambiato a IN_PROGRESS");
    mtx_unlock(&sync->mutex);

    // Attende che tutti i twin finiscono di lavorare
    mtx_lock(&sync->mutex);
    while (sync->returned < em->rescuer_count)
        cnd_wait(&sync->all_returned, &sync->mutex);
    // Quando tutti hanno finito -> COMPLETED
    em->status = COMPLETED;
    em->rescuer_count = 0;
    free(em->rescuers_dt);
    em->rescuers_dt = NULL;
    log_event_id(a->e->id, "EMERGENCY_STATUS", "Stato cambiato a COMPLETED");
    mtx_unlock(&sync->mutex);

    // Libera risorse di sincronizzazione
    cnd_destroy(&sync->all_arrived);
    cnd_destroy(&sync->all_returned);
    mtx_destroy(&sync->mutex);
    free(sync);
    free(a);
    return 0;
}



// Thread dedicato alla gestione di un'emergenza.
// Spiegato dettagliatamente in report sezione 2.2
int worker_thread(void *arg) {
    worker_args_t *args = (worker_args_t *)arg;
    emergency_withID_t *e = args->emergency;
    rescuer_data_t *rdata = args->rdata;
    intent_table_t *itable = args->itable;
    mtx_t *twin_locks = args->twin_locks;
    // Contatore per refresh intent
    int replace_intent_counter = 0;
    // Flag per registrazione iniziale intent
    int first_time = 1;

    // Ciclo principale del worker
    while (1) {

        // Step 1: Controlla se ci sono abbastanza numero di twin 
        // raggiungibili entro il tempo limite 
        if (!check_reachability(e, rdata)){
            free_emergency_instance(e);
            free(args);
            return 0;
        }

        // Step 2: Controlla se il tempo deadline e' scaduto 
        if (!check_deadline(e)) {
            unregister_intent(itable, e->id);
            free_emergency_instance(e);
            free(args);
            return 0;
        }

        // Step 3: Alla prima volta si registra un intent, dalla 
        // seconda in poi si aggiorna l'intent ogni 1 secondo
        if (first_time || replace_intent_counter >= INTENT_REFRESH_INTERVAL) {
            if (refresh_intent(itable, e, rdata, first_time) != 0) {
                unregister_intent(itable, e->id);
                free_emergency_instance(e);
                free(args);
                return 0;
            }
            first_time = 0;
            replace_intent_counter = 0;
        }

        // Step 4: Determina se l'emergenza corrente puo' entrare 
        // nella fase di assegnazione, riprovare dopo 5ms altrimenti
        if (!can_proceed(itable, e->id)) {
            thrd_sleep(&(struct timespec){.tv_sec = 0, .tv_nsec = 5 * 1000000}, NULL); // 5ms
            replace_intent_counter++;
            continue;
        }

        // Step 5: Tenta di assegnare le risorse, in caso fallito 
        // riprovare dopo 5ms
        rescuer_digital_twin_t *assigned_twins[MAX_TWINS];
        if (assign_rescuers_to_emergency(e, rdata, assigned_twins, twin_locks)){
            // elimina l'intent se ha successo
            unregister_intent(itable, e->id);
            // Step 6: Modella il comportamento temporale dei twin 
            // assegnati e dell'emergenza
            handle_emergency(e, assigned_twins);
            free_emergency_instance(e);
            free(args);
            return 0;
        }
        else {
            // Assegnazione fallita -> aspetta e riprova
            thrd_sleep(&(struct timespec){.tv_sec = 0, .tv_nsec = 5 * 1000000}, NULL); // 5ms
            replace_intent_counter++;
            continue;
        }
    }
}
