#ifndef LOGGER_H
#define LOGGER_H

#include <time.h>

void init_log(void);
void log_event(const char *id, const char *event_type, const char *message);
void close_log(void);
void log_event_id(int id, const char *event_type, const char *message);

#endif
