/*
 * Copyright (c) 2024, NVIDIA CORPORATION. All rights reserved.
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
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
 */
#include "ufifo.h"
#include "chid.h"
#include "priv.h"
#include "runl.h"

void
nvkm_ufifo_ctor(struct nvkm_fifo *fifo, struct nvif_device_impl_fifo *impl)
{
	struct nvkm_runl *runl;
	struct nvkm_engn *engn;
	int runi = 0;

	nvkm_runl_foreach(runl, fifo) {
		bool failed_engines = false;
		int engi = 0;

		nvkm_runl_foreach_engn(engn, runl) {
			struct nvkm_engine *engine = engn->engine;
			enum nvif_engine_type type;
			int i;

			switch (engine->subdev.type) {
			case NVKM_ENGINE_SW    : type = NVIF_ENGINE_SW; break;
			case NVKM_ENGINE_GR    : type = NVIF_ENGINE_GR; break;
			case NVKM_ENGINE_MPEG  : type = NVIF_ENGINE_MPEG; break;
			case NVKM_ENGINE_ME    : type = NVIF_ENGINE_ME; break;
			case NVKM_ENGINE_CIPHER: type = NVIF_ENGINE_CIPHER; break;
			case NVKM_ENGINE_BSP   : type = NVIF_ENGINE_BSP; break;
			case NVKM_ENGINE_VP    : type = NVIF_ENGINE_VP; break;
			case NVKM_ENGINE_CE    : type = NVIF_ENGINE_CE; break;
			case NVKM_ENGINE_SEC   : type = NVIF_ENGINE_SEC; break;
			case NVKM_ENGINE_MSVLD : type = NVIF_ENGINE_MSVLD; break;
			case NVKM_ENGINE_MSPDEC: type = NVIF_ENGINE_MSPDEC; break;
			case NVKM_ENGINE_MSPPP : type = NVIF_ENGINE_MSPPP; break;
			case NVKM_ENGINE_MSENC : type = NVIF_ENGINE_MSENC; break;
			case NVKM_ENGINE_VIC   : type = NVIF_ENGINE_VIC; break;
			case NVKM_ENGINE_SEC2  : type = NVIF_ENGINE_SEC2; break;
			case NVKM_ENGINE_NVDEC : type = NVIF_ENGINE_NVDEC; break;
			case NVKM_ENGINE_NVENC : type = NVIF_ENGINE_NVENC; break;
			case NVKM_ENGINE_NVJPG : type = NVIF_ENGINE_NVJPG; break;
			case NVKM_ENGINE_OFA   : type = NVIF_ENGINE_OFA; break;
			default:
				failed_engines = true;
				WARN_ON(1);
				continue;
			}

			for (i = 0; i < impl->engine_nr; i++) {
				if (impl->engine[i].type == type)
					break;
			}

			if (i == impl->engine_nr) {
				int clsi = 0;

				if (WARN_ON(i >= ARRAY_SIZE(impl->engine))) {
					failed_engines = true;
					break;
				}

				impl->engine[i].type = type;
				impl->engine_nr++;

				for (;;) {
					struct nvkm_oclass oclass = { .engine = engine };

					if (engine->func->fifo.sclass)
						engine->func->fifo.sclass(&oclass, clsi);
					else
						oclass.base = engine->func->sclass[clsi];
					if (!oclass.base.oclass)
						break;

					if (WARN_ON(clsi >= ARRAY_SIZE(impl->engine[i].oclass)))
						break;

					impl->engine[i].oclass[clsi++] = oclass.base.oclass;
				}

				impl->engine[i].oclass_nr = clsi;
			}

			engi = impl->runl[runi].engn_nr;

			if (WARN_ON(engi >= ARRAY_SIZE(impl->runl[runi].engn))) {
				failed_engines = true;
				break;
			}

			impl->runl[runi].engn[engi].engine = i;
			impl->runl[runi].engn[engi].inst = engine->subdev.inst;
			impl->runl[runi].engn_nr = ++engi;
		}

		if (failed_engines ||
		    WARN_ON(runi >= ARRAY_SIZE(impl->runl)))
			continue;

		impl->runl[runi].id = runl->id;
		impl->runl[runi].chan_nr = runl->chid->nr;
		impl->runl[runi].runq_nr = runl->runq_nr;
		impl->runl[runi].engn_nr = engi;
		impl->runl_nr = ++runi;
	}

	impl->cgrp.oclass = fifo->func->cgrp.user.oclass;
	impl->chan.oclass = fifo->func->chan.user.oclass;
}
