#define _POSIX_C_SOURCE 200809L
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <signal.h> 
#include <math.h>
#include <fcntl.h>
#include <mqueue.h>
#include <limits.h>
#include <errno.h>
#include <sys/wait.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <threads.h>
#include "scall.h"
#include "logger.h"
#include "env.h"
#include "rescuers.h"
#include "emergency_types.h"
#include "emergency.h"
#include "worker_thread.h"
#include "intent.h"


#define MAX_MSG_SIZE 512
#define NAME_SIZE 64
#define TIMEOUT_PRIORITY_1 30
#define TIMEOUT_PRIORITY_2 10
#define MQ_MAX_MSG 10
#define MQ_MSG_SIZE MAX_MSG_SIZE

volatile sig_atomic_t terminate_request = 0;

// Gestore del Segnale SIGINT
// Questa funzione viene chiamata quando il processo riceve SIGINT (Ctrl+C).
void sigint_handler(int signal_number) {
    // Imposta il flag per comunicare al main loop di terminare.
    terminate_request = 1;

    // Scrivi un messaggio *semplice* sulla console usando write()
    // (che è async-signal-safe, a differenza di printf).
    const char msg[] = "\nCtrl+C premuto! Avvio procedura di terminazione...\n";
    // Ignoriamo eventuali errori di write qui per semplicità del gestore
    write(STDOUT_FILENO, msg, sizeof(msg) - 1);
}


