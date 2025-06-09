#include <linux/debugfs.h>
#include <linux/fs.h>
#include <linux/slab.h>
#include <linux/uaccess.h>
#include <linux/types.h>
#include <linux/export.h>
#include <linux/nova_debugfs_helpers.h>

struct nova_debugfs {
    struct dentry *root;
    struct dentry *rm_log0;
    struct dentry *rm_log1;
    struct dentry *rm_log2;
    struct dentry *status;
    void *gsp_mem;
};

struct nova_debugfs_file_data {
    struct nova_log_buffer_info *info;
    loff_t pos;
};

static int nova_debugfs_open(struct inode *inode, struct file *file)
{
    struct nova_debugfs_file_data *data;
    
    data = kmalloc(sizeof(*data), GFP_KERNEL);
    if (!data)
        return -ENOMEM;
    
    data->info = inode->i_private;
    data->pos = 0;
    file->private_data = data;
    
    return 0;
}

static int nova_debugfs_release(struct inode *inode, struct file *file)
{
    kfree(file->private_data);
    return 0;
}

static ssize_t nova_debugfs_read(struct file *file, char __user *buf,
                                size_t count, loff_t *ppos)
{
    struct nova_debugfs_file_data *data = file->private_data;
    struct nova_log_buffer_info *info = data->info;
    loff_t pos = *ppos;
    char hex_buf[64];
    size_t bytes_read = 0;
    size_t offset;
    
    if (!info || !info->data)
        return -EINVAL;
    
    // Each byte becomes 3 chars (2 hex + 1 space), plus newline every 16 bytes
    offset = pos / 3;
    
    if (offset >= info->size)
        return 0;  // EOF
    
    while (bytes_read < count && offset < info->size) {
        size_t line_offset = offset % 16;
        size_t bytes_in_line = min_t(size_t, 16 - line_offset, info->size - offset);
        size_t hex_pos = 0;
        size_t i;
        
        for (i = 0; i < bytes_in_line; i++) {
            u8 byte = ((u8 *)info->data)[offset + i];
            hex_pos += scnprintf(hex_buf + hex_pos, sizeof(hex_buf) - hex_pos,
                               "%02x ", byte);
        }
        
        if (line_offset + bytes_in_line == 16 || offset + bytes_in_line == info->size) {
            hex_buf[hex_pos - 1] = '\n';
        }
        if (bytes_read + hex_pos > count)
            break;
        if (copy_to_user(buf + bytes_read, hex_buf, hex_pos))
            return -EFAULT;
        
        bytes_read += hex_pos;
        offset += bytes_in_line;
        *ppos = offset * 3;
    }
    
    return bytes_read;
}

static const struct file_operations nova_debugfs_fops = {
    .owner = THIS_MODULE,
    .open = nova_debugfs_open,
    .release = nova_debugfs_release,
    .read = nova_debugfs_read,
    .llseek = default_llseek,
};

struct nova_debugfs *nova_debugfs_create(const char *name)
{
    struct nova_debugfs *debugfs;
    
    debugfs = kzalloc(sizeof(*debugfs), GFP_KERNEL);
    if (!debugfs)
        return NULL;
    
    debugfs->root = debugfs_create_dir(name, NULL);
    if (!debugfs->root) {
        kfree(debugfs);
        return NULL;
    }
    
    return debugfs;
}

void nova_debugfs_destroy(struct nova_debugfs *debugfs)
{
    if (!debugfs)
        return;
        
    debugfs_remove_recursive(debugfs->root);
    kfree(debugfs);
}

int nova_debugfs_create_log_files(struct nova_debugfs *debugfs,
                                 struct nova_log_buffer_info *loginit_info,
                                 struct nova_log_buffer_info *logintr_info,
                                 struct nova_log_buffer_info *logrm_info)
{
    if (!debugfs || !debugfs->root)
        return -EINVAL;
    
    debugfs->rm_log0 = debugfs_create_file("init_log", 0444, debugfs->root,
                                          loginit_info, &nova_debugfs_fops);
    
    debugfs->rm_log1 = debugfs_create_file("intr_log", 0444, debugfs->root,
                                          logintr_info, &nova_debugfs_fops);
    
    debugfs->rm_log2 = debugfs_create_file("rm_log", 0444, debugfs->root,
                                          logrm_info, &nova_debugfs_fops);
    
    return 0;
}

EXPORT_SYMBOL_GPL(nova_debugfs_create);
EXPORT_SYMBOL_GPL(nova_debugfs_destroy);
EXPORT_SYMBOL_GPL(nova_debugfs_create_log_files);
