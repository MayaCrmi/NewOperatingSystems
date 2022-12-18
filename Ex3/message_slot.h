#define MAJOR_NUMBER 235
#define MSG_SLOT_CHANNEL _IOW(MAJOR_NUMBER, 0, unsigned long)
#define MAX_BUFFER 128

/* creating structs  */
struct channel_data {
    long channel_id;
    int msglen;
    char msg[128];
    struct channel_data* next;
};

struct device_data {
    int minor_number;
    long open_channel_id;
    int opened;
    struct channel_data* head;
    struct channel_data* open_channel;
};



void free_allocated_memory(void);
struct channel_data* find_channel(struct device_data* device, int channel_num);