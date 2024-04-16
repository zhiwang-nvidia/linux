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
#include <nvif/outp.h>
#include <nvif/disp.h>
#include <nvif/printf.h>

#include <nvif/class.h>

int
nvif_outp_dp_mst_vcpi(struct nvif_outp *outp, int head,
		      u8 start_slot, u8 num_slots, u16 pbn, u16 aligned_pbn)
{
	int ret;

	ret = outp->impl->dp.mst_vcpi(outp->priv, head, start_slot, num_slots, pbn, aligned_pbn);
	NVIF_ERRON(ret, &outp->object,
		   "[DP_MST_VCPI head:%d start_slot:%02x num_slots:%02x pbn:%04x aligned_pbn:%04x]",
		   head, start_slot, num_slots, pbn, aligned_pbn);
	return ret;
}

int
nvif_outp_dp_mst_id_put(struct nvif_outp *outp, u32 id)
{
	int ret;

	ret = outp->impl->dp.mst_id_put(outp->priv, id);
	NVIF_ERRON(ret, &outp->object, "[DP_MST_ID_PUT id:%08x]", id);
	return ret;
}

int
nvif_outp_dp_mst_id_get(struct nvif_outp *outp, u32 *id)
{
	int ret;

	ret = outp->impl->dp.mst_id_get(outp->priv, id);
	NVIF_ERRON(ret, &outp->object, "[DP_MST_ID_GET] id:%08x", *id);
	return ret;
}

int
nvif_outp_dp_sst(struct nvif_outp *outp, int head, u32 watermark, u32 hblanksym, u32 vblanksym)
{
	int ret;

	ret = outp->impl->dp.sst(outp->priv, head, watermark, hblanksym, vblanksym);
	NVIF_ERRON(ret, &outp->object,
		   "[DP_SST head:%d watermark:%d hblanksym:%d vblanksym:%d]",
		   head, watermark, hblanksym, vblanksym);
	return ret;
}

int
nvif_outp_dp_drive(struct nvif_outp *outp, u8 lanes, u8 pe[4], u8 vs[4])
{
	int ret;

	ret = outp->impl->dp.drive(outp->priv, lanes, pe, vs);
	NVIF_ERRON(ret, &outp->object, "[DP_DRIVE lanes:%d]", lanes);
	return ret;
}

int
nvif_outp_dp_train(struct nvif_outp *outp, u8 dpcd[DP_RECEIVER_CAP_SIZE], u8 lttprs,
		   u8 link_nr, u32 link_bw, bool mst, bool post_lt_adj, bool retrain)
{
	int ret;

	ret = outp->impl->dp.train(outp->priv, dpcd, lttprs, link_nr, link_bw, mst,
				   post_lt_adj, retrain);
	NVIF_ERRON(ret, &outp->object,
		   "[DP_TRAIN retrain:%d mst:%d lttprs:%d post_lt_adj:%d nr:%d bw:%d]",
		   retrain, mst, lttprs, post_lt_adj, link_nr, link_bw);
	return ret;
}

int
nvif_outp_dp_rates(struct nvif_outp *outp, struct nvif_outp_dp_rate *rate, int rates)
{
	int ret;

	ret = outp->impl->dp.rates(outp->priv, rate, rates);
	NVIF_ERRON(ret, &outp->object, "[DP_RATES rates:%d]", rates);
	return ret;
}

int
nvif_outp_dp_aux_xfer(struct nvif_outp *outp, u8 type, u8 *psize, u32 addr, u8 *data)
{
	u8 size = *psize;
	int ret;

	ret = outp->impl->dp.aux_xfer(outp->priv, type, addr, data, &size);
	NVIF_DEBUG(&outp->object, "[DP_AUX_XFER type:%d size:%d addr:%05x] %d size:%d (ret: %d)",
		   type, *psize, addr, ret, size, ret);
	if (ret < 0)
		return ret;

	*psize = size;
	return ret;
}

int
nvif_outp_dp_aux_pwr(struct nvif_outp *outp, bool enable)
{
	int ret;

	ret = outp->impl->dp.aux_pwr(outp->priv, enable);
	NVIF_ERRON(ret, &outp->object, "[DP_AUX_PWR state:%d]", enable);
	return ret;
}

