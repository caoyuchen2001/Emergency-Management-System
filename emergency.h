#ifndef EMERGENCY_H
#define EMERGENCY_H

#include <time.h>
#include "rescuers.h"
#include "env.h"
#include "emergency_types.h"

#define NAME_SIZE 64

typedef enum {
    WAITING,
    ASSIGNED,
    IN_PROGRESS,
    PAUSED,
    COMPLETED,
    CANCELED,
    TIMEOUT
} emergency_status_t;

typedef struct {
    char emergency_name[NAME_SIZE];
    int x;
    int y;
    time_t timestamp;
} emergency_request_t;

typedef struct {
    int id; 
    emergency_request_t req;
} emergency_request_withID_t;

typedef struct {
    emergency_type_t type;
    emergency_status_t status;
    int x;
    int y;
    time_t time;
    int rescuer_count;
    rescuer_digital_twin_t* rescuers_dt;
} emergency_t;

typedef struct {
    int id; // ID univoco dell'istanza di emergenza
    emergency_t emergency;
} emergency_withID_t;

int parse_MQrequest(const char *msg, emergency_request_withID_t *req);
int validate_MQrequest(const emergency_request_withID_t *req,
                       emergency_type_t *types,
                       int num_types,
                       const env_config_t *env);
int create_emergency_instance(emergency_withID_t *instance,
                               const emergency_request_withID_t *req,
                               emergency_type_t *types,
                               int num_types);
const char* emergency_status_str(emergency_status_t status);
void print_emergency_instance(const emergency_withID_t *e);
void free_emergency_instance(emergency_withID_t *e);

#endif
