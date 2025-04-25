// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright © 2025 NVIDIA Corporation
 */

#include <linux/debugfs.h>

#include "vfio.h"

static void free_vgpu_log(struct nvidia_vgpu_vfio_log *log)
{
	debugfs_remove(log->dentry);
	kvfree(log->mem);
	log->mem = NULL;
}

static void clean_vgpu_logs(struct nvidia_vgpu_vfio *nvdev)
{
	free_vgpu_log(&nvdev->log_init_task);
	free_vgpu_log(&nvdev->log_vgpu_task);
	free_vgpu_log(&nvdev->log_kernel);
}

static int alloc_vgpu_log(struct nvidia_vgpu_vfio_log *log, struct device *dev,
			  struct dentry *root, const char *name, u64 size)
{
	void *path = NULL;

	path = kzalloc(PATH_MAX, GFP_KERNEL);
	if (!path)
		return -ENOMEM;

	log->mem = kvzalloc(size, GFP_KERNEL);
	if (!log->mem) {
		kfree(log->mem);
		return -ENOMEM;
	}

	log->blob.size = size;
	log->blob.data = log->mem;

	snprintf(path, PATH_MAX, "%s-%s", dev_name(dev), name);
	log->dentry = debugfs_create_blob(path, 0400, root, &log->blob);

	kfree(path);
	path = NULL;

	if (IS_ERR(log->dentry)) {
		kfree(log->mem);
		return PTR_ERR(log->dentry);
	}
	return 0;
}

static int setup_vgpu_logs(struct nvidia_vgpu_vfio *nvdev, struct dentry *root)
{
	struct nvidia_vgpu_mgr *vgpu_mgr = nvdev->vgpu_mgr;
	struct device *dev = &nvdev->core_dev.pdev->dev;
	int ret;

	ret = alloc_vgpu_log(&nvdev->log_init_task, dev, root, "init_task_log",
			     vgpu_mgr->init_task_log_size);
	if (ret)
		return ret;

	ret = alloc_vgpu_log(&nvdev->log_vgpu_task, dev, root, "vgpu_task_log",
			     vgpu_mgr->vgpu_task_log_size);
	if (ret) {
		free_vgpu_log(&nvdev->log_init_task);
		return ret;
	}

	ret = alloc_vgpu_log(&nvdev->log_kernel, dev, root, "kernel_log",
			     vgpu_mgr->kernel_log_size);
	if (ret) {
		free_vgpu_log(&nvdev->log_init_task);
		free_vgpu_log(&nvdev->log_vgpu_task);
		return ret;
	}
	return 0;
}

int nvidia_vgpu_vfio_setup_debugfs(struct nvidia_vgpu_vfio *nvdev)
{
	struct dentry *root = nvidia_vgpu_get_debugfs_root();
	int ret;

	if (IS_ERR(root))
		return PTR_ERR(root);

	ret = setup_vgpu_logs(nvdev, root);
	if (ret) {
		nvidia_vgpu_put_debugfs_root();
		return ret;
	}

	return 0;
}

void nvidia_vgpu_vfio_clean_debugfs(struct nvidia_vgpu_vfio *nvdev)
{
	clean_vgpu_logs(nvdev);
	nvidia_vgpu_put_debugfs_root();
}

void nvidia_vgpu_vfio_update_logs(struct nvidia_vgpu_vfio *nvdev)
{
	struct nvidia_vgpu_vfio_log *logs[] = {
		&nvdev->log_init_task,
		&nvdev->log_vgpu_task,
		&nvdev->log_kernel,
	};
	int i;

	for (i = 0; i < ARRAY_SIZE(logs); i++)
		memcpy(logs[i]->mem, logs[i]->blob.data, logs[i]->blob.size);
}
