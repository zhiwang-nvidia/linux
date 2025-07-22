/* SPDX-License-Identifier: MIT */
/*
 * Copyright © 2025 NVIDIA Corporation
 */

#ifndef __NVIDIA_VGPU_VFIO_PF_INTF_H__
#define __NVIDIA_VGPU_VFIO_PF_INTF_H__

/**
 * DOC: Attachment of the NVIDIA vGPU VFIO driver
 *
 * The NVIDIA vGPU VFIO driver relies on PF driver functions to manage
 * vGPUs. To access these functions, an attachment mechanism is introduced
 * prior to the VFIO driver invoking any PF driver operations.
 *
 * A "handle" represents the connection between the PF and the VFIO driver.
 * The VFIO driver first generates this handle to verify whether the PF
 * driver has initialized vGPU VFIO operations. If so, this indicates that
 * the PF driver supports vGPU functionality.
 *
 * The VFIO driver then checks if vGPU support is enabled in the PF driver.
 * If it is, the PF driver acquires a lock on the handle, VFIO driver attaches
 * handle data to it or uses the existing data to complete its initialization.
 * Afterward, the PF driver releases the lock.
 *
 * Once the attachment is complete, the VFIO driver can interact with the
 * PF driver via the `nova_vgpu_vfio_ops` interface.
 */

#define NVIDIA_VGPU_MAX_PF_DRIVER_CAPS 512

enum {
	NVIDIA_VGPU_PF_DRIVER_CAP_HAS_GSP_CLIENT_ALLOC = 0,
	NVIDIA_VGPU_PF_DRIVER_CAP_HAS_RM_ALLOC,
	NVIDIA_VGPU_PF_DRIVER_CAP_HAS_CHID_ALLOC,
	NVIDIA_VGPU_PF_DRIVER_CAP_HAS_FB_MEM_ALLOC,
	NVIDIA_VGPU_PF_DRIVER_CAP_HAS_FB_MEM_BAR1_MAP,
	NVIDIA_VGPU_PF_DRIVER_CAP_HAS_CHAN_MEM_MAP,
	NVIDIA_VGPU_PF_DRIVER_CAP_HAS_CE_CHAN_ALLOC,
	NVIDIA_VGPU_PF_DRIVER_CAP_HAS_PUSHBUF_SUBMIT,
	NVIDIA_VGPU_PF_DRIVER_CAP_HAS_PF_EVENT_DELIVERY,
};

/**
 * struct nvidia_vgpu_vfio_handle_data - VFIO driver handle data
 *
 * @vfio.handle: pointer to the driver handle
 * @vfio.module: pointer to the VFIO driver module
 * @vfio.private_data: pointer to the VFIO driver private data
 * @vfio.pf_event_notify_fn: the PF driver event notify callback
 * @pf.driver_is_unbound: the PF driver is unbound
 * @pf.driver_caps: the PF driver capabilitiles
 */
struct nvidia_vgpu_vfio_handle_data {
	struct {
		void *handle;
		struct module *module;
		void *private_data;
		int (*pf_event_notify_fn)(void *priv, unsigned int event, void *data);
		void (*pf_detach_handle_fn)(void *handle,
					    struct nvidia_vgpu_vfio_handle_data *handle_data);
	} vfio;

	struct {
		bool driver_is_unbound;
		DECLARE_BITMAP(driver_caps, NVIDIA_VGPU_MAX_PF_DRIVER_CAPS);
	} pf;
};

struct nvidia_vgpu_vfio_attach_handle_data {
	int (*pf_attach_handle_fn)(void *handle, struct nvidia_vgpu_vfio_handle_data *handle_data,
				   struct nvidia_vgpu_vfio_attach_handle_data *attach_handle_data);
	void *vgpu_mgr_handle;
	int (*init_vfio_fn)(void *priv, void *data);
	void *init_vfio_fn_data;
};

/**
 * struct nvidia_vgpu_gsp_client - the GSP client for VFIO driver
 *
 * @gsp_client: GSP object RmClient
 * @gsp_device: GSP object RmDevice
 */
struct nvidia_vgpu_gsp_client {
	void *gsp_client;
	void *gsp_device;
};

