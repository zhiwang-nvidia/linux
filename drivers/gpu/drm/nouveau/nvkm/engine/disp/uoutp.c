/*
 * Copyright 2021 Red Hat Inc.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE COPYRIGHT HOLDER(S) OR AUTHOR(S) BE LIABLE FOR ANY CLAIM, DAMAGES OR
 * OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE,
 * ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
 * OTHER DEALINGS IN THE SOFTWARE.
 */
#include "uoutp.h"
#include "dp.h"
#include "head.h"
#include "ior.h"

#include <subdev/i2c.h>

#include <nvif/if0012.h>

struct nvif_outp_priv {
	struct nvkm_object object;
	struct nvkm_outp *outp;

	struct nvif_outp_impl impl;
};

static inline void
nvkm_uoutp_unlock(struct nvif_outp_priv *uoutp)
{
	mutex_unlock(&uoutp->outp->disp->super.mutex);
}

static inline void
nvkm_uoutp_lock(struct nvif_outp_priv *uoutp)
{
	mutex_lock(&uoutp->outp->disp->super.mutex);
}

static inline int
nvkm_uoutp_lock_acquired(struct nvif_outp_priv *uoutp)
{
	nvkm_uoutp_lock(uoutp);

	if (!uoutp->outp->ior) {
		nvkm_uoutp_unlock(uoutp);
		return -EIO;
	}

	return 0;
}

static int
nvkm_uoutp_mthd_dp_mst_vcpi(struct nvkm_outp *outp, void *argv, u32 argc)
{
	struct nvkm_ior *ior = outp->ior;
	union nvif_outp_dp_mst_vcpi_args *args = argv;

	if (argc != sizeof(args->v0) || args->v0.version != 0)
		return -ENOSYS;
	if (!ior->func->dp || !ior->func->dp->vcpi || !nvkm_head_find(outp->disp, args->v0.head))
		return -EINVAL;

	ior->func->dp->vcpi(ior, args->v0.head, args->v0.start_slot, args->v0.num_slots,
				 args->v0.pbn, args->v0.aligned_pbn);
	return 0;
}

static int
nvkm_uoutp_mthd_dp_mst_id_put(struct nvkm_outp *outp, void *argv, u32 argc)
{
	union nvif_outp_dp_mst_id_put_args *args = argv;

	if (argc != sizeof(args->v0) || args->v0.version != 0)
	        return -ENOSYS;
	if (!outp->func->dp.mst_id_put)
	        return -EINVAL;

	return outp->func->dp.mst_id_put(outp, args->v0.id);
}

static int
nvkm_uoutp_mthd_dp_mst_id_get(struct nvkm_outp *outp, void *argv, u32 argc)
{
	union nvif_outp_dp_mst_id_get_args *args = argv;

	if (argc != sizeof(args->v0) || args->v0.version != 0)
	        return -ENOSYS;
	if (!outp->func->dp.mst_id_get)
	        return -EINVAL;

	return outp->func->dp.mst_id_get(outp, &args->v0.id);
}

static int
nvkm_uoutp_dp_sst(struct nvif_outp_priv *uoutp, u8 head,
		  u32 watermark, u32 hblanksym, u32 vblanksym)
{
	struct nvkm_outp *outp = uoutp->outp;
	struct nvkm_disp *disp = outp->disp;
	struct nvkm_ior *ior;
	int ret;

	if (!nvkm_head_find(disp, head))
		return -EINVAL;

	ret = nvkm_uoutp_lock_acquired(uoutp);
	if (ret)
		return ret;

	ior = outp->ior;

	if (ior->func->dp->sst) {
		ret = ior->func->dp->sst(ior, head,
					 outp->dp.dpcd[DPCD_RC02] & DPCD_RC02_ENHANCED_FRAME_CAP,
					 watermark, hblanksym, vblanksym);
	}

	nvkm_uoutp_unlock(uoutp);
	return ret;
}

