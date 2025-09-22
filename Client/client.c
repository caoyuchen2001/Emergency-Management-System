#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <mqueue.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <time.h>
#include <errno.h>

#define MQ_NAME "/emergenze616906"
#define MAX_MSG_SIZE 512
#define NAME_SIZE 64

// Funzione per inviare un messaggio di emergenza tramite una coda di messaggi POSIX
void send_emergency(const char *msg) {
    // Apre la coda di messaggi in modalità scrittura
    mqd_t mq = mq_open(MQ_NAME, O_WRONLY);
    if (mq == -1) {
        perror("mq_open");
        exit(EXIT_FAILURE);
    }
    // Invia il messaggio nella coda
    if (mq_send(mq, msg, strlen(msg) + 1, 0) == -1) {
        perror("mq_send");
        mq_close(mq);
        exit(EXIT_FAILURE);
    }
    // Chiude la coda di messaggi dopo l'invio
    if (mq_close(mq) == -1) {
        perror("errore in mq_close");
        exit(EXIT_FAILURE);
    }
}

int main(int argc, char *argv[]) {
    // Controlla se i parametri corrispondono alla modalità singola
    if (argc == 5) {
        // Modalità singola: ./client <tipo> <x> <y> <ritardo>
        const char *tipo = argv[1];
        int x = atoi(argv[2]);
        int y = atoi(argv[3]);
        int ritardo = atoi(argv[4]);
        // Attende per il tempo indicato prima di inviare l'emergenza
        sleep(ritardo);
        // Prepara il messaggio con tipo, coordinate e timestamp attuale
        char msg[MAX_MSG_SIZE];
        snprintf(msg, sizeof(msg), "%s %d %d %ld", tipo, x, y, time(NULL));
        // Invia il messaggio
        send_emergency(msg);
    }
    // Controlla se i parametri corrispondono alla modalità -f 
    else if (argc == 3 && strcmp(argv[1], "-f") == 0) {
        // Apre il file
        FILE *f = fopen(argv[2], "r");
        if (!f) {
            perror("fopen");
            exit(EXIT_FAILURE);
        }
        char line[MAX_MSG_SIZE];
        // Legge ogni riga del file
        while (fgets(line, sizeof(line), f)) {
            // Rimuove il carattere di newline
            line[strcspn(line, "\n")] = '\0';

            char tipo[NAME_SIZE];
            int x, y, ritardo;
            if (sscanf(line, "%63s %d %d %d", tipo, &x, &y, &ritardo) != 4){
                // Salta alla prossima riga in caso di errore di formato
                continue;
            }
            // Attende per il ritardo specificato
            sleep(ritardo);
            char msg[MAX_MSG_SIZE];
            // Prepara il messaggio con tipo, coordinate e timestamp
            snprintf(msg, sizeof(msg), "%s %d %d %ld", tipo, x, y, time(NULL));
            // Invia il messaggio
            send_emergency(msg);
        }

        // Chiude il file
        if (fclose(f) == -1){
            perror("fclose");
            exit(EXIT_FAILURE);
        }
    }
    else{
        // Stampa il messaggio di utilizzo in caso di parametri non validi
        fprintf(stderr, "Uso:\n");
        fprintf(stderr, "  %s <tipo> <x> <y> <ritardo>\n", argv[0]);
        fprintf(stderr, "  %s -f <file>\n", argv[0]);
        return -1;
    }

    return 0;
}
