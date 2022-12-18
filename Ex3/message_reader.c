#include "message_slot.h"
#include <fcntl.h> 
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <ctype.h>
#include <sys/ioctl.h>

/* check to see that the correct number of args has been sent to the program
command line args will be:
argv[1] = message slot file path
argv[2] = target message channel id (non negative integer) */

int main(int argc, char** argv) {
    char buffer[MAX_BUFFER];
    printf("starting message reader\n");
    if (argc != 3) {
        /* validating that the correct number of cmd args is passed */
        if (argc < 3) {
            perror("Missing arguments in message_sender call");
            exit(1);
        } else {
            perror("Too many arguments in message_sender call");
            exit(1);
        }
    }
    /* creating the full path to the file */
    char* file_path = argv[1];
    unsigned long channel_id = atoi(argv[2]);
    /* using the open, ioctl, read and write functions to implement this */
    int file_dest = open(file_path, O_RDWR);
    printf("read 1\n");
    if (file_dest < 0) {
        perror("Problem while opening file destination");
        exit(1);
    } 
    printf("read 2\n");
    if (ioctl(file_dest, MSG_SLOT_CHANNEL, channel_id) < 0) {
        /* a command to set the file's channel to channel_id */
        perror("The command ioctl failed");
        exit(1);
    }
    printf("read 3\n");
    if (read(file_dest, buffer, MAX_BUFFER) < 0) {
        perror("The command read failed");
        exit(1);
    }
    printf("read 4\n");
    if (write(STDOUT_FILENO, buffer, MAX_BUFFER) < 0) {
        perror("The command write failed");
        exit(1);
    }
    printf("read 5\n");
    close(file_dest);
    printf("finishing message reader\n");
    return 0;
}