int main()
{
    // --- Inizializza il sistema di logging ---
    init_log();

    // --- Parsing del file di configurazione env.conf ----
    env_config_t config;
    log_event("main.c", "FILE_PARSING", "Avvio del parsing di env.conf");
    if (parse_env("env.conf", &config) != 0) {
        printf("Errore durante il parsing di env.conf\n");
        log_event("main.c", "FILE_PARSING", "Errore durante il parsing di env.conf");
        // clean up and exit
        free_env_config(&config);
        close_log();
        exit(EXIT_FAILURE);
    }
    print_env(&config);

    // --- Configura attributi della coda di messaggi ---
    struct mq_attr attr = {
        .mq_flags = 0,
        .mq_maxmsg = MQ_MAX_MSG,
        .mq_msgsize = MQ_MSG_SIZE,
        .mq_curmsgs = 0
    };
    // Apre la coda di messaggi (non bloccante)
    mqd_t mq = mq_open(config.queue_name, O_CREAT | O_RDONLY | O_NONBLOCK, 0666, &attr);
    if (mq == -1) {
        perror("mq_open");
        exit(EXIT_FAILURE);
    }
    log_event("main.c", "MESSAGE_QUEUE", "Coda di messaggi creata");

    // --- Parsing del file rescuers.conf ---
    rescuer_data_t rescuer_data;
    log_event("main.c", "FILE_PARSING", "Avvio del parsing di rescuers.conf");
    if (parse_rescuers("rescuers.conf", &rescuer_data) != 0) {
        printf("Errore durante il parsing di rescuers.conf\n");
        log_event("main.c", "FILE_PARSING", "Errore nel parsing di rescuers.conf");
        // clean up and exit
        free_env_config(&config);
        close_log();
        exit(EXIT_FAILURE);
    }
    print_rescuer_data(&rescuer_data);

    // --- Inizializza array di mutex per gestire accesso concorrente ai digital twin ---
    mtx_t twin_locks[MAX_TWINS];
    for (int i = 0; i < MAX_TWINS; ++i) {
        mtx_init(&twin_locks[i], mtx_plain);
    }

    // --- Parsing del file emergency_types.conf ---
    emergency_data_t emergency_data;
    log_event("main.c", "FILE_PARSING", "Avvio del parsing di emergency_types.conf");
    if (parse_emergency_types("emergency_types.conf", &rescuer_data, &emergency_data) != 0) {
        printf("Errore durante il parsing di emergency_types.conf\n");
        log_event("main.c", "FILE_PARSING", "Errore nel parsing di emergency_types.conf");
        // clean up and exit
        free_env_config(&config);
        free_rescuers_data(&rescuer_data);
        close_log();
        exit(EXIT_FAILURE);
    }
    print_emergency_types(&emergency_data);

    // --- Configurazione del gestore per SIGINT (Ctrl+C) ---
    struct sigaction sa_sigint;
    memset(&sa_sigint, 0, sizeof(sa_sigint)); // Azzera la struttura
    sa_sigint.sa_handler = sigint_handler; // Imposta la nostra funzione gestore
    sigemptyset(&sa_sigint.sa_mask); // Non bloccare altri segnali durante l'esecuzione del gestore
    sa_sigint.sa_flags = 0; // Non riavvia chiamate di sistema lente (come mq_receive) se interrotte
    // Registra il gestore SIGINT
    if (sigaction(SIGINT, &sa_sigint, NULL) == -1)
    {
        perror("sigaction fallita");
        // Proviamo comunque a chiudere le risorse
        printf("Esecuzione cleanup.\n");
        mq_close(mq);
        mq_unlink(config.queue_name);
        free_env_config(&config);
        free_rescuers_data(&rescuer_data);
        free_emergency_types(&emergency_data);
        for (int i = 0; i < MAX_TWINS; i++)
        {
            mtx_destroy(&twin_locks[i]);
        }
        close_log();
        printf("Cleanup completato. Uscita.\n");
        exit(EXIT_FAILURE);
    }
    printf("Gestore SIGINT installato. Inizio ciclo principale...\n");

    // --- Inizializza tabella degli intenti e id generator ---
    static int emergency_id = 1;
    intent_table_t itable;
    init_intent_table(&itable);
    // Buffer per ricezione messaggi
    char buffer[MAX_MSG_SIZE];

    // --- Ciclo principale: ricezione messaggi e creazione worker thread ---
    while(terminate_request==0) {
    
        ssize_t bytes_read = mq_receive(mq, buffer, MQ_MSG_SIZE, NULL);
        if (bytes_read == -1) {
            if (errno == EAGAIN) {
                // Nessun messaggio disponibile, attende brevemente
                thrd_sleep(&(struct timespec){.tv_sec=0, .tv_nsec=5 * 1000000}, NULL); // 5ms
                continue;
            }
            else 
            {
                perror("mq_receive");
                continue;
            }
        }

        // Alloca richiesta ed emergenza
        emergency_request_withID_t *req = malloc(sizeof(emergency_request_withID_t));
        emergency_withID_t *inst = malloc(sizeof(emergency_withID_t));
        if (!req || !inst) {
            log_event("main.c", "ALLOC_ERROR", "malloc fallita per request o instanza");
            continue;
        }

        req->id = emergency_id++;

        // Parsing e validazione del messaggio ricevuto
        if (parse_MQrequest(buffer, req) != 0 ||
            validate_MQrequest(req, emergency_data.types, emergency_data.num_types, &config) != 0 ||
            create_emergency_instance(inst, req, emergency_data.types, emergency_data.num_types) != 0) {

            log_event("main.c", "PARSING/VALIDATION_ERROR", buffer);
            free(req);
            free(inst);
            continue;
        }

        free(req);
        print_emergency_instance(inst);

        // Alloca argomenti e crea un worker thread per gestire l'emergenza
        worker_args_t *args = malloc(sizeof(worker_args_t));
        if (!args)
        {
            log_event("main.c", "ALLOC_ERROR", "malloc fallita per worker args");
            free_emergency_instance(inst);
            continue;
        }
        args->emergency = inst;
        args->itable = &itable;
        args->rdata = &rescuer_data;
        args->twin_locks = twin_locks;

        thrd_t t;
        if (thrd_create(&t, worker_thread, args) != thrd_success) {
            log_event("main.c", "THREAD_ERROR", "Creazione thread fallita");
            free_emergency_instance(inst);
            free(args);
            continue;
        }

        // Detach del thread per evitare memory leak
        thrd_detach(t);
    }

    // Cleanup al termine del ciclo (SIGINT ricevuto)
    printf("Flag di terminazione rilevato.\n");
    printf("Esecuzione cleanup prima della terminazione.\n");
    // Clean
    mq_close(mq);
    mq_unlink(config.queue_name);
    free_env_config(&config);
    free_rescuers_data(&rescuer_data);
    free_emergency_types(&emergency_data);
    free_intent_table(&itable);
    for (int i = 0; i < MAX_TWINS; i++)
    {
        mtx_destroy(&twin_locks[i]);
    }
    close_log();
    printf("Cleanup completato. Uscita.\n");

    exit(EXIT_SUCCESS); // Termina il programma con successo
    return 0;
}

