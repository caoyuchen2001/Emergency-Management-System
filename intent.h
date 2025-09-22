#ifndef INTENT_TABLE_H
#define INTENT_TABLE_H

#include <time.h>
#include <threads.h>
#include "rescuers.h"
#include "emergency.h"

#define MAX_INTENT_ENTRIES 128
#define WINDOW_PERIOD_SEC 5

typedef struct {
    int id;
    int priority;
    time_t timestamp;
    int twin_ids[MAX_TWINS];
    int twin_count;
} intent_t;

typedef struct {
    intent_t *items[MAX_INTENT_ENTRIES];
    int size;
    mtx_t mutex;
} intent_table_t;

void init_intent_table(intent_table_t *table);
int register_intent(intent_table_t *table, intent_t *intent);
int update_intent(intent_table_t *table, intent_t *new_intent);
int refresh_intent(intent_table_t *table, emergency_withID_t *e, rescuer_data_t *rdata, int first_time);
void unregister_intent(intent_table_t *table, int emergency_id);
int can_proceed(intent_table_t *table, int emergency_id);
intent_t *create_intent_from_emergency(const emergency_withID_t *e, const rescuer_data_t *rdata);
void free_intent_table(intent_table_t *table);

#endif
