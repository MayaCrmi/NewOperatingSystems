
#define MAJOR_NUMBER 235
#define MSG_SLOT_CHANNEL _IOW(MAJOR_NUMBER, 0, unsigned long)
#define MAX_BUFFER 128

#undef __KERNEL__
#define __KERNEL__
#undef MODULE
#define MODULE

/* from Eran's recitations' code */
#include <linux/kernel.h>   /* We're doing kernel work */
#include <linux/module.h>   /* Specifically, a module */
#include <linux/fs.h>       /* for register_chrdev */
#include <linux/errno.h>
#include <linux/uaccess.h>  /* for get_user and put_user */
#include <linux/string.h>   /* for memset. NOTE - not string.h!*/
#include <linux/slab.h>

MODULE_LICENSE("GPL");

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

static struct device_data devices_list[256];

/* before removing a module, free the linked lists containing the devices' channels data */
void free_allocated_memory(void) {
    int i;
    for (i=0 ; i < 256 ; i++) {
        struct channel_data* curr = devices_list[i].head;
        while (curr) {
            struct channel_data* dummy = curr;
            curr = curr -> next;
            kfree(dummy);
        }
    }
}

/* iterate over the linked list containing the device's channels and either return the one corresponding with the desired channel number or NULL */
struct channel_data* find_channel(struct device_data* device, unsigned long channel_num) {
    struct channel_data* curr = device -> head;
    while (curr != NULL) {
        if ((curr -> channel_id) == channel_num) {
            return curr;
        }
        curr = curr -> next;
    }
    return curr;
}

static int device_open(struct inode* inode, struct file* file) {
    int minor = iminor(inode);
    struct device_data* new_device;
    if ((devices_list[minor].opened) == 0) {
        /* if this is the first time we opened a device with this minor number */
        new_device = kmalloc(sizeof(struct device_data), GFP_KERNEL);
        if (!new_device) {
            return -ENOMEM;
        }
        new_device -> minor_number = minor;
        new_device -> opened = 1;
        devices_list[minor] = *new_device;
    } 
    /* set the current file's data to contain its corresponding device_data instance */
    file -> private_data = (void*) (&devices_list[minor]);
    return 0;
}

static long device_ioctl(struct file* file, unsigned int cmd, unsigned long channel_number) {    
    struct device_data* device;
    struct channel_data* curr_channel;
    if ((channel_number == 0) || (cmd != MSG_SLOT_CHANNEL)) {
        return -EINVAL;
    }
    /* extract the current file's corresponding device_data */
    device = (struct device_data*)file -> private_data;
    curr_channel = find_channel(device, channel_number);
    if (!curr_channel) {
        /* if we haven't used a channel with this number yet */
        curr_channel = kmalloc(sizeof(struct channel_data), GFP_KERNEL);
        if (!curr_channel) {
            return -ENOMEM;
        }
        curr_channel -> channel_id = channel_number;
        curr_channel -> next = device -> head;
        device -> head = curr_channel;
        
    }
    (device -> open_channel_id) = channel_number;
    (device -> open_channel) = curr_channel;
    devices_list[device -> minor_number] = *device;
    return 0;
}

static ssize_t device_read(struct file* file, char* buffer, size_t length, loff_t* offset) {
    int i=0;
    unsigned long curr_channel_id;
    int minor, channel_msglen;
    struct device_data* device;
    struct channel_data* curr_channel;
    device = (struct device_data*)file -> private_data;
    curr_channel_id = device -> open_channel_id;
    if (buffer == NULL || curr_channel_id == 0) {
        return -EINVAL;
    }
    minor = iminor(file -> f_inode);
    curr_channel = find_channel(device, curr_channel_id);
    if (!curr_channel || curr_channel -> channel_id == 0) {
        return -ENOMEM;
    }
    channel_msglen = curr_channel -> msglen;
    if (channel_msglen <= 0) {
        return -EWOULDBLOCK;
    }
    if (channel_msglen > length) {
        return -ENOSPC;
    }
    while (i < length && i < channel_msglen) {
        put_user(curr_channel -> msg[i], &buffer[i]);
        i++;
    }
    if (i != channel_msglen) {
        printk("The message hasn't been read successfully\n");
        return -1;
    }
    return i;
}

static ssize_t device_write(struct file* file, const char* buffer, size_t msglen, loff_t* offset) { 
    int i=0;
    int minor;
    unsigned long curr_channel_id;
    struct device_data* device;
    struct channel_data* curr_channel;
    minor = iminor(file -> f_inode);
    /* get the device data */
    device = (struct device_data*)file -> private_data;
    curr_channel_id = device -> open_channel_id;
    if (buffer == NULL || curr_channel_id == 0) {
        return -EINVAL;
    }
    if (msglen == 0 || msglen > MAX_BUFFER) {
        return -EMSGSIZE;
    }
    /* get the channel data */
    curr_channel = find_channel(device, curr_channel_id);
    if (!curr_channel || curr_channel -> channel_id == 0) {
        return -EINVAL;
    }
    while (i < msglen) {
        get_user(curr_channel -> msg[i], &buffer[i]);
        i++;
    }
    if (i != msglen) {
        printk("The message hasn't been written successfully\n");
        return -1;
    }
    curr_channel -> msglen = i;
    return i;
}

static int device_release (struct inode *inode, struct file *file) {
    struct device_data* extracted_device = (struct device_data*) file -> private_data;
    extracted_device -> open_channel_id = 0;
    return 0;
 }

struct file_operations fops = {
    .owner = THIS_MODULE,
    .open = device_open,
    .unlocked_ioctl = device_ioctl,
    .read = device_read, 
    .write = device_write, 
    .release = device_release,
};


static int __init message_slot_init(void) {
    int reg;
    printk("Inserting the message slot module\n");
    reg = register_chrdev(MAJOR_NUMBER, "message_slot", &fops);
    if (reg < 0) {
        printk(KERN_ERR "The insertion of the module has failed\n");
        return -1;
    }
    printk("The module has been inserted\n");
    return 0;

}

static void __exit message_slot_exit(void) {
    printk("Before unloading the module, free all memory that was allocated\n");
    free_allocated_memory();
    printk("Exiting the message slot module\n");
    unregister_chrdev(MAJOR_NUMBER, "message_slot");
}

module_init(message_slot_init)
module_exit(message_slot_exit)