#include <linux/backing-dev-defs.h> // For struct backing_dev_info
#include <linux/file.h>             // For struct file_operations
#include <linux/kernel.h>           // For printk
#include <linux/ktime.h>            // For ktime_get and ktime_to_ns
#include <linux/module.h>           // For module_init and module_exit
#include <linux/namei.h>            // For kern_path
#include <linux/proc_fs.h>          // Include the necessary header file
#include <linux/uaccess.h>          // For copy_from_user
#include <linux/xarray.h>           // For xa_load and xa_is_value

#define MAX_PATH_LEN 256

static char user_input_buffer[MAX_PATH_LEN + 1];

static void find_pg_hole(struct file *file) {
    struct backing_dev_info *bdi;
    struct address_space *mapping;
    struct folio *folio;

    unsigned long missing_page = 1;
    pgoff_t total_pages;

    pgoff_t index = 16; // let's say this is missing page index
    pgoff_t left_offset = 0;
    pgoff_t right_offset = 0;
    int left_hit = 0;
    int right_hit = 0;

    ktime_t start_time, end_time;
    s64 time_diff;

    total_pages = file->f_inode->i_size >> PAGE_SHIFT;
    total_pages += !!(file_inode(file)->i_size & ~PAGE_MASK);   // round up

    mapping = file->f_mapping;
    bdi = mapping->host->i_sb->s_bdi;

    start_time = ktime_get();

    // left scan
    do {
        left_offset++;
        folio = xa_load(&mapping->i_pages, index - left_offset);
        if (folio && !xa_is_value(folio)) {
            left_hit = 1;
            break;
        } else {
            missing_page++;
        }
    } while (0 < index - left_offset && left_offset < bdi->ra_pages / 2);

    // right scan
    do {
        right_offset++;
        folio = xa_load(&mapping->i_pages, index + right_offset);
        if (folio && !xa_is_value(folio)) {
            right_hit = 1;
            break;
        } else {
            missing_page++;
        }
    } while (index + right_offset < total_pages - 1 &&
             right_offset < bdi->ra_pages / 2 - 1);

    end_time = ktime_get();
    time_diff = ktime_to_ns(ktime_sub(end_time, start_time));

    printk(KERN_INFO "pghole_scanner: Left offset: %lu (%s)\n", index - left_offset,
           (left_hit) ? "hit" : "miss");
    printk(KERN_INFO "pghole_scanner: Missing index: %lu\n", index);
    printk(KERN_INFO "pghole_scanner: Right offset: %lu (%s)\n", index + right_offset,
           (right_hit) ? "hit" : "miss");
    printk(KERN_INFO "pghole_scanner: hole range: %lu - %lu\n", index - left_offset + left_hit,
           index + right_offset - right_hit);
    printk(KERN_INFO "pghole_scanner: Missing pages: %lu\n", missing_page);
    printk(KERN_INFO "pghole_scanner: Time taken: %lld ns\n", time_diff);
}

ssize_t pghole_scanner_write(struct file *file, const char __user *buffer,
                             size_t count, loff_t *pos) {
    struct path path;
    struct file *file_to_scan;
    int ret;

    if (count > MAX_PATH_LEN) {
        printk(KERN_ERR "Path too long.\n");
        return -EINVAL;
    }

    // Copy the user input to the kernel space
    ret = copy_from_user(user_input_buffer, buffer, count);
    if (ret) {
        printk(KERN_ERR "Error copying from user space.\n");
        return ret;
    }
    user_input_buffer[count - 1] = '\0'; // remove newline character
    printk(KERN_INFO "pghole_scanner: user input: %s\n", user_input_buffer);

    // Get the path from the user input
    ret = kern_path(user_input_buffer, LOOKUP_FOLLOW, &path);
    if (ret) {
        printk(KERN_ERR "pghole_scanner: Error getting path.\n");
        return ret;
    }

    // Open the file
    file_to_scan = dentry_open(&path, O_RDONLY, current_cred());
    if (IS_ERR(file_to_scan)) {
        printk(KERN_ERR "pghole_scanner: Error opening file.\n");
        return PTR_ERR(file_to_scan);
    }

    // Scan the page cache
    find_pg_hole(file_to_scan);

    // Cleanup
    fput(file_to_scan);
    path_put(&path);

    return count;
}

static const struct proc_ops pghole_scanner_fops = {
    .proc_write = pghole_scanner_write,
};

static int __init pghole_scanner_init(void) {
    proc_create("pghole_scanner", 06, NULL, &pghole_scanner_fops);
    printk(KERN_INFO "pghole_scanner module loaded.\n");
    return 0;
}

static void __exit pghole_scanner_exit(void) {
    remove_proc_entry("pghole_scanner", NULL);
    printk(KERN_INFO "pghole_scanner module unloaded.\n");
}

MODULE_LICENSE("GPL");
module_init(pghole_scanner_init);
module_exit(pghole_scanner_exit);
