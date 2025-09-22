CC = gcc
CFLAGS = -Wall -pedantic -std=c11
NAME = main
LIBS = -lpthread

SRCS = main.c logger.c parse_env.c parse_rescuers.c parse_emergency_types.c emergency.c intent.c worker_thread.c
OBJS = $(SRCS:.c=.o)

.PHONY: default clean run

default: $(NAME)

%.o: %.c
	$(CC) -c $(CFLAGS) $< -o $@

$(NAME): $(OBJS)
	$(CC) $(CFLAGS) -o $@ $^ $(LIBS)

run: $(NAME)
	./$(NAME)

clean:
	rm -f $(NAME) $(OBJS)
