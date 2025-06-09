#ifndef _NOVA_DEBUGFS_HELPERS_H
#define _NOVA_DEBUGFS_HELPERS_H

#include <linux/types.h>

struct nova_debugfs;

struct nova_log_buffer_info {
    const char *name;
    void *data;
    size_t size;
};

struct nova_debugfs *nova_debugfs_create(const char *name);
void nova_debugfs_destroy(struct nova_debugfs *debugfs);
int nova_debugfs_create_log_files(struct nova_debugfs *debugfs,
                                 struct nova_log_buffer_info *loginit_info,
                                 struct nova_log_buffer_info *logintr_info,
                                 struct nova_log_buffer_info *logrm_info);

#endif
