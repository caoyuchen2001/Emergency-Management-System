#ifndef RESCUER_H
#define RESCUER_H

#define MAX_TYPES 512
#define MAX_TWINS 2048

typedef enum {
    IDLE,            
    EN_ROUTE_TO_SCENE,  
    ON_SCENE,           
    RETURNING_TO_BASE  
} rescuer_status_t;

typedef struct {
    char *rescuer_type_name;  
    int speed;            
    int x;             
    int y;               
} rescuer_type_t;

typedef struct {
    int id;                  
    int x;                  
    int y;
    rescuer_type_t *rescuer;  
    rescuer_status_t status;    
} rescuer_digital_twin_t;

typedef struct {
    rescuer_type_t **types; 
    int num_types;

    rescuer_digital_twin_t *twins; 
    int num_twins;
} rescuer_data_t;

int parse_rescuers(const char *filename, rescuer_data_t *data);
void free_rescuers_data(rescuer_data_t *data);
const char* twin_status_to_string(rescuer_status_t status);
void print_rescuer_data(rescuer_data_t *data);

#endif 
