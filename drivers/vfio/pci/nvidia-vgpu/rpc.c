// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright © 2025 NVIDIA Corporation
 */

#include <linux/delay.h>
#include <linux/kernel.h>

#include <nvrm/vgpu.h>

#include "debug.h"
#include "vgpu_mgr.h"

#define NV_VIRTUAL_FUNCTION_PRIV_DOORBELL (0xb80000 + 0x2200)

static void trigger_doorbell(struct nvidia_vgpu *vgpu)
{
	struct nvidia_vgpu_mgr *vgpu_mgr = vgpu->vgpu_mgr;

	u32 v = vgpu->info.gfid * 32 + 17;

	writel(v, vgpu_mgr->bar0_vaddr + NV_VIRTUAL_FUNCTION_PRIV_DOORBELL);
	readl(vgpu_mgr->bar0_vaddr + NV_VIRTUAL_FUNCTION_PRIV_DOORBELL);
}

static void send_rpc_request(struct nvidia_vgpu *vgpu, u32 msg_type,
			     void *data, u64 size)
{
	struct nvidia_vgpu_rpc *rpc = &vgpu->rpc;
	VGPU_CPU_GSP_CTRL_BUFF_REGION *ctrl_buf = rpc->ctrl_buf;

	if (data && size)
		memcpy_toio(rpc->msg_buf, data, size);

	writel(msg_type, &ctrl_buf->message_type);

	rpc->msg_seq_num++;
	writel(rpc->msg_seq_num, &ctrl_buf->message_seq_num);

	trigger_doorbell(vgpu);
}

static int wait_for_response(struct nvidia_vgpu *vgpu)
{
	struct nvidia_vgpu_rpc *rpc = &vgpu->rpc;
	VGPU_CPU_GSP_RESPONSE_BUFF_REGION *resp_buf = rpc->resp_buf;

	u64 timeout = 120 * 1000000; /* 120s */

	do {
		if (readl(&resp_buf->message_seq_num_processed) == rpc->msg_seq_num)
			break;

		usleep_range(1, 2);
	} while (--timeout);

	return timeout ? 0 : -ETIMEDOUT;
}

static int recv_rpc_response(struct nvidia_vgpu *vgpu, void *data,
			     u64 size, u32 *result)
{
	struct nvidia_vgpu_rpc *rpc = &vgpu->rpc;
	VGPU_CPU_GSP_RESPONSE_BUFF_REGION *resp_buf = rpc->resp_buf;
	int ret;

	ret = wait_for_response(vgpu);
	if (result)
		*result = resp_buf->result_code;

	if (ret)
		return ret;

	if (data && size)
		memcpy_fromio(data, rpc->msg_buf, size);

	return 0;
}

/**
 * nvidia_vgpu_rpc_call - vGPU host RPC call.
 * @vgpu: the vGPU instance.
 * @msg_type: the RPC message.
 * @data: the RPC data.
 * @size: the RPC size.
 *
 * Returns: zero on success, others on failure.
 */
int nvidia_vgpu_rpc_call(struct nvidia_vgpu *vgpu, u32 msg_type,
			 void *data, u64 size)
{
	struct nvidia_vgpu_rpc *rpc = &vgpu->rpc;
	u32 result;
	int ret;

	if (WARN_ON(msg_type >= NV_VGPU_CPU_RPC_MSG_MAX) ||
	    size > VGPU_CPU_GSP_MESSAGE_BUFF_REGION_SIZE ||
	    (size != 0 && !data))
		return -EINVAL;

	mutex_lock(&rpc->lock);

	send_rpc_request(vgpu, msg_type, data, size);
	ret = recv_rpc_response(vgpu, data, size, &result);

	mutex_unlock(&rpc->lock);
	if (ret || result) {
		vgpu_error(vgpu, "fail to recv RPC: result %u\n",
			   result);
		return -EINVAL;
	}
	return ret;
}

/**
 * nvidia_vgpu_clean_rpc - clean vGPU host RPCl
 * @vgpu: the vGPU instance.
 */
void nvidia_vgpu_clean_rpc(struct nvidia_vgpu *vgpu)
{
}

static void init_rpc_buf_pointers(struct nvidia_vgpu *vgpu)
{
	struct nvidia_vgpu_mgmt *mgmt = &vgpu->mgmt;
	struct nvidia_vgpu_rpc *rpc = &vgpu->rpc;

	rpc->ctrl_buf = mgmt->ctrl_vaddr;
	rpc->resp_buf = rpc->ctrl_buf + VGPU_CPU_GSP_CTRL_BUFF_REGION_SIZE;
	rpc->msg_buf = rpc->resp_buf + VGPU_CPU_GSP_RESPONSE_BUFF_REGION_SIZE;
	rpc->migration_buf = rpc->msg_buf + VGPU_CPU_GSP_MESSAGE_BUFF_REGION_SIZE;
	rpc->error_buf = rpc->migration_buf + VGPU_CPU_GSP_MIGRATION_BUFF_REGION_SIZE;
}

