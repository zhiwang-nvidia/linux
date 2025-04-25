// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright © 2025 NVIDIA Corporation
 */

#include <linux/debugfs.h>

#include "vgpu_mgr.h"

struct debugfs_root {
	/* mutex to protect the debugfs_root */
	struct mutex mutex;
	struct kref refcount;
	struct dentry *root;
};

struct debugfs_root debugfs_root = {
	.mutex = __MUTEX_INITIALIZER(debugfs_root.mutex),
};

struct dentry *nvidia_vgpu_get_debugfs_root(void)
{
	struct debugfs_root *root = &debugfs_root;
	struct dentry *dentry;

	mutex_lock(&root->mutex);
	if (root->root) {
		kref_get(&root->refcount);
		dentry = root->root;
		goto out_unlock;
	}

	dentry = debugfs_create_dir("nvidia-vgpu", NULL);
	if (IS_ERR(dentry))
		goto out_unlock;

	kref_init(&root->refcount);
	root->root = dentry;

out_unlock:
	mutex_unlock(&root->mutex);
	return dentry;
}

static void debugfs_root_release(struct kref *kref)
{
	struct debugfs_root *root = container_of(kref, struct debugfs_root, refcount);

	debugfs_remove(root->root);
	root->root = NULL;
}

void nvidia_vgpu_put_debugfs_root(void)
{
	struct debugfs_root *root = &debugfs_root;

	mutex_lock(&root->mutex);
	if (WARN_ON(!root->root))
		goto out_unlock;

	kref_put(&root->refcount, debugfs_root_release);

out_unlock:
	mutex_unlock(&root->mutex);
}
