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

// void free_allocated_memory(void) {
//     int i;
//     for (i=0 ; i < 256 ; i++) {
//         channel_data curr = devices_list[i] -> head;
//         while (curr) {
//             channel_data* dummy = curr;
//             curr = curr -> next;
//             kfree(dummy);
//         }
//         }
// }

struct channel_data* find_channel(struct device_data* device, int channel_num) {
    struct channel_data* curr = device -> head;
    printk("inside find_channel now\n");
    if (curr) {
        while (curr -> next) {
            if ((curr -> channel_id) == channel_num) {
                printk("Found channel number %ld in find_channel\n", curr -> channel_id);
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
    printk("open module 1\n");
    if ((devices_list[minor].head) == NULL) {
        printk("open module 2\n");
        new_device = kmalloc(sizeof(struct device_data), GFP_KERNEL);
        if (!new_device) {
            return -ENOMEM;
        }
        printk("open module 3\n");
        new_device -> minor_number = minor;
        printk("open module 4\n");
        devices_list[minor] = *new_device;
        printk("open module 5\n");
        file -> private_data = (void*) new_device;
    }
    printk("open module LAST PRINT BEFORE RETURN got here 6\n");
    return 0;
}

static long device_ioctl(struct file* file, unsigned int cmd, unsigned long channel) {    
    struct device_data* device;
    struct channel_data* curr_channel;
    printk("ioctl module 1\n");
    if ((channel == 0) || (cmd != MSG_SLOT_CHANNEL)) {
        return -EINVAL;
    }
    printk("ioctl module 2\n");
    device = (struct device_data*)file -> private_data;
    curr_channel = find_channel(device, channel);
    if (!curr_channel) {
        printk("ioctl module 3\n");
        curr_channel = kmalloc(sizeof(struct channel_data), GFP_KERNEL);
        if (!curr_channel) {
            printk("ioctl module 4\n");
            return -ENOMEM;
        }
        curr_channel -> channel_id = channel;
        printk("ioctl module 5\n");
        curr_channel -> next = device -> head;
        device -> head = curr_channel;
    }
    printk("ioctl module 6\n");
    (device -> open_channel_id) = channel;
    printk("ioctl module 7, working channel is %lu", device -> open_channel_id);
    return 0;
}

static ssize_t device_read(struct file* file, char* buffer, size_t msglen, loff_t* offset) {
    int i=0;
    int minor, curr_channel_id, channel_msglen;
    struct device_data* device;
    struct channel_data* curr_channel;
    printk("read module 1\n");
    minor = iminor(file -> f_inode);
    printk("read module 2\n");
    device = (struct device_data*)file -> private_data;
    printk("read module 3\n");
    curr_channel_id = device -> open_channel_id;
    printk("read module 4\n");
    if (buffer == NULL || curr_channel_id == 0) {
        return -EINVAL;
    }
    printk("read module 5\n");
    curr_channel = find_channel(device, curr_channel_id);
    printk("read module 6\n");
    if (curr_channel == NULL) {
        return -ENOMEM;
    }
    printk("read module 7\n");
    channel_msglen = curr_channel -> msglen;
    printk("read module 8\n");
    if (channel_msglen <= 0) {
        return -EWOULDBLOCK;
    }
    printk("read module 9\n");
    if (channel_msglen > msglen) {
        return -ENOSPC;
    }
    printk("read module 10\n");
    while (i < msglen && i < channel_msglen) {
        char to_put = curr_channel -> msg[i];
        char* put_here = &buffer[i];
        put_user(to_put, put_here);
        i += 1;
    }
    printk("read module 11\n");
    if (i != msglen) {
        printk("The message hasn't been read successfully\n");
        return -1;
    }
    printk("read module LAST PRINT BEFORE RETURN 12\n");
    return i;
}

static ssize_t device_write(struct file* file, const char* buffer, size_t msglen, loff_t* offset) { 
    int minor, curr_channel_id;
    struct device_data* device;
    struct channel_data* curr_channel;
    int i=0;
    printk("write module 1\n");
    minor = iminor(file -> f_inode);
    printk("write module 2\n");
    device = (struct device_data*)file -> private_data;
    printk("write module 3\n");
    curr_channel_id = device -> open_channel_id;
    printk("write module 4\n");
    if (buffer == NULL || curr_channel_id == 0) {
        return -EINVAL;
    }
    printk("write module 5\n");
    if (msglen == 0 || msglen > MAX_BUFFER) {
        return -EMSGSIZE;
    }
    printk("write module 6\n");
    curr_channel = find_channel(device, curr_channel_id);
    printk("write module 7\n");
    if (!curr_channel) {
        return -EINVAL;
    }
    printk("write module 8\n");
    while (i < msglen) {
        char put_here = curr_channel -> msg[i];
        const char* to_put = &buffer[i];
        get_user(put_here, to_put);
        i++;
    }
    printk("write module 9\n");
    if (i != msglen) {
        printk("The message hasn't been written successfully\n");
        return -1;
    }
    printk("write module got here 10\n");
    printk("1) i is %d while curr_channel's msglen is %d\n", i, curr_channel -> msglen);
    curr_channel -> msglen = i;
    printk("2) i is %d while curr_channel's msglen is %d\n", i, curr_channel -> msglen);
    printk("write module LAST PRINT BEFORE RETURN 10\n");
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
    //free_allocated_memory();
    printk("Exiting the message slot module\n");
    unregister_chrdev(MAJOR_NUMBER, "message_slot");
}



module_init(message_slot_init)
module_exit(message_slot_exit)