static int
nvkm_uoutp_dp_drive(struct nvif_outp_priv *uoutp, u8 lanes, u8 pe[4], u8 vs[4])
{
	struct nvkm_outp *outp = uoutp->outp;
	int ret;

	ret = nvkm_uoutp_lock_acquired(uoutp);
	if (ret)
		return ret;

	ret = outp->func->dp.drive(outp, lanes, pe, vs);
	nvkm_uoutp_unlock(uoutp);
	return ret;
}

static int
nvkm_uoutp_dp_train(struct nvif_outp_priv *uoutp, u8 dpcd[DP_RECEIVER_CAP_SIZE], u8 lttprs,
		    u8 link_nr, u32 link_bw, bool mst, bool post_lt_adj, bool retrain)
{
	struct nvkm_outp *outp = uoutp->outp;
	int ret;

	ret = nvkm_uoutp_lock_acquired(uoutp);
	if (ret)
		return ret;

	if (!retrain) {
		memcpy(outp->dp.dpcd, dpcd, sizeof(outp->dp.dpcd));
		outp->dp.lttprs = lttprs;
		outp->dp.lt.nr = link_nr;
		outp->dp.lt.bw = link_bw / 27000;
		outp->dp.lt.mst = mst;
		outp->dp.lt.post_adj = post_lt_adj;
	}

	ret = outp->func->dp.train(outp, retrain);
	nvkm_uoutp_unlock(uoutp);
	return ret;
}

static int
nvkm_uoutp_dp_rates(struct nvif_outp_priv *uoutp, struct nvif_outp_dp_rate *rate, u8 rates)
{
	struct nvkm_outp *outp = uoutp->outp;

	if (rates > ARRAY_SIZE(outp->dp.rate))
		return -EINVAL;

	nvkm_uoutp_lock(uoutp);

	for (int i = 0; i < rates; i++) {
		outp->dp.rate[i].dpcd = rate[i].dpcd;
		outp->dp.rate[i].rate = rate[i].rate;
	}

	outp->dp.rates = rates;

	if (outp->func->dp.rates)
		outp->func->dp.rates(outp);

	nvkm_uoutp_unlock(uoutp);
	return 0;
}

static int
nvkm_uoutp_dp_aux_xfer(struct nvif_outp_priv *uoutp, u8 type, u32 addr, u8 *data, u8 *size)
{
	struct nvkm_outp *outp = uoutp->outp;
	int ret;

	nvkm_uoutp_lock(uoutp);
	ret = outp->func->dp.aux_xfer(outp, type, addr, data, size);
	nvkm_uoutp_unlock(uoutp);
	return ret;
}

static int
nvkm_uoutp_dp_aux_pwr(struct nvif_outp_priv *uoutp, bool enable)
{
	struct nvkm_outp *outp = uoutp->outp;
	int ret;

	nvkm_uoutp_lock(uoutp);
	ret = outp->func->dp.aux_pwr(outp, enable);
	nvkm_uoutp_unlock(uoutp);
	return ret;
}

static int
nvkm_uoutp_hda_eld(struct nvif_outp_priv *uoutp, u8 head, u8 *data, u8 size)
{
	struct nvkm_outp *outp = uoutp->outp;
	struct nvkm_ior *ior;
	int ret;

	if (!nvkm_head_find(outp->disp, head))
		return -EINVAL;
	if (size > 0x60)
		return -E2BIG;

	ret = nvkm_uoutp_lock_acquired(uoutp);
	if (ret)
		return ret;

	ior = outp->ior;
	if (!ior->hda) {
		nvkm_uoutp_unlock(uoutp);
		return -EINVAL;
	}

	if (size && data[0]) {
		if (outp->info.type == DCB_OUTPUT_DP)
			ior->func->dp->audio(ior, head, true);
		else
		if (ior->func->hdmi->audio)
			ior->func->hdmi->audio(ior, head, true);

		ior->func->hda->hpd(ior, head, true);
		ior->func->hda->eld(ior, head, data, size);
	} else {
		ior->func->hda->hpd(ior, head, false);

		if (outp->info.type == DCB_OUTPUT_DP)
			ior->func->dp->audio(ior, head, false);
		else
		if (ior->func->hdmi->audio)
			ior->func->hdmi->audio(ior, head, false);
	}

	nvkm_uoutp_unlock(uoutp);
	return 0;
}

