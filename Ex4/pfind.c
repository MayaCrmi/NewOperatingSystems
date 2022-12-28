#include <stdio.h>
#include <stdlib.h>
#include <dirent.h>
#include <string.h>
#include <threads.h>

/* gcc -O3 -D_POSIX_C_SOURCE=200809 -Wall -std=c11 -pthread pfind.c */

struct dir_instance {
    struct dir_instance* next;
    char* dir_path;
};

struct fifo_dir_queue {
    struct dir_instance* head;
    struct dir_instance* tail;
};

/* global variables */
char* search_term;
int max_searching_threads, created_threads, found_files;
struct fifo_dir_queue* queue;
struct dir_instance* root_dir;

void freeQueues() {
    struct dir_instance* curr = queue -> head;
    while (curr) {
        struct dir_instance* dummy = curr;
        free(dummy);
        curr = curr -> next;
    }
}

void manage_queue(char* path, int act) {
    if (act == 1) {
        /* inserting a new directory to the queue*/
        struct dir_instance* new_dir;
        new_dir = malloc(sizeof(struct dir_instance));
        if (new_dir == NULL) {
            fprintf(stderr, "Problem with memory allocation:");
            exit(1);
        }
        new_dir -> dir_path = path;
        if (queue -> head == NULL) {
            queue -> head = new_dir;
            queue -> tail = new_dir;
        } else {
            (queue -> tail) -> next = new_dir;
            queue -> tail = new_dir;
        }
    } 
    if (act == -1) {
        /* removing a directory from the queue */
        if (queue -> head == NULL) {
            fprintf(stderr, "Invalid request");
            exit(1);
        }
    }
}

int thread_func(void* thread_param) {
    created_threads++;
    if (created_threads == max_searching_threads) {
        /* after all searching threads are created, the main thread signals them to start searching */
    }
    /* need to implement this */
    return 0;
}

void create_threads() {
    thrd_t thread_ids[max_searching_threads];
    for (int i=0 ; i < max_searching_threads ; i++) {
        if (thrd_create(&thread_ids[i], thread_func, NULL) != 0) {
            fprintf(stderr, "Failed creating thread:\n");
            exit(1);
        }
    }
    for (int i = 0; i < max_searching_threads; i++) {
        if (thrd_join(thread_ids[i], NULL) != 0) {
            fprintf(stderr, "Failed joining thread:\n");
            exit(1);
        }
    }
}

int main(int argc, char* argv[]) {
    if (argc != 4) {
        fprintf(stderr, "Invalid number of arguments:\n");
        exit(1);
    }
    DIR *open_dir = opendir(argv[1]);
    if (open_dir == NULL) {
        fprintf(stderr, "Failed to open root directory:\n");
        exit(1);
    }
    search_term = argv[2];
    max_searching_threads = atoi(argv[3]);
    queue = malloc(sizeof(struct fifo_dir_queue));
    if (queue == NULL) {
        fprintf(stderr, "Could not initialize queue:\n");
        exit(1);
    }
    root_dir = malloc(sizeof(struct dir_instance));
    if (root_dir == NULL) {
        fprintf(stderr, "Could not initialize root directory:\n");
        exit(1);
    }
    strcpy(root_dir -> dir_path, argv[1]);
    queue -> head = root_dir;
    create_threads();
    printf("Done searching, found %d files\n", found_files);
    if(closedir(open_dir) != 0) {
        fprintf(stderr, "Could not close the directory:");
        exit(1);
    }
    freeQueues();
    return 0;
}