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
    /* add here a thread */
};

struct thread_queue {
    struct thread_instance* head;
    struct thread_instance* tail;
};

/* global variables */
char* search_term;
int max_searching_threads, created_threads, finished_threads, found_files;
struct fifo_dir_queue* queue;
struct dir_instance* root_dir;
cnd_t finished_creating_threads;
mtx_t lock;


void freeQueues() {
    struct dir_instance* curr = queue -> head;
    while (curr) {
        struct dir_instance* dummy = curr;
        free(dummy -> dir_path);
        free(dummy);
        curr = curr -> next;
    }
}

char* manage_queue(char* path, int method) {
    struct dir_instance* curr_dir;
    char* ret_path = NULL;
    if (method == 1) {
        /* inserting a new directory to the queue */
        curr_dir = malloc(sizeof(struct dir_instance));
        if (curr_dir == NULL) {
            fprintf(stderr, "Problem with memory allocation:");
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
        return "";
    }
    if (method == -1) {
        /* removing a directory from the queue */
        if ((queue -> head) == NULL) {
            fprintf(stderr, "Queue is empty");
            exit(1);
        }
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
    return "";
}

int thread_func(void* thread_param) {
    DIR* curr_dir;
    char *curr_dir_name, *curr_entry_name, next_dir[PATH_MAX];
    struct dirent* dir_entry;
    created_threads++;
    mtx_lock(&lock);
    cnd_wait(&finished_creating_threads, &lock);
    while (finished_threads < created_threads) {
        /* searching */
        curr_dir_name = manage_queue("", -1);
        curr_dir = opendir(curr_dir_name);
        if (curr_dir == NULL) {
            fprintf(stderr, "Problem opening directory:");
            exit(1);
        }
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
                    manage_queue(next_dir, 1);
                }
            } else if (strstr(curr_entry_name, search_term)) {
                /* if the current entry is a file and contains the search term */
                printf("found one!!!!!!! %s\n", curr_entry_name);
                found_files++;
            }
        }
        closedir(curr_dir);
    }
    return 0;
}

void create_threads() {
    /* add into a queue here */
    thrd_t thread_ids[max_searching_threads];
    for (int i=0 ; i < max_searching_threads ; i++) {
        if (thrd_create(&thread_ids[i], thread_func, NULL) != 0) {
            fprintf(stderr, "Failed creating thread:\n");
            exit(1);
        }
    }
    sleep(1);
    cnd_broadcast(&finished_creating_threads);
    for (int i = 0; i < max_searching_threads; i++) {
        if (thrd_join(thread_ids[i], NULL) != 0) {
            fprintf(stderr, "Failed joining thread:\n");
            exit(1);
        }
    }
}

int main(int argc, char* argv[]) {
    struct stat information;
    /* start by the necessary initializations of the arguments received */
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
    max_searching_threads = atoi(argv[3]);
    queue = malloc(sizeof(struct fifo_dir_queue));
    if (queue == NULL) {
        fprintf(stderr, "Could not initialize queue\n");
        exit(1);
    }
    queue -> head = queue -> tail = NULL;
    manage_queue(argv[1], 1);
    /* before creating the threads, initialize the locks conditions and variables */
    mtx_init(&lock, mtx_plain);
    cnd_init(&finished_creating_threads);
    create_threads();
    /* ending the main function */
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