static int
nvkm_uoutp_infoframe(struct nvif_outp_priv *uoutp, u8 head,
		     enum nvif_outp_infoframe_type type, u8 *data, u8 size)
{
	struct nvkm_ior *ior;
	int ret;

	if (!nvkm_head_find(uoutp->outp->disp, head))
		return -EINVAL;

	ret = nvkm_uoutp_lock_acquired(uoutp);
	if (ret)
		return ret;

	ior = uoutp->outp->ior;

	switch (ior->func->hdmi ? type : 0xff) {
	case NVIF_OUTP_INFOFRAME_AVI:
		ior->func->hdmi->infoframe_avi(ior, head, data, size);
		break;
	case NVIF_OUTP_INFOFRAME_VSI:
		ior->func->hdmi->infoframe_vsi(ior, head, data, size);
		break;
	default:
		ret = -EINVAL;
		break;
	}

	nvkm_uoutp_unlock(uoutp);
	return ret;
}

static int
nvkm_uoutp_hdmi(struct nvif_outp_priv *uoutp, u8 head, bool enable, u8 max_ac_packet, u8 rekey,
	        u32 khz, bool scdc, bool scdc_scrambling, bool scdc_low_rates)
{
	struct nvkm_outp *outp = uoutp->outp;
	struct nvkm_ior *ior;
	int ret;

	ret = nvkm_uoutp_lock_acquired(uoutp);
	if (ret)
		return ret;

	if (!(outp->asy.head = nvkm_head_find(outp->disp, head))) {
		nvkm_uoutp_unlock(uoutp);
		return -EINVAL;
	}

	ior = outp->ior;

	if (!ior->func->hdmi ||
	    max_ac_packet > 0x1f ||
	    rekey > 0x7f ||
	    (scdc && !ior->func->hdmi->scdc)) {
		nvkm_uoutp_unlock(uoutp);
		return -EINVAL;
	}

	if (!enable) {
		ior->func->hdmi->infoframe_avi(ior, head, NULL, 0);
		ior->func->hdmi->infoframe_vsi(ior, head, NULL, 0);
		ior->func->hdmi->ctrl(ior, head, false, 0, 0);
		nvkm_uoutp_unlock(uoutp);
		return 0;
	}

	ior->func->hdmi->ctrl(ior, head, enable,
			      max_ac_packet, rekey);
	if (ior->func->hdmi->scdc)
		ior->func->hdmi->scdc(ior, khz, scdc, scdc_scrambling,
				      scdc_low_rates);

	nvkm_uoutp_unlock(uoutp);
	return 0;
}

static int
nvkm_uoutp_lvds(struct nvif_outp_priv *uoutp, bool dual, bool bpc8)
{
	struct nvkm_outp *outp = uoutp->outp;
	int ret;

	ret = nvkm_uoutp_lock_acquired(uoutp);
	if (ret)
		return ret;

	outp->lvds.dual = dual;
	outp->lvds.bpc8 = bpc8;
	nvkm_uoutp_unlock(uoutp);
	return 0;
}

static int
nvkm_uoutp_bl_set(struct nvif_outp_priv *uoutp, u8 level)
{
	struct nvkm_outp *outp = uoutp->outp;
	int ret;

	nvkm_uoutp_lock(uoutp);
	ret = outp->func->bl.set(outp, level);
	nvkm_uoutp_unlock(uoutp);
	return ret;
}

static int
nvkm_uoutp_bl_get(struct nvif_outp_priv *uoutp, u8 *level)
{
	struct nvkm_outp *outp = uoutp->outp;
	int ret;

	nvkm_uoutp_lock(uoutp);
	ret = outp->func->bl.get(outp);
	nvkm_uoutp_unlock(uoutp);
	if (ret >= 0) {
		*level = ret;
		ret = 0;
	}

	return ret;
}

