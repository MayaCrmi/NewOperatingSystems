#include <stdio.h>
#include <stdlib.h>
#include <dirent.h>
#include <string.h>
#include <threads.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <stdatomic.h>
#include <limits.h>
#include <unistd.h>

/* gcc -O3 -D_POSIX_C_SOURCE=200809 -Wall -std=c11 -pthread pfind.c */

/* creating two different queue structs, one for a thread struct and one for a directory struct 
with the directory queue promising a FIFO traversing over the files in the root directory 
and the threads queue promising to wake the thread that has been sleeping for the longest */

struct dir_instance {
    struct dir_instance* next;
    char* dir_path;
};

struct fifo_dir_queue {
    struct dir_instance* head;
    struct dir_instance* tail;
    int size;
};

struct thread_instance {
    struct thread_instance* next;
    cnd_t cond;
};

struct thread_queue {
    struct thread_instance* head;
    struct thread_instance* tail;
    int size;
};

/* global variables */
char* search_term;
int threads_num, created_threads, waiting_threads, thread_error, found_files;
struct fifo_dir_queue* queue;
struct thread_queue* thread_queue;
cnd_t finished_creating_threads, dir_in_queue;
mtx_t lock;


/* freeing up the pointers from the queues we created during the search */
void freeQueues() {
    struct dir_instance* curr_dir = queue -> head;
    while (curr_dir) {
        struct dir_instance* dummy = curr_dir;
        free(dummy -> dir_path);
        free(dummy);
        curr_dir = curr_dir -> next;
    }
    struct thread_instance* curr_thread = thread_queue -> head;
    while (curr_thread) {
        struct thread_instance* dummy = curr_thread;
        free(dummy);
        cnd_destroy(&curr_thread -> cond);
        curr_thread = curr_thread -> next;
    }

}

struct thread_instance* initialize_thread() {
    struct thread_instance* thread = malloc(sizeof(struct thread_instance));
    if (thread == NULL) {
        fprintf(stderr, "Problem with memory allocation\n");
        thread_error++;
    }
    return thread;
}

/* maintaining the threads queue */
void enqueue_threads(struct thread_instance* thread) {
    struct thread_instance* curr_thread = initialize_thread();
    curr_thread -> next = NULL;
    if (thread_queue -> head == NULL) {
        thread_queue -> head = thread_queue -> tail = curr_thread;
    } else {
        (thread_queue -> tail) -> next = curr_thread;
        thread_queue -> tail = curr_thread;
    }
    (thread_queue -> size)++;
}

struct thread_instance* dequeue_threads() {
    if (thread_queue -> head == NULL) {
        return NULL;
    }
    struct thread_instance* curr_thread = initialize_thread();
    curr_thread = thread_queue -> head;
    thread_queue -> head = (thread_queue -> head) -> next;
    if (thread_queue -> head == NULL) {
        thread_queue -> tail = NULL;
    }
    (thread_queue -> size)--;
    return curr_thread;
}

/* maintaining the directories queue */
void enqueue_dirs(char* path) {
    struct dir_instance* curr_dir;
    curr_dir = malloc(sizeof(struct dir_instance));
    if (curr_dir == NULL) {
        fprintf(stderr, "Problem with memory allocation\n");
        thread_error++;;
    }
    curr_dir -> dir_path = malloc((strlen(path)+1));
    if (curr_dir -> dir_path == NULL) {
        fprintf(stderr, "Problem with memory allocation\n");
        thread_error++;;
    }
    curr_dir -> next = NULL;
    strcpy(curr_dir -> dir_path, path);
    if (queue -> head == NULL) {
        queue -> head = queue -> tail = curr_dir;
    } else {
        (queue -> tail) -> next = curr_dir;
        queue -> tail = curr_dir;
    }
    (queue -> size)++;
}

char* dequeue_dirs() {
    struct dir_instance* curr_dir;
    struct thread_instance* curr_thread;
    char* ret_path = NULL;
    while((queue -> size) == 0) {
        if ((thread_queue -> size) == threads_num-1) {
            /* if all of the threads are waiting but there aren't any directories 
            in the queue, so we need to tell all the threads to exit */
            while ((curr_thread = dequeue_threads()) != NULL) {
                printf("signalingggggg\n");
                cnd_signal(&(curr_thread -> cond));
            }
            mtx_unlock(&lock);
            printf("going to exit now\n");
            thrd_exit(0);
        } else {
            curr_thread = initialize_thread();
            cnd_t condition = curr_thread -> cond;
            cnd_init(&condition);
            enqueue_threads(curr_thread);
            cnd_wait(&condition, &lock);
            cnd_destroy(&condition);
            if ((thread_queue -> size) == threads_num) { 
                /* if all of the threads finished */
                mtx_unlock(&lock);
                thrd_exit(0);
            }
        }   
    }
    /* when the next thread is ready, extract the next directory waiting to be searched */
    curr_dir = queue -> head;
    ret_path = malloc(((strlen(curr_dir -> dir_path))+1));
    if (ret_path == NULL) {
        fprintf(stderr, "Problem with memory allocation\n");
        thread_error++;;
    }
    strcpy(ret_path, ((curr_dir) -> dir_path));
    queue -> head = (queue -> head) -> next;
    if (queue -> head == NULL) {
        queue -> tail = NULL;
    }
    free(curr_dir -> dir_path);
    free(curr_dir);
    (queue -> size)--;
    mtx_unlock(&lock);
    return ret_path;
}