int
nvif_outp_hda_eld(struct nvif_outp *outp, int head, void *data, u32 size)
{
	int ret;

	ret = outp->impl->hda.eld(outp->priv, head, data, size);
	NVIF_ERRON(ret, &outp->object, "[HDA_ELD head:%d size:%d]", head, size);
	return ret;
}

int
nvif_outp_infoframe(struct nvif_outp *outp, int head, enum nvif_outp_infoframe_type type,
		    u8 *data, u8 size)
{
	int ret;

	ret = outp->impl->hdmi.infoframe(outp->priv, head, type, data, size);
	NVIF_ERRON(ret, &outp->object, "[INFOFRAME type:%d size:%d]", type, size);
	return ret;
}

int
nvif_outp_hdmi(struct nvif_outp *outp, int head, bool enable, u8 max_ac_packet, u8 rekey,
	       u32 khz, bool scdc, bool scdc_scrambling, bool scdc_low_rates)
{
	int ret;

	ret = outp->impl->hdmi.config(outp->priv, head, enable, max_ac_packet, rekey, khz,
				      scdc, scdc_scrambling, scdc_low_rates);
	NVIF_ERRON(ret, &outp->object,
		   "[HDMI head:%d enable:%d max_ac_packet:%d rekey:%d khz:%d scdc:%d "
		   "scdc_scrambling:%d scdc_low_rates:%d]",
		   head, enable, max_ac_packet, rekey, khz,
		   scdc, scdc_scrambling, scdc_low_rates);
	return ret;
}

int
nvif_outp_lvds(struct nvif_outp *outp, bool dual, bool bpc8)
{
	int ret;

	ret = outp->impl->lvds.config(outp->priv, dual, bpc8);
	NVIF_ERRON(ret, &outp->object, "[LVDS dual:%d 8bpc:%d]", dual, bpc8);
	return ret;
}

int
nvif_outp_bl_set(struct nvif_outp *outp, int level)
{
	int ret;

	ret = outp->impl->bl.set(outp->priv, level);
	NVIF_ERRON(ret, &outp->object, "[BL_SET level:%d]", level);
	return ret;
}

int
nvif_outp_bl_get(struct nvif_outp *outp)
{
	u8 level;
	int ret;

	ret = outp->impl->bl.get(outp->priv, &level);
	NVIF_ERRON(ret, &outp->object, "[BL_GET level:%d]", level);
	return ret ? ret : level;
}

void
nvif_outp_release(struct nvif_outp *outp)
{
	int ret = outp->impl->release(outp->priv);
	NVIF_ERRON(ret, &outp->object, "[RELEASE]");
	outp->or.id = -1;
}

static inline int
nvif_outp_acquire(struct nvif_outp *outp, enum nvif_outp_type type, bool hda)
{
	u8 or, link;
	int ret;

	ret = outp->impl->acquire(outp->priv, type, hda, &or, &link);
	if (ret)
		return ret;

	outp->or.id = or;
	outp->or.link = link;
	return 0;
}

int
nvif_outp_acquire_pior(struct nvif_outp *outp)
{
	int ret;

	ret = nvif_outp_acquire(outp, NVIF_OUTP_PIOR, false);
	NVIF_ERRON(ret, &outp->object, "[ACQUIRE PIOR] or:%d", outp->or.id);
	return ret;
}

int
nvif_outp_acquire_sor(struct nvif_outp *outp, bool hda)
{
	int ret;

	ret = nvif_outp_acquire(outp, NVIF_OUTP_SOR, hda);
	NVIF_ERRON(ret, &outp->object, "[ACQUIRE SOR] or:%d link:%d", outp->or.id, outp->or.link);
	return ret;
}

int
nvif_outp_acquire_dac(struct nvif_outp *outp)
{
	int ret;

	ret = nvif_outp_acquire(outp, NVIF_OUTP_DAC, false);
	NVIF_ERRON(ret, &outp->object, "[ACQUIRE DAC] or:%d", outp->or.id);
	return ret;
}