static int
nvkm_uoutp_release(struct nvif_outp_priv *uoutp)
{
	struct nvkm_outp *outp = uoutp->outp;
	int ret;

	ret = nvkm_uoutp_lock_acquired(uoutp);
	if (ret)
		return ret;

	outp->func->release(outp);
	nvkm_uoutp_unlock(uoutp);
	return 0;
}

static int
nvkm_uoutp_acquire(struct nvif_outp_priv *uoutp, enum nvif_outp_type type, bool hda,
		   u8 *or, u8 *link)
{
	struct nvkm_outp *outp = uoutp->outp;
	int ret;

	nvkm_uoutp_lock(uoutp);
	if (outp->ior) {
		nvkm_uoutp_unlock(uoutp);
		return -EBUSY;
	}

	switch (type) {
	case NVIF_OUTP_DAC:
	case NVIF_OUTP_PIOR:
		ret = outp->func->acquire(outp, false);
		break;
	case NVIF_OUTP_SOR:
		ret = outp->func->acquire(outp, hda);
		break;
	default:
		ret = -EINVAL;
		break;
	}

	if (ret == 0) {
		*or = outp->ior->id;
		*link = outp->ior->asy.link;
	}

	nvkm_uoutp_unlock(uoutp);
	return ret;
}

static int
nvkm_uoutp_inherit(struct nvif_outp_priv *uoutp, enum nvif_outp_proto proto,
		   u8 *or, u8 *link, u8 *head, u8 *proto_evo)
{
	struct nvkm_outp *outp = uoutp->outp;
	struct nvkm_ior *ior;
	int ret = -ENODEV;

	nvkm_uoutp_lock(uoutp);

	/* Ensure an ior is hooked up to this outp already */
	ior = outp->func->inherit(outp);
	if (!ior || !ior->arm.head)
		goto done;

	/* With iors, there will be a separate output path for each type of connector - and all of
	 * them will appear to be hooked up. Figure out which one is actually the one we're using
	 * based on the protocol we were given over nvif
	 */
	switch (proto) {
	case NVIF_OUTP_TMDS:
		if (ior->arm.proto != TMDS)
			goto done;
		break;
	case NVIF_OUTP_DP:
		if (ior->arm.proto != DP)
			goto done;
		break;
	case NVIF_OUTP_LVDS:
		if (ior->arm.proto != LVDS)
			goto done;
		break;
	case NVIF_OUTP_RGB_CRT:
		if (ior->arm.proto != CRT)
			goto done;
		break;
	default:
		ret = -EINVAL;
		break;
	}

	/* Make sure that userspace hasn't already acquired this */
	if (outp->acquired) {
		OUTP_ERR(outp, "cannot inherit an already acquired (%02x) outp", outp->acquired);
		ret = -EBUSY;
		goto done;
	}

	/* Mark the outp acquired by userspace now that we've confirmed it's already active */
	OUTP_TRACE(outp, "inherit %02x |= %02x %p", outp->acquired, NVKM_OUTP_USER, ior);
	nvkm_outp_acquire_ior(outp, NVKM_OUTP_USER, ior);

	*or = ior->id;
	*link = ior->arm.link;
	*head = ffs(ior->arm.head) - 1;
	*proto_evo = ior->arm.proto_evo;

	ret = 0;
done:
	nvkm_uoutp_unlock(uoutp);
	return ret;
}

static int
nvkm_uoutp_load_detect(struct nvif_outp_priv *uoutp, u32 loadval, u8 *load)
{
	struct nvkm_outp *outp = uoutp->outp;
	int ret;

	nvkm_uoutp_lock(uoutp);

	ret = nvkm_outp_acquire_or(outp, NVKM_OUTP_PRIV, false);
	if (ret == 0) {
		if (outp->ior->func->sense) {
			ret = outp->ior->func->sense(outp->ior, loadval);
			*load = ret < 0 ? 0 : ret;
		} else {
			ret = -EINVAL;
		}
		nvkm_outp_release_or(outp, NVKM_OUTP_PRIV);
	}

	nvkm_uoutp_unlock(uoutp);
	return ret;
}

