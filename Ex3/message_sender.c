#include "message_slot.h"

#include <fcntl.h> 
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <ctype.h>
#include <sys/ioctl.h>

int main(int argc, char** argv) {
    if (argc !=4) {
        if (argc < 4) {
            perror("Missing arguments in message_sender call");
            exit(1);
        } else if (argc > 4) {
            perror("Too many arguments in message_sender call");
            exit(1);
    }
    }
    char* file_path = argv[1];
    unsigned long channel_id = atoi(argv[2]);
    char* msg = argv[3];
    /* using the open, ioctl and write functions to implement this */
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
    if (write(file_dest, msg, strlen(msg)) < 0) {
        /* write the input message */
        perror("The command write failed");
        exit(1);
    }
    close(file_dest);
    return 0;
}