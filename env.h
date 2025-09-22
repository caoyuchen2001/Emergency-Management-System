#ifndef ENV_H
#define ENV_H

typedef struct {
    char* queue_name;
    int height;
    int width;
} env_config_t;

int parse_env(const char *filename, env_config_t *config);
void free_env_config(env_config_t *config);
void print_env(const env_config_t *config);

#endif