static void init_ctrl_buf_offsets(struct nvidia_vgpu *vgpu)
{
	struct nvidia_vgpu_rpc *rpc = &vgpu->rpc;
	VGPU_CPU_GSP_CTRL_BUFF_REGION *ctrl_buf;
	u64 offset = 0;

	ctrl_buf = rpc->ctrl_buf;

	writel(VGPU_CPU_GSP_CTRL_BUFF_VERSION, &ctrl_buf->version);

	offset = VGPU_CPU_GSP_CTRL_BUFF_REGION_SIZE;
	writeq(offset, &ctrl_buf->response_buff_offset);

	offset += VGPU_CPU_GSP_RESPONSE_BUFF_REGION_SIZE;
	writeq(offset, &ctrl_buf->message_buff_offset);

	offset += VGPU_CPU_GSP_MESSAGE_BUFF_REGION_SIZE;
	writeq(offset, &ctrl_buf->migration_buff_offset);

	offset += VGPU_CPU_GSP_MIGRATION_BUFF_REGION_SIZE;
	writeq(offset, &ctrl_buf->error_buff_offset);
}

static int wait_vgpu_plugin_task_bootloaded(struct nvidia_vgpu *vgpu)
{
	struct nvidia_vgpu_rpc *rpc = &vgpu->rpc;
	VGPU_CPU_GSP_CTRL_BUFF_REGION *ctrl_buf = rpc->ctrl_buf;

	u64 timeout = 10 * 1000000; /* 10 s */

	do {
		if (readl(&ctrl_buf->message_seq_num) == GSP_PLUGIN_BOOTLOADED)
			break;

		usleep_range(1, 2);
	} while (--timeout);

	return timeout ? 0 : -ETIMEDOUT;
}

static int negotiate_rpc_version(struct nvidia_vgpu *vgpu)
{
	return nvidia_vgpu_rpc_call(vgpu, NV_VGPU_CPU_RPC_MSG_VERSION_NEGOTIATION,
				    NULL, 0);
}

/* Initial snapshot of config params */
static const unsigned char config_params[] = {
	0x24, 0xef, 0x8f, 0xf7, 0x3e, 0xd5, 0x11, 0xef, 0xae, 0x36, 0x97, 0x58,
	0xb1, 0xcb, 0x0c, 0x87, 0x04, 0xc1, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x14, 0x00, 0xd0, 0xc1, 0x65, 0x03, 0x00, 0x00, 0xa1, 0x0e, 0x00, 0x00,
	0xff, 0xff, 0xff, 0xff, 0x40, 0x00, 0x00, 0x00, 0x03, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x40, 0x00, 0x00, 0x04, 0x00, 0x00, 0x00,
	0x02, 0x00, 0x00, 0x00, 0x00, 0x10, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x01, 0x00, 0x01, 0x01, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00
};

static int send_config_params_and_init(struct nvidia_vgpu *vgpu)
{
	struct nvidia_vgpu_mgr *vgpu_mgr = vgpu->vgpu_mgr;
	struct nvidia_vgpu_info *info = &vgpu->info;
	NV_VGPU_CPU_RPC_DATA_COPY_CONFIG_PARAMS params = {0};

	memcpy(&params, config_params, sizeof(config_params));

	params.dbdf = vgpu->info.dbdf;
	params.vgpu_device_instance_id =
		nvidia_vgpu_mgr_get_gsp_client_handle(vgpu_mgr, &vgpu->gsp_client);
	params.vgpu_type = info->vgpu_type->vgpu_type;
	params.vm_pid = vgpu->info.vm_pid;
	params.swizz_id = 0;
	params.num_channels = vgpu->chid.num_chid;
	params.num_plugin_channels = vgpu->chid.num_plugin_channels;

	return nvidia_vgpu_rpc_call(vgpu, NV_VGPU_CPU_RPC_MSG_SETUP_CONFIG_PARAMS_AND_INIT,
				    &params, sizeof(params));
}

/**
 * nvidia_vgpu_setup_rpc - setup the vGPU host RPC channel and send runtime
 * configuration.
 * @vgpu: the vGPU instance.
 *
 * Returns: zero on success, others on failure.
 */
int nvidia_vgpu_setup_rpc(struct nvidia_vgpu *vgpu)
{
	struct nvidia_vgpu_rpc *rpc = &vgpu->rpc;
	int ret;

	mutex_init(&rpc->lock);

	init_rpc_buf_pointers(vgpu);
	init_ctrl_buf_offsets(vgpu);

	ret = wait_vgpu_plugin_task_bootloaded(vgpu);
	if (ret) {
		vgpu_error(vgpu, "host_rpc: waiting bootload timeout\n");
		return ret;
	}

	vgpu_debug(vgpu, "bootloaded\n");

	ret = negotiate_rpc_version(vgpu);
	if (ret) {
		vgpu_error(vgpu, "host_rpc: fail to negotiate rpc version\n");
		return ret;
	}

	ret = send_config_params_and_init(vgpu);
	if (ret) {
		vgpu_error(vgpu, "host_rpc: fail to init vgpu plugin task\n");
		return ret;
	}

	vgpu_debug(vgpu, "active\n");

	return 0;
}