static int
nvkm_uoutp_edid_get(struct nvif_outp_priv *uoutp, u8 *data, u16 *size)
{
	struct nvkm_outp *outp = uoutp->outp;
	int ret;

	nvkm_uoutp_lock(uoutp);
	ret = outp->func->edid_get(outp, data, size);
	nvkm_uoutp_unlock(uoutp);
	return ret;
}

static int
nvkm_uoutp_detect(struct nvif_outp_priv *uoutp, enum nvif_outp_detect_status *status)
{
	struct nvkm_outp *outp = uoutp->outp;
	int ret;

	nvkm_uoutp_lock(uoutp);
	ret = outp->func->detect(outp);
	nvkm_uoutp_unlock(uoutp);

	switch (ret) {
	case 0: *status = NVIF_OUTP_DETECT_NOT_PRESENT; break;
	case 1: *status = NVIF_OUTP_DETECT_PRESENT; break;
	default:
		*status = NVIF_OUTP_DETECT_UNKNOWN;
		break;
	}

	return 0;
}

static int
nvkm_uoutp_mthd_acquired(struct nvkm_outp *outp, u32 mthd, void *argv, u32 argc)
{
	switch (mthd) {
	case NVIF_OUTP_V0_DP_MST_ID_GET: return nvkm_uoutp_mthd_dp_mst_id_get(outp, argv, argc);
	case NVIF_OUTP_V0_DP_MST_ID_PUT: return nvkm_uoutp_mthd_dp_mst_id_put(outp, argv, argc);
	case NVIF_OUTP_V0_DP_MST_VCPI  : return nvkm_uoutp_mthd_dp_mst_vcpi  (outp, argv, argc);
	default:
		break;
	}

	return -EINVAL;
}

static int
nvkm_uoutp_mthd(struct nvkm_object *object, u32 mthd, void *argv, u32 argc)
{
	struct nvkm_outp *outp = container_of(object, struct nvif_outp_priv, object)->outp;
	struct nvkm_disp *disp = outp->disp;
	int ret;

	mutex_lock(&disp->super.mutex);

	if (outp->ior)
		ret = nvkm_uoutp_mthd_acquired(outp, mthd, argv, argc);
	else
		ret = -EIO;

	mutex_unlock(&disp->super.mutex);
	return ret;
}

static void
nvkm_uoutp_del(struct nvif_outp_priv *uoutp)
{
	struct nvkm_object *object = &uoutp->object;

	nvkm_object_del(&object);
}

static const struct nvif_outp_impl
nvkm_uoutp_impl = {
	.del = nvkm_uoutp_del,
	.inherit = nvkm_uoutp_inherit,
	.acquire = nvkm_uoutp_acquire,
	.release = nvkm_uoutp_release,
};

static void *
nvkm_uoutp_dtor(struct nvkm_object *object)
{
	struct nvif_outp_priv *uoutp = container_of(object, typeof(*uoutp), object);
	struct nvkm_disp *disp = uoutp->outp->disp;

	spin_lock(&disp->user.lock);
	uoutp->outp->user = false;
	spin_unlock(&disp->user.lock);
	return uoutp;
}

static const struct nvkm_object_func
nvkm_uoutp = {
	.dtor = nvkm_uoutp_dtor,
	.mthd = nvkm_uoutp_mthd,
};

int
nvkm_uoutp_new(struct nvkm_disp *disp, u8 id, const struct nvif_outp_impl **pimpl,
	       struct nvif_outp_priv **ppriv, struct nvkm_object **pobject)
{
	struct nvkm_outp *outt, *outp = NULL;
	struct nvif_outp_priv *uoutp;

	list_for_each_entry(outt, &disp->outps, head) {
		if (outt->index == id) {
			outp = outt;
			break;
		}
	}

	if (!outp)
		return -EINVAL;

	uoutp = kzalloc(sizeof(*uoutp), GFP_KERNEL);
	if (!uoutp)
		return -ENOMEM;

	nvkm_object_ctor(&nvkm_uoutp, &(struct nvkm_oclass) {}, &uoutp->object);
	uoutp->outp = outp;
	uoutp->impl = nvkm_uoutp_impl;
	uoutp->impl.id = id;

