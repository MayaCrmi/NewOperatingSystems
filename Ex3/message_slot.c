#include "message_slot.h"

#undef __KERNEL__
#define __KERNEL__
#undef MODULE
#define MODULE

/*
make -> build the module
sudo insmod message_slot.ko -> install the module
sudo mknod /dev/msg8 c 235 8
sudo chmod 777 /dev/msg8
./message_sender /dev/msg8 99 "Writing this msg to channel number 99"
*/

/* from Eran's recitations' code */
#include <linux/kernel.h>   /* We're doing kernel work */
#include <linux/module.h>   /* Specifically, a module */
#include <linux/fs.h>       /* for register_chrdev */
#include <linux/errno.h>
#include <linux/uaccess.h>  /* for get_user and put_user */
#include <linux/string.h>   /* for memset. NOTE - not string.h!*/
#include <linux/slab.h>

MODULE_LICENSE("GPL");
static struct device_data devices_list[256];

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

struct channel_data* find_channel(struct device_data* device, unsigned long channel_num) {
    struct channel_data* curr = device -> head;
    printk("inside find_channel now\n");
    if (curr) {
        while (curr -> next) {
            if ((curr -> channel_id) == channel_num) {
                printk("Found channel number %lu in find_channel\n", curr -> channel_id);
                return curr;
            }
            curr = curr -> next;
        }
    }
    printk("now finishing find_channel, returning NULL\n");
    return curr;
}

static int device_open(struct inode* inode, struct file* file) {
    int minor = iminor(inode);
    struct device_data* new_device;
    printk("open: Going in\n");
    printk("open: Current minor is %d", minor);
    if ((devices_list[minor].opened) == 0) {
        printk("open: devices_list[minor].opened was 0\n");
        new_device = kmalloc(sizeof(struct device_data), GFP_KERNEL);
        if (!new_device) {
            return -ENOMEM;
        }
        new_device -> minor_number = minor;
        new_device -> opened = 1;
        devices_list[minor] = *new_device;
        file -> private_data = (void*) new_device;
    } else {
        printk("open: devices_list[minor].opened was 1\n");
        file -> private_data = (void*) &devices_list[minor];
    }
    printk("open: Finishing\n");
    return 0;
}

static long device_ioctl(struct file* file, unsigned int cmd, unsigned long channel) {    
    struct device_data* device;
    struct channel_data* curr_channel;
    printk("ioctl: Going in\n");
    if ((channel == 0) || (cmd != MSG_SLOT_CHANNEL)) {
        return -EINVAL;
    }
    device = (struct device_data*)file -> private_data;
    curr_channel = find_channel(device, channel);
    if (!curr_channel) {
        printk("ioctl: There was no curr_channel\n");
        curr_channel = kmalloc(sizeof(struct channel_data), GFP_KERNEL);
        if (!curr_channel) {
            return -ENOMEM;
        }
        curr_channel -> channel_id = channel;
        curr_channel -> next = device -> head;
        device -> head = curr_channel;
        
    }
    (device -> open_channel_id) = channel;
    (device -> open_channel) = curr_channel;
    devices_list[device->minor_number] = *device;
    printk("ioctl: Finishing, working channel is %lu\n", device -> open_channel_id);
    return 0;
}

static ssize_t device_read(struct file* file, char* buffer, size_t msglen, loff_t* offset) {
    int i=0;
    unsigned long curr_channel_id;
    int minor, channel_msglen;
    struct device_data* device;
    struct channel_data* curr_channel;
    printk("read: Going in\n");
    minor = iminor(file -> f_inode);
    printk("read: Current minor is %d", minor);
    device = (struct device_data*)file -> private_data;
    curr_channel_id = device -> open_channel_id;
    if (buffer == NULL || curr_channel_id == 0) {
        return -EINVAL;
    }
    curr_channel = find_channel(device, curr_channel_id);
    if (!curr_channel || curr_channel -> channel_id == 0) {
        printk("read: inside error, channel_id is %lu\n", curr_channel -> channel_id);
        return -ENOMEM;
    }
    printk("read: didn't get error, channel_id is %lu\n", curr_channel -> channel_id);
    channel_msglen = curr_channel -> msglen;
    printk("read: updated message length for channel to be %d\n", channel_msglen);
    if (channel_msglen <= 0) {
        return -EWOULDBLOCK;
    }
    if (channel_msglen > msglen) {
        return -ENOSPC;
    }
    while (i < msglen && i < channel_msglen) {
        char to_put = curr_channel -> msg[i];
        char* put_here = &buffer[i];
        put_user(to_put, put_here);
        i += 1;
    }
    printk("read: finished writing with i being %d and msglen being %zu \n", i, msglen);
    if (i != channel_msglen) {
        printk("The message hasn't been read successfully\n");
        return -1;
    }
    printk("read: LAST PRINT BEFORE RETURN\n");
    return i;
}

static ssize_t device_write(struct file* file, const char* buffer, size_t msglen, loff_t* offset) { 
    int minor;
    unsigned long curr_channel_id;
    struct device_data* device;
    struct channel_data* curr_channel;
    int i=0;
    printk("write: Going in\n");
    minor = iminor(file -> f_inode);
    device = (struct device_data*)file -> private_data;
    curr_channel_id = device -> open_channel_id;
    if (buffer == NULL || curr_channel_id == 0) {
        return -EINVAL;
    }
    if (msglen == 0 || msglen > MAX_BUFFER) {
        return -EMSGSIZE;
    }
    curr_channel = find_channel(device, curr_channel_id);
    if (!curr_channel || curr_channel -> channel_id == 0) {
        printk("write: inside error, channel_id is %lu\n", curr_channel -> channel_id);
        return -EINVAL;
    }
    printk("write: didn't get error, channel_id is %lu\n", curr_channel -> channel_id);
    while (i < msglen) {
        char put_here = curr_channel -> msg[i];
        const char* to_put = &buffer[i];
        get_user(put_here, to_put);
        i++;
    }
    if (i != msglen) {
        printk("The message hasn't been written successfully\n");
        return -1;
    }
    curr_channel -> msglen = i;
    printk("write: Finishing with msglen being %d\n", i);
    return i;
}

struct file_operations fops = {
    .owner = THIS_MODULE,
    .open = device_open,
    .unlocked_ioctl = device_ioctl,
    .read = device_read, 
    .write = device_write, 
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