int thread_func(void* thread_struct) {
    DIR* curr_dir;
    char *curr_dir_name, *curr_entry_name, next_dir[PATH_MAX];
    struct dirent* dir_entry;
    //struct thread_instance* curr_thread = (struct thread_instance*) thread_struct;
    mtx_lock(&lock);
    cnd_wait(&finished_creating_threads, &lock);
    //curr_thread = dequeue_threads();
    while (((thread_queue) -> size) < threads_num || (thread_queue -> size != 0 && queue -> size != 0)) {
        printf("going into search now when the queue of threads has %d in it and queue of dics has %d in it\n", thread_queue->size, queue -> size);
         /* the actual file searching logic */
        curr_dir_name = dequeue_dirs();
        if ((curr_dir = opendir(curr_dir_name)) == NULL) {
            fprintf(stderr, "Problem opening directory %s\n", curr_dir_name);
            thread_error++;;
        }
        while ((dir_entry = readdir(curr_dir)) != NULL) {
            curr_entry_name = dir_entry -> d_name;
            if (!strcmp(curr_entry_name, ".") || !strcmp(curr_entry_name, "..")) {
                /* in the case the current entry is the parent directory or the directory 
                itself we want to move on to the next entry in the directory */
                continue;
            }
            if ((dir_entry -> d_type) == 4) {
                /* if the current entry is a directory */
                strcpy(next_dir, curr_dir_name);
                strcat(next_dir, "/");
                strcat(next_dir, curr_entry_name);
                if ((!access(curr_entry_name, R_OK) && !access(curr_entry_name, X_OK))) {
                    printf("Directory %s: Permission denied.\n", next_dir);
                    continue;
                } else {
                    mtx_lock(&lock);
                    enqueue_dirs(next_dir);
                    if ((thread_queue -> head) != NULL) {
                        cnd_signal(&((thread_queue -> head) -> cond));
                    }
                    mtx_unlock(&lock);
                }
            } else if (strstr(curr_entry_name, search_term)) {
                /* if the current entry is a file and contains the search term */
                printf("%s\n", curr_entry_name);
                found_files++;
            }  
        }
        closedir(curr_dir);
        mtx_lock(&lock);
    }
    thrd_exit(0);
}

void create_threads() {
    /* add into a queue here */
    int i;
    thrd_t thread_ids[threads_num];
    for (i = 0 ; i < threads_num ; i++) {
        struct thread_instance* thread = initialize_thread();
        cnd_init(&thread -> cond);
        if (thrd_create(&thread_ids[i], thread_func, (void*)thread) != 0) {
            fprintf(stderr, "Failed creating thread\n");
            exit(1);
        }
    }

    /* without sleep(1) the broadcast does not work */
    sleep(1);
    cnd_broadcast(&finished_creating_threads);
    for (i = 0; i < threads_num; i++) {
        printf("going into join with i being %d\n", i);
        if ((thrd_join(thread_ids[i], NULL)) != 0) {
            printf("heeeeeeeeeeeeeeeeeeeeeeeeeeeeey\n");
            fprintf(stderr, "Failed joining thread\n");
            exit(1);
        }
    }
    printf("did you finish joining?\n");
}

int main(int argc, char* argv[]) {
    struct stat information;

    /* processing the given arguements */
    if (argc != 4) {
        fprintf(stderr, "Invalid number of arguments\n");
        exit(1);
    }
    if (((stat(argv[1], &information) != 0) || S_ISDIR(information.st_mode) == 0 || access(argv[1], R_OK) || access(argv[1], X_OK))) {
        fprintf(stderr, "Root directory is not available\n");
        exit(1);
    }
    DIR *open_dir = opendir(argv[1]);
    if (open_dir == NULL) {
        fprintf(stderr, "Failed to open root directory\n");
        exit(1);
    }
    search_term = argv[2];
    threads_num = atoi(argv[3]);

    /* initializing the necessary structs */
    queue = malloc(sizeof(struct fifo_dir_queue));
    if (queue == NULL) {
        fprintf(stderr, "Could not initialize queue\n");
        exit(1);
    }
    thread_queue = malloc(sizeof(struct thread_queue));
    if (queue == NULL) {
        fprintf(stderr, "Could not initialize queue\n");
        exit(1);
    }
    queue -> head = queue -> tail = NULL;
    thread_queue -> head = thread_queue -> tail = NULL;
    
    /* before creating the threads, initialize the locks conditions and variables */
    mtx_init(&lock, mtx_plain);
    cnd_init(&finished_creating_threads);
    enqueue_dirs(argv[1]);
    create_threads();
    
    /* ending the main function and returning the wanted response */
    printf("Done searching, found %d files\n", found_files);
    if(closedir(open_dir) != 0) {
        fprintf(stderr, "Could not close the directory\n");
        exit(1);
    }
    freeQueues();
    mtx_destroy(&lock);
    cnd_destroy(&finished_creating_threads);
    int exit = thread_error == 0 ? 0 : 1;
    return exit;
}
