#include "message_slot.h"

#include <fcntl.h> 
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <ctype.h>
#include <sys/ioctl.h>

int main(int argc, char** argv) {
    int len;
    char buffer[MAX_BUFFER];
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
    if (file_dest < 0) {
        perror("Problem while opening file destination");
        exit(1);
    } 
    if (ioctl(file_dest, MSG_SLOT_CHANNEL, channel_id) < 0) {
        /* set the file's channel to channel_id */
        perror("The command ioctl failed");
        exit(1);
    }
    /* read the input message */
    len = read(file_dest, buffer, MAX_BUFFER);
    if (len < 0) {
        perror("The command read failed");
        exit(1);
    }
    if (write(STDOUT_FILENO, buffer, len) < 0) {
        /* write the message to the command line */
        perror("The command write failed");
        exit(1);
    }
    close(file_dest);
    return 0;
}