// worker_thread.h
#ifndef WORKER_THREAD_H
#define WORKER_THREAD_H

#include <threads.h>
#include "rescuers.h"
#include "emergency.h"
#include "intent.h"

#define TIMEOUT_PRIORITY_1 30
#define TIMEOUT_PRIORITY_2 10
// timeout massimo per priorità 0: 1 giorno
// usata per evitare overflow (siccome INT_MAX + qualsiasi int ha rischio di overflow)
#define TIMEOUT_MAX 86400
#define INTENT_REFRESH_INTERVAL 200

typedef struct {
  intent_table_t *itable;
  rescuer_data_t *rdata;
  mtx_t *twin_locks;
  emergency_withID_t *emergency;
} worker_args_t;

typedef struct {
  int arrived;
  int returned;
  mtx_t mutex;
  cnd_t all_arrived;
  cnd_t all_returned;
} emergency_sync_t;

typedef struct {
  rescuer_digital_twin_t *twin;
  emergency_withID_t *e;
  emergency_sync_t *sync;
} twin_arg_t;

typedef struct {
  emergency_withID_t *e;
  emergency_sync_t *sync;
} em_arg_t;

typedef struct {
    rescuer_digital_twin_t *twin;
    int travel_time;
} twin_candidate_t;

typedef struct {
        char type[NAME_SIZE];
        int ids[MAX_TWINS];
        int count;
} group_t;

int check_deadline(emergency_withID_t *e);
int check_reachability(emergency_withID_t *e, rescuer_data_t *rdata);
int assign_rescuers_to_emergency(emergency_withID_t *e,
                                 rescuer_data_t *rdata,
                                 rescuer_digital_twin_t **assigned_twins,
                                 mtx_t *twin_locks); 
void handle_emergency(emergency_withID_t *e,
                      rescuer_digital_twin_t **assigned_twins);
int run_twin_task(void *arg);
int run_emergency_task(void *arg);
int worker_thread(void *arg);

#endif
