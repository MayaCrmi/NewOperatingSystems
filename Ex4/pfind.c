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

struct dir_instance {
    struct dir_instance* next;
    char* dir_path;
};

struct fifo_dir_queue {
    struct dir_instance* head;
    struct dir_instance* tail;
};

struct thread_instance {
    struct thread_instance* next;
    int thread_id;
    cnd_t cond_queue;
};

struct thread_queue {
    struct thread_instance* head;
    struct thread_instance* tail;
    int size;
};

/* global variables */
char* search_term;
int threads_num, created_threads, waiting_threads, found_files;
struct fifo_dir_queue* queue;
struct thread_queue* thread_queue;
cnd_t finished_creating_threads, dir_in_queue;
mtx_t lock;


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
        cnd_destroy(&curr_thread -> cond_queue);
        curr_thread = curr_thread -> next;
    }

}

void enqueue_dirs(char* path) {
    struct dir_instance* curr_dir;
    curr_dir = malloc(sizeof(struct dir_instance));
    if (curr_dir == NULL) {
        fprintf(stderr, "Problem with memory allocation");
        exit(1);
    }
    curr_dir -> dir_path = malloc((strlen(path)+1));
    if (curr_dir -> dir_path == NULL) {
        fprintf(stderr, "Problem with memory allocation");
        exit(1);
    }
    curr_dir -> next = NULL;
    strcpy(curr_dir -> dir_path, path);
    if (queue -> head == NULL) {
        queue -> head = queue -> tail = curr_dir;
    } else {
        (queue -> tail) -> next = curr_dir;
        queue -> tail = curr_dir;
    }
}

char* dequeue_dirs() {
    struct dir_instance* curr_dir;
    char* ret_path = NULL;
    while((queue -> head) == NULL) {
        if ((thread_queue -> size) == threads_num) {
            /* all of the threads are waiting but there aren't any directories in the queue */
            printf("I suspect there's a problem here\n");
            cnd_broadcast(&dir_in_queue);
            mtx_unlock(&lock);
            thrd_exit(0);
        }
        /* the thread's condition will signal when we enter a directory into the queue */
        printf("Waiting for the condition with thread number being %d\n", thread_queue -> head -> thread_id);
        cnd_wait(&(thread_queue -> head) -> cond_queue, &lock);
        printf("Finished waiting for the condition with thread number being %d\n", thread_queue -> head -> thread_id);
        if ((thread_queue -> size) == threads_num) { 
            /* if every thread finished */
            mtx_unlock(&lock);
            thrd_exit(0);
        }
    }
    printf("Dequeueing the next dir %s with thread num %d\n", queue->head->dir_path, thread_queue->head->thread_id);
    curr_dir = queue -> head;
    ret_path = malloc(((strlen(curr_dir -> dir_path))+1));
    if (ret_path == NULL) {
        fprintf(stderr, "Problem with memory allocation");
        exit(1);
    }
    strcpy(ret_path, ((curr_dir) -> dir_path));
    queue -> head = (queue -> head) -> next;
    free(curr_dir -> dir_path);
    free(curr_dir);
    return ret_path;
}

void enqueue_threads(struct thread_instance* thread) {
    struct thread_instance* curr_thread = malloc(sizeof(struct thread_instance));
    if (curr_thread == NULL) {
        fprintf(stderr, "Problem with memory allocation");
        exit(1);
    }
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
    struct thread_instance* curr_thread;
    if (thread_queue -> head == NULL) {
        fprintf(stderr, "No threads in queue");
        exit(1);
    }
    curr_thread = malloc(sizeof(struct thread_instance));
    if (curr_thread == NULL) {
        fprintf(stderr, "Problem with memory allocation");
        exit(1);
    }
    curr_thread = thread_queue -> head;
    thread_queue -> head = (thread_queue -> head) -> next;
    (thread_queue -> size)--;
    return curr_thread;
}

int thread_func(void* thread_struct) {
    DIR* curr_dir;
    char *curr_dir_name, *curr_entry_name, next_dir[PATH_MAX];
    struct dirent* dir_entry;
    struct thread_instance* curr_thread = (struct thread_instance*) thread_struct;
    mtx_lock(&lock);
    cnd_wait(&finished_creating_threads, &lock);
    curr_thread = dequeue_threads();
    while (((thread_queue) -> size) < threads_num) {
        printf("going into the while loop with curreny thread id being %d\n", curr_thread->thread_id);
         /* the actual file searching logic */
        curr_dir_name = dequeue_dirs();
        curr_dir = opendir(curr_dir_name);
        if (curr_dir == NULL) {
            fprintf(stderr, "Problem opening directory %s", curr_dir_name);
            exit(1);
        }
        printf("going into the while loop with the dir_name being %s\n", curr_dir_name);
        while ((dir_entry = readdir(curr_dir)) != NULL) {
            curr_entry_name = dir_entry -> d_name;
            printf("scanning file %s\n", curr_entry_name);
            if (!strcmp(curr_entry_name, ".") || !strcmp(curr_entry_name, "..")) {
                /* in case the current entry is the parent directory or the directory itself */
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
                    printf("Found a new directory to work on\n");
                    //mtx_lock(&lock);
                    enqueue_dirs(next_dir);
                    cnd_signal(&((thread_queue -> head) -> cond_queue));
                    curr_thread = dequeue_threads();
                    printf("Sent a signal\n");
                    //mtx_unlock(&lock);
                }
            } else if (strstr(curr_entry_name, search_term)) {
                /* if the current entry is a file and contains the search term */
                printf("FOUND: %s\n", curr_entry_name);
                found_files++;
            }  
        }
        closedir(curr_dir);
        enqueue_threads(curr_thread);
        mtx_lock(&lock);
    }
    return 0;
}

void create_threads() {
    /* add into a queue here */
    thrd_t thread_ids[threads_num];
    for (int i=0 ; i < threads_num ; i++) {
        struct thread_instance* thread;
        thread = malloc(sizeof(struct thread_instance));
        if (thread == NULL) {
            fprintf(stderr, "Problem with memory allocation");
            exit(1);
        }
        thread -> thread_id = i;
        cnd_init(&thread -> cond_queue);
        if (thrd_create(&thread_ids[i], thread_func, (void*)thread) != 0) {
            fprintf(stderr, "Failed creating thread\n");
            exit(1);
        }
        enqueue_threads(thread);
    }

    /* without sleep(1) the broadcast does not work */
    sleep(1);
    cnd_broadcast(&finished_creating_threads);
    for (int i = 0; i < threads_num; i++) {
        if (thrd_join(thread_ids[i], NULL) != 0) {
            fprintf(stderr, "Failed joining thread\n");
            exit(1);
        }
    }
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
    enqueue_dirs(argv[1]);
    
    /* before creating the threads, initialize the locks conditions and variables */
    mtx_init(&lock, mtx_plain);
    cnd_init(&finished_creating_threads);
    create_threads();

    /* ending the main function and returning the wanted response */
    printf("Done searching, found %d files\n", found_files);
    if(closedir(open_dir) != 0) {
        fprintf(stderr, "Could not close the directory");
        exit(1);
    }
    freeQueues();
    mtx_destroy(&lock);
    cnd_destroy(&finished_creating_threads);
    return 0;
}