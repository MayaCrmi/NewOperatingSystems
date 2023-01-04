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

char* search_term;
int threads_num, created_threads, waiting_threads, thread_error, found_files;
struct fifo_dir_queue* queue;
struct thread_queue* thread_queue;
cnd_t finished_creating_threads;
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
        struct thread_instance* dummy_thread = curr_thread;
        cnd_destroy(&dummy_thread -> cond);
        free(dummy_thread);
        curr_thread = curr_thread -> next;
    }

}

struct thread_instance* initialize_thread() {
    struct thread_instance* thread = malloc(sizeof(struct thread_instance));
    if (thread == NULL) {
        fprintf(stderr, "Problem with memory allocation\n");
        thread_error++;
    }
    cnd_t* cond = malloc(sizeof(cnd_t));
    if (cond == NULL) {
        fprintf(stderr, "Problem with memory allocation\n");
        thread_error++;
    }
    thread -> cond = *cond;
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

cnd_t dequeue_threads() {
    cnd_t cond;
    if (thread_queue -> head == NULL) {
        fprintf(stderr, "No threads to dequeue\n");
        thread_error++;
    } else {
        struct thread_instance* curr_thread = initialize_thread();
        curr_thread = thread_queue -> head;
        cond = curr_thread -> cond;
        thread_queue -> head = (thread_queue -> head) -> next;
        free(curr_thread);
        if (thread_queue -> head == NULL) {
            thread_queue -> tail = NULL;
        }
    }
    (thread_queue -> size)--;
    return cond;
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
    char* ret_path = NULL;
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

char* before_dequeue_dirs() {
    struct thread_instance* curr_thread;
    while((queue -> size) == 0) {
        waiting_threads++;
        if (waiting_threads == threads_num) {
            /* if all of the threads are waiting but there aren't any directories 
            in the queue, so we need to tell all the threads to exit */
            while (thread_queue -> size > 0) {
                cnd_t cond = dequeue_threads();
                cnd_signal(&cond);
            }
            mtx_unlock(&lock);
            thrd_exit(0);
        }
        curr_thread = initialize_thread();
        cnd_init(&curr_thread -> cond);
        enqueue_threads(curr_thread);
        cnd_wait(&curr_thread -> cond, &lock);
        cnd_destroy(&curr_thread -> cond);
        if (waiting_threads == threads_num) { 
            mtx_unlock(&lock);
            thrd_exit(0);
        }
        waiting_threads--;
    }
    /* when the next thread is ready, extract the next directory waiting to be searched */
    return dequeue_dirs();
    
}

int thread_func(void* thread) {
    DIR* curr_dir;
    char *curr_dir_name, *curr_entry_name, next_dir[PATH_MAX];
    struct dirent* dir_entry;
    mtx_lock(&lock);
    cnd_wait(&finished_creating_threads, &lock);
    while (waiting_threads < threads_num) {
         /* the actual file searching logic */
        curr_dir_name = before_dequeue_dirs();
        curr_dir = opendir(curr_dir_name);
        if (curr_dir == NULL) {
            fprintf(stderr, "Problem opening directory %s\n", curr_dir_name);
            thread_error++;
            continue;
        }
        while ((dir_entry = readdir(curr_dir)) != NULL) {
            curr_entry_name = dir_entry -> d_name;
            strcpy(next_dir, curr_dir_name);
            strcat(next_dir, "/");
            strcat(next_dir, curr_entry_name);
            if (!(!strcmp(curr_entry_name, ".") || !strcmp(curr_entry_name, "..")) && (dir_entry -> d_type == 4)) {
                /* if the current entry is a directory but not the directory itself or its parent */
                if ((!access(curr_entry_name, R_OK) && !access(curr_entry_name, X_OK))) {
                    printf("Directory %s: Permission denied.\n", next_dir);
                } else {
                    mtx_lock(&lock);
                    enqueue_dirs(next_dir);
                    if ((thread_queue -> head) != NULL) {
                        cnd_signal(&(thread_queue -> head) -> cond);
                    }
                    mtx_unlock(&lock);
                }
            } else if (strstr(curr_entry_name, search_term)) {
                /* if the current entry is a file and contains the search term */
                printf("%s\n", next_dir);
                found_files++;
            }  
        }
        mtx_lock(&lock);
        closedir(curr_dir);
    }
    thrd_exit(0);
}

void handle_threads() {
    thrd_t thread_ids[threads_num];
    for (int i = 0 ; i < threads_num ; i++) {
        if (thrd_create(&thread_ids[i], thread_func, NULL) != 0) {
            fprintf(stderr, "Failed creating thread\n");
            exit(1);
        }
    }
    /* without sleep(1) the broadcast does not work */
    sleep(1);
    cnd_broadcast(&finished_creating_threads);
    for (int i = 0; i < threads_num; i++) {
        printf("going into join with i being %d\n", i);
        if ((thrd_join(thread_ids[i], NULL)) != 0) {
            printf("Done searching, found %d files\n", found_files);
            fprintf(stderr, "Failed joining thread\n");
            exit(1);
        }
        printf("finishing join with i being %d\n", i);
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
    handle_threads();
    
    /* ending the main function and returning the wanted response */
    printf("Done searching, found %d files\n", found_files);
    if(closedir(open_dir) != 0) {
        fprintf(stderr, "Could not close the directory\n");
        exit(1);
    }
    mtx_destroy(&lock);
    cnd_destroy(&finished_creating_threads);
    freeQueues();
    int detected = thread_error == 0 ? 0 : 1;
    return detected;
}
