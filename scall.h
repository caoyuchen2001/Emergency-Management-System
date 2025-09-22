
#define SCALL_ERROR -1

#define SCALL(r, c, e) do { if((r = c) == SCALL_ERROR) { perror(e); exit(EXIT_FAILURE); } } while(0)
#define SNCALL(r, c, e) do { if((r = c) == NULL) { perror(e); exit(EXIT_FAILURE); } } while(0)

#define SCALLREAD(r,loop_cond_op,read_loop_op,e) do { while((r = loop_cond_op)>0) { read_loop_op; } if(r == SCALL_ERROR) { perror(e); exit(EXIT_FAILURE); } } while(0)
#define PARENT_OR_CHILD(pid,f_parent,f_child) do { if(pid == 0) { f_child(); } else { f_parent(pid); } } while(0)

#define MCALL_INIT(mutex_ptr, type, errmsg) \
    do { \
        if (mtx_init((mutex_ptr), (type)) != thrd_success) { \
            perror(errmsg); \
            exit(EXIT_FAILURE); \
        } \
    } while (0)

#define MCALL_LOCK(mutex_ptr, errmsg) \
    do { \
        if (mtx_lock((mutex_ptr)) != thrd_success) { \
            perror(errmsg); \
            exit(EXIT_FAILURE); \
        } \
    } while (0)

#define MCALL_UNLOCK(mutex_ptr, errmsg) \
    do { \
        if (mtx_unlock((mutex_ptr)) != thrd_success) { \
            perror(errmsg); \
            exit(EXIT_FAILURE); \
        } \
    } while (0)