struct nvidia_vgpu_vfio_ops {
	/**
	 * vgpu_is_enabled() - if vGPU support is enabled in the PF driver.
	 * @handle: the VFIO driver handle.
	 *
	 * Return: True if vGPU support is enabled.
	 */
	bool (*vgpu_is_enabled)(void *handle);
	/**
	 * attach_handle() - attach handle data to the driver handle.
	 * @handle: the VFIO driver handle.
	 * @data: the driver handle data pointer.
	 *
	 * Attach the handle_data to PF driver if none exists,
	 * the handle must be locked before attach.
	 *
	 * Return: zero on success, others on errors.
	 */
	int (*attach_handle)(void *handle,
			     struct nvidia_vgpu_vfio_attach_handle_data *data);
	/**
	 * detach_handle() - detach handle data from the driver handle.
	 * @handle: the VFIO driver handle.
	 *
	 * The handle must be locked before detach.
	 */
	void (*detach_handle)(void *handle);
	/**
	 * alloc_gsp_client() - allocate a GSP client.
	 * @handle: the VFIO driver handle.
	 * @client: the GSP client.
	 *
	 * Return: zero on success, others on errors.
	 */
	int (*alloc_gsp_client)(void *handle,
				struct nvidia_vgpu_gsp_client *client);
	/**
	 * free_gsp_client() - free a GSP client.
	 * @client: the GSP client container.
	 */
	void (*free_gsp_client)(struct nvidia_vgpu_gsp_client *client);
	/**
	 * get_gsp_client_handle() - get the handle of a GSP client.
	 * @client: the GSP client.
	 *
	 * When allocating the GSP client from the GSP, the PF driver composes a
	 * handle to repsent the client.
	 *
	 * Return: the handle that PF driver composed.
	 */
	u32 (*get_gsp_client_handle)(struct nvidia_vgpu_gsp_client *client);
	/**
	 * rm_ctrl_get() - get a GSP RPC container of RM control.
	 * @client: the GSP client.
	 * @cmd: the RM control command.
	 * @size: the size of the RM control.
	 *
	 * Return: the allocated GSP RPC container on success. an error pointer on errors.
	 */
	void *(*rm_ctrl_get)(struct nvidia_vgpu_gsp_client *client,
			     u32 cmd, u32 size);
	/**
	 * rm_ctrl_wr() - send the RM control to GSP without a reply.
	 * @client: the GSP client.
	 * @ctrl: the RM control GSP RPC container.
	 *
	 * Note that the PF driver should free the GSP RPC container.
	 *
	 * Return: zero on success. others on errors.
	 */
	int (*rm_ctrl_wr)(struct nvidia_vgpu_gsp_client *client,
			  void *ctrl);
	/**
	 * rm_ctrl_rd() - send the RM control to GSP and requires a reply.
	 * @client: the GSP client.
	 * @cmd: the RM control command.
	 * @size: the size of the RM control.
	 *
	 * Return: the GSP RPC container as reply on success. an error pointer on errors.
	 */
	void *(*rm_ctrl_rd)(struct nvidia_vgpu_gsp_client *client, u32 cmd,
			    u32 size);
	/**
	 * rm_ctrl_done() - free the reply allocated by rd_ctrl_rd().
	 * @client: the GSP client.
	 * @ctrl: the RM control GSP RPC container.
	 */
	void (*rm_ctrl_done)(struct nvidia_vgpu_gsp_client *client,
			     void *ctrl);
	/**
	 * alloc_chids() - allocate the CHIDs.
	 * @handle: the VFIO driver handle.
	 * @offset: return the beginning offset of the CHIDs.
	 * @count: the amount of the CHIDs going to be allocated.
	 *
	 * Return: zero on success. others on errors.
	 */
	int (*alloc_chids)(void *handle, u32 *offset, u32 count);
	/**
	 * free_chids() - free the CHIDs.
	 * @handle: the VFIO driver handle.
	 * @offset: the beginning offset of the allocated CHIDs.
	 * @count: the amount of the CHIDs allocated.
	 */
	void (*free_chids)(void *handle, u32 offset, u32 count);
	/**
	 * get_avails_chis() - get the total amount of CHIDs for VFIO driver.
	 * @handle: the VFIO driver handle.
	 *
	 * Return: the amount of available CHIDS for VFIO driver.
	 */
	u32 (*get_avail_chids)(void *handle);
};

struct nvidia_vgpu_vfio_ops *nova_vgpu_get_vfio_ops(void *handle);

#endif
