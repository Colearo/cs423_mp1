#define LINUX

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/proc_fs.h>
#include <linux/list.h>
#include <linux/slab.h>
#include <linux/timer.h>
#include <linux/jiffies.h>
#include <asm/uaccess.h>
#include "mp1_given.h"

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Kechen Lu");
MODULE_DESCRIPTION("CS-423 MP1");

#define DEBUG 1

// Declare all needed global variables for the proc fs initializaiton
// The mp1 directory
#define MP1_DIR "mp1"
struct proc_dir_entry * mp1_dir;
// The mp1/status entry
#define MP1_STAT "status"
struct proc_dir_entry * mp1_status;

// Define the structure to persist the PID and its CPU use time
struct registration_block {
    int pid;
    unsigned long used_time;
    struct list_head next;
};

// Declare a linked list to store the registered process information
// Initialize the empty linked list
LIST_HEAD(registration_list);

// Declare the timer callback function
static void timer_handler(unsigned long data);
// Declare the staic variable to timeout five seconds
static unsigned long five_sec;
// Declare the global kernel timer
DEFINE_TIMER(update_timer, timer_handler, 0, 0);

#define MAX_BUF_SIZE 4096
// Decalre the callback functions for proc read and write
// Write callback function for user space to write pid to the 
// /proc/mp1/status file
ssize_t register_pid(struct file *file, 
	const char __user *usr_buf, 
	size_t n, 
	loff_t *ppos) {
    // Local variable to store the result copied from user buffer
    char* kern_buf;
    // Variables used in the kstrtol()
    unsigned long pid_val, used_time;
    int ret;
    // Linked list entry
    struct registration_block* new_block_ptr;
    struct registration_block* cur, * temp;

    // Using vmalloc() to allocate buffer for kernel space
    kern_buf = (char *)kmalloc(MAX_BUF_SIZE * sizeof(char), GFP_KERNEL);
    if (!kern_buf) 
	return -ENOMEM;
    memset(kern_buf, 0, MAX_BUF_SIZE * sizeof(char));

    // If the input str is larger than buffer, return error
    if (n > MAX_BUF_SIZE || *ppos > 0) 
	return -EFAULT;
    if (copy_from_user(kern_buf, usr_buf, n))
	return -EFAULT;

    kern_buf[n] = 0;
    printk(KERN_DEBUG "ECHO %s", kern_buf); 

    ret = kstrtoul(kern_buf, 10, &pid_val);
    if (ret != 0 || pid_val >= ((1<<16)-1)) {
	printk(KERN_ALERT "Unrecognized pid number");
	return -EFAULT;
    }

    printk(KERN_DEBUG "PID %d", (int)pid_val); 

    // Through the helper function to get the cpu used time
    ret = get_cpu_use((int)pid_val, &used_time); 
    if (ret != 0) {
	printk(KERN_ALERT "Unrecognized or invalid pid number\n");
	return -EFAULT;
    }

    // Iterate the whole linked list to prevent insert repeated one of pid
    list_for_each_entry_safe(cur, temp, &registration_list, next) {
       // Access the entry and determine if the pid has been in the list
       if (cur->pid == (int)pid_val) {
	   printk(KERN_ALERT "Repeated pid number\n");
	   return -EFAULT;
       }
    }

    // Allocate a new block to store the result
    new_block_ptr = kmalloc(sizeof(*new_block_ptr), GFP_KERNEL);
    new_block_ptr->pid = (int)pid_val;
    new_block_ptr->used_time = used_time;
    INIT_LIST_HEAD(&new_block_ptr->next);
    // Add the entry to the linked list to register
    list_add(&new_block_ptr->next, &registration_list);

    kfree(kern_buf);
    return n;
}

// Read callback function for user space to read the proc file in
// /proc/mp1/status
ssize_t get_status(struct file *file, 
	char __user *usr_buf, 
	size_t n, 
	loff_t *ppos) {
    // Local variable to store the data would copy to user buffer
    char* kern_buf;
    int length = 0;
    // Linked list entry
    struct registration_block* cur, * temp;

    // Using vmalloc() to allocate buffer for kernel space
    kern_buf = (char *)kmalloc(MAX_BUF_SIZE * sizeof(char), GFP_KERNEL);
    if (!kern_buf) 
	return -ENOMEM;
    memset(kern_buf, 0, MAX_BUF_SIZE * sizeof(char));

    // If the input str is larger than buffer, return zero
    if (n < MAX_BUF_SIZE || *ppos > 0) 
	return 0;

    // Iterate the whole linked list to output to the user space read buf
    list_for_each_entry_safe(cur, temp, &registration_list, next) {
	length += sprintf(kern_buf + length, "PID %d: %lu\n", 
				cur->pid, cur->used_time);
    }
    
    printk(KERN_ALERT "Read this proc file %d\n", length);
    kern_buf[length] = 0;

    // Copy returned data from kernel space to user space
    if (copy_to_user(usr_buf, (const void *)kern_buf, length))
	return -EFAULT;
    *ppos = length;

    kfree(kern_buf);
    return length;
}


// File operations callback functions, overload read, write, open, and 
// so forth a series of ops
static const struct file_operations mp1_proc_fops = {
    .owner = THIS_MODULE,
    .write = register_pid,
    .read = get_status,
};

// Timer updating handler
static void timer_handler(unsigned long data) {
    unsigned long j = jiffies;
    printk(KERN_DEBUG "time handling here %lu\n", j);

    mod_timer(&update_timer, jiffies + five_sec);
}

// Work function for workqueue to schedule


// mp1_init - Called when module is loaded
int __init mp1_init(void)
{
   #ifdef DEBUG
   printk(KERN_ALERT "MP1 MODULE LOADING\n");
   #endif
   // Make a new proc dir /proc/mp1
   mp1_dir = proc_mkdir(MP1_DIR, NULL); 
   // Make a new proc entry /proc/mp1/status
   mp1_status = proc_create(MP1_STAT, 0666, mp1_dir, &mp1_proc_fops); 
   // Initialize the timeout delay 
   five_sec = msecs_to_jiffies(5000 * 1);
   mod_timer(&update_timer, jiffies + five_sec);

   printk(KERN_ALERT "MP1 MODULE LOADED\n");
   return 0;   
}

// mp1_exit - Called when module is unloaded
void __exit mp1_exit(void)
{
   struct registration_block* cur, * temp;
   #ifdef DEBUG
   printk(KERN_ALERT "MP1 MODULE UNLOADING\n");
   #endif
   // Remove all the proc file entry and dir we created before
   proc_remove(mp1_status);
   proc_remove(mp1_dir);

   // Remove all entries of the registration list
   list_for_each_entry_safe(cur, temp, &registration_list, next) {
       // Access the entry and delete/free memory space
       printk(KERN_ALERT "PID %d: [%lu]\n", cur->pid, cur->used_time);
       list_del(&cur->next);
       kfree(cur);
   }

   printk(KERN_ALERT "MP1 MODULE UNLOADED\n");
}

// Register init and exit funtions
module_init(mp1_init);
module_exit(mp1_exit);