	if (outp->func->detect)
		uoutp->impl.detect = nvkm_uoutp_detect;
	if (outp->func->edid_get)
		uoutp->impl.edid_get = nvkm_uoutp_edid_get;

	uoutp->impl.load_detect = nvkm_uoutp_load_detect;

	switch (outp->info.type) {
	case DCB_OUTPUT_ANALOG:
		uoutp->impl.type = NVIF_OUTP_DAC;
		uoutp->impl.proto = NVIF_OUTP_RGB_CRT;
		uoutp->impl.rgb_crt.freq_max = outp->info.crtconf.maxfreq;
		break;
	case DCB_OUTPUT_TMDS:
		if (!outp->info.location) {
			uoutp->impl.type = NVIF_OUTP_SOR;
			uoutp->impl.tmds.dual = (outp->info.tmdsconf.sor.link == 3);
		} else {
			uoutp->impl.type = NVIF_OUTP_PIOR;
			uoutp->impl.tmds.dual = 0;
		}
		uoutp->impl.proto = NVIF_OUTP_TMDS;
		uoutp->impl.hdmi.config = nvkm_uoutp_hdmi;
		uoutp->impl.hdmi.infoframe = nvkm_uoutp_infoframe;
		uoutp->impl.hda.eld = nvkm_uoutp_hda_eld;
		break;
	case DCB_OUTPUT_LVDS:
		uoutp->impl.type = NVIF_OUTP_SOR;
		uoutp->impl.proto = NVIF_OUTP_LVDS;
		uoutp->impl.lvds.acpi_edid = outp->info.lvdsconf.use_acpi_for_edid;
		uoutp->impl.lvds.config = nvkm_uoutp_lvds;
		break;
	case DCB_OUTPUT_DP:
		if (!outp->info.location) {
			uoutp->impl.type = NVIF_OUTP_SOR;
			uoutp->impl.dp.aux = outp->info.i2c_index;
		} else {
			uoutp->impl.type = NVIF_OUTP_PIOR;
			uoutp->impl.dp.aux = NVKM_I2C_AUX_EXT(outp->info.extdev);
		}
		uoutp->impl.proto = NVIF_OUTP_DP;
		uoutp->impl.hda.eld = nvkm_uoutp_hda_eld;
		uoutp->impl.dp.mst = outp->dp.mst;
		uoutp->impl.dp.increased_wm = outp->dp.increased_wm;
		uoutp->impl.dp.link_nr = outp->info.dpconf.link_nr;
		uoutp->impl.dp.link_bw = outp->info.dpconf.link_bw * 27000;
		uoutp->impl.dp.aux_pwr = nvkm_uoutp_dp_aux_pwr;
		uoutp->impl.dp.aux_xfer = nvkm_uoutp_dp_aux_xfer;
		uoutp->impl.dp.rates = nvkm_uoutp_dp_rates;
		uoutp->impl.dp.train = nvkm_uoutp_dp_train;
		uoutp->impl.dp.drive = nvkm_uoutp_dp_drive;
		uoutp->impl.dp.sst = nvkm_uoutp_dp_sst;
		break;
	default:
		WARN_ON(1);
		kfree(uoutp);
		return -EINVAL;
	}

	if (outp->info.location)
		uoutp->impl.ddc = NVKM_I2C_BUS_EXT(outp->info.extdev);
	else
		uoutp->impl.ddc = outp->info.i2c_index;
	uoutp->impl.heads = outp->info.heads;
	uoutp->impl.conn = outp->info.connector;

	if (outp->func->bl.get) {
		uoutp->impl.bl.get = nvkm_uoutp_bl_get;
		uoutp->impl.bl.set = nvkm_uoutp_bl_set;
	}

	spin_lock(&disp->user.lock);
	if (outp->user) {
		spin_unlock(&disp->user.lock);
		kfree(uoutp);
		return -EBUSY;
	}
	outp->user = true;
	spin_unlock(&disp->user.lock);

	*pimpl = &uoutp->impl;
	*ppriv = uoutp;
	*pobject = &uoutp->object;
	return 0;
}