static int
nvif_outp_inherit(struct nvif_outp *outp, enum nvif_outp_proto proto, u8 *head, u8 *proto_evo)
{
	u8 or, link;
	int ret;

	ret = outp->impl->inherit(outp->priv, proto, &or, &link, head, proto_evo);
	if (ret)
		return ret;

	outp->or.id = or;
	outp->or.link = link;
	return 0;
}

int
nvif_outp_inherit_lvds(struct nvif_outp *outp, u8 *proto_out)
{
	u8 head;
	int ret;

	ret = nvif_outp_inherit(outp, NVIF_OUTP_LVDS, &head, proto_out);
	NVIF_ERRON(ret && ret != -ENODEV, &outp->object, "[INHERIT proto:LVDS] ret:%d", ret);
	return ret ?: head;
}

int
nvif_outp_inherit_tmds(struct nvif_outp *outp, u8 *proto_out)
{
	u8 head;
	int ret;

	ret = nvif_outp_inherit(outp, NVIF_OUTP_TMDS, &head, proto_out);
	NVIF_ERRON(ret && ret != -ENODEV, &outp->object, "[INHERIT proto:TMDS] ret:%d", ret);
	return ret ?: head;
}

int
nvif_outp_inherit_dp(struct nvif_outp *outp, u8 *proto_out)
{
	u8 head;
	int ret;

	ret = nvif_outp_inherit(outp, NVIF_OUTP_DP, &head, proto_out);
	NVIF_ERRON(ret && ret != -ENODEV, &outp->object, "[INHERIT proto:DP] ret:%d", ret);

	// TODO: Get current link info

	return ret ?: head;
}

int
nvif_outp_inherit_rgb_crt(struct nvif_outp *outp, u8 *proto_out)
{
	u8 head;
	int ret;

	ret = nvif_outp_inherit(outp, NVIF_OUTP_RGB_CRT, &head, proto_out);
	NVIF_ERRON(ret && ret != -ENODEV, &outp->object, "[INHERIT proto:RGB_CRT] ret:%d", ret);
	return ret ?: head;
}

int
nvif_outp_load_detect(struct nvif_outp *outp, u32 loadval)
{
	u8 load;
	int ret;

	ret = outp->impl->load_detect(outp->priv, loadval, &load);
	NVIF_ERRON(ret, &outp->object, "[LOAD_DETECT data:%08x] load:%02x", loadval, load);
	return ret < 0 ? ret : load;
}

int
nvif_outp_edid_get(struct nvif_outp *outp, u8 **pedid)
{
	u16 size = 2048;
	u8 *data;
	int ret;

	data = kmalloc(size, GFP_KERNEL);
	if (!data)
		return -ENOMEM;

	ret = outp->impl->edid_get(outp->priv, data, &size);
	NVIF_ERRON(ret, &outp->object, "[EDID_GET] size:%d", size);
	if (ret)
		goto done;

	*pedid = kmemdup(data, size, GFP_KERNEL);
	if (!*pedid) {
		ret = -ENOMEM;
		goto done;
	}

	ret = size;
done:
	kfree(data);
	return ret;
}

enum nvif_outp_detect_status
nvif_outp_detect(struct nvif_outp *outp)
{
	enum nvif_outp_detect_status status;
	int ret;

	ret = outp->impl->detect(outp->priv, &status);
	NVIF_ERRON(ret, &outp->object, "[DETECT] status:%02x", status);
	if (ret)
		return NVIF_OUTP_DETECT_UNKNOWN;

	return status;
}

void
nvif_outp_dtor(struct nvif_outp *outp)
{
	if (!outp->impl)
		return;

	outp->impl->del(outp->priv);
	outp->impl = NULL;
}

int
nvif_outp_ctor(struct nvif_disp *disp, const char *name, int id, struct nvif_outp *outp)
{
	int ret;

	ret = disp->impl->outp.new(disp->priv, id, &outp->impl, &outp->priv);
	NVIF_ERRON(ret, &disp->object, "[NEW outp id:%d]", id);
	if (ret)
		return ret;

	nvif_object_ctor(&disp->object, name ?: "nvifOutp", id, 0, &outp->object);
	outp->or.id = -1;
	return 0;
}
