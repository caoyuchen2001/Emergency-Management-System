#ifndef EMERGENCY_TYPES_H
#define EMERGENCY_TYPES_H

#include "rescuers.h" 

typedef struct {
    rescuer_type_t *type;   
    int required_count;    
    int time_to_manage;   
} rescuer_request_t;

typedef struct {
    short priority;                    
    char *emergency_desc;             
    rescuer_request_t *rescuers;      
    int rescuers_req_number;        
} emergency_type_t;

typedef struct {
    emergency_type_t *types; 
    int num_types;            
} emergency_data_t;

int parse_emergency_types(const char *filename, const rescuer_data_t *rescuer_data, emergency_data_t *emergency_data);
void free_emergency_types(emergency_data_t *data);
void print_emergency_types(const emergency_data_t *data);

#endif 
