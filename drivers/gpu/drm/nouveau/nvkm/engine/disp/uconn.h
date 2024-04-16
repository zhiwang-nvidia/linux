/* SPDX-License-Identifier: MIT */
#ifndef __NVKM_UCONN_H__
#define __NVKM_UCONN_H__
#include "conn.h"
#include <nvif/driverif.h>

int nvkm_uconn_new(struct nvkm_disp *, u8 id, const struct nvif_conn_impl **,
		   struct nvif_conn_priv **, struct nvkm_object **);
#endif
