/* SPDX-License-Identifier: MIT */
#ifndef __NVIF_OBJECT_H__
#define __NVIF_OBJECT_H__
#include <nvif/os.h>

struct nvif_object {
	struct nvif_parent *parent;
	struct nvif_client *client;
	const char *name;
	u32 handle;
	s32 oclass;
	void *priv;
	struct {
		void __iomem *ptr;
		u64 size;
	} map;
};

struct nvif_map {
	const struct nvif_mapinfo *impl;
	struct nvif_object *object;
	void __iomem *ptr;
};

static inline bool
nvif_object_constructed(struct nvif_object *object)
{
	return object->client != NULL;
}

int nvif_object_ctor_0(struct nvif_object *, const char *name, u32 handle,
		       s32 oclass, void *, u32, struct nvif_object *);
void nvif_object_ctor_1(struct nvif_object *parent, const char *name, u32 handle, s32 oclass,
			struct nvif_object *);

#define nvif_object_ctor_(A,B,C,D,E,F,G,IMPL,...) IMPL
#define nvif_object_ctor(A...) nvif_object_ctor_(A, nvif_object_ctor_0, \
						    nvif_object_ctor__, \
						    nvif_object_ctor_1)(A)

void nvif_object_dtor(struct nvif_object *);
int  nvif_object_ioctl(struct nvif_object *, void *, u32, void **);
int  nvif_object_mthd(struct nvif_object *, u32, void *, u32);
int nvif_object_map_cpu(struct nvif_object *, const struct nvif_mapinfo *, struct nvif_map *);
int nvif_object_unmap_cpu(struct nvif_map *);

#define nvif_handle(a) (unsigned long)(void *)(a)
#define nvif_object(a) (a)->object

#define nvif_rd(a,f,b,c) ({                                                    \
	u32 _data = f((u8 __iomem *)(a)->map.ptr + (c));                       \
	_data;                                                                 \
})
#define nvif_wr(a,f,b,c,d) ({                                                  \
	f((d), (u8 __iomem *)(a)->map.ptr + (c));                              \
})
#define nvif_rd08(a,b) ({ ((u8)nvif_rd((a), ioread8, 1, (b))); })
#define nvif_rd16(a,b) ({ ((u16)nvif_rd((a), ioread16_native, 2, (b))); })
#define nvif_rd32(a,b) ({ ((u32)nvif_rd((a), ioread32_native, 4, (b))); })
#define nvif_wr08(a,b,c) nvif_wr((a), iowrite8, 1, (b), (u8)(c))
#define nvif_wr16(a,b,c) nvif_wr((a), iowrite16_native, 2, (b), (u16)(c))
#define nvif_wr32(a,b,c) nvif_wr((a), iowrite32_native, 4, (b), (u32)(c))
#define nvif_mask(a,b,c,d) ({                                                  \
	typeof(a) __object = (a);                                              \
	u32 _addr = (b), _data = nvif_rd32(__object, _addr);                   \
	nvif_wr32(__object, _addr, (_data & ~(c)) | (d));                      \
	_data;                                                                 \
})

#define nvif_mthd(a,b,c,d) nvif_object_mthd((a), (b), (c), (d))

#define NVIF_RD32_(p,o,dr)   nvif_rd32((p), (o) + (dr))
#define NVIF_WR32_(p,o,dr,f) nvif_wr32((p), (o) + (dr), (f))
#define NVIF_RD32(p,A...) DRF_RD(NVIF_RD32_,                  (p), 0, ##A)
#define NVIF_RV32(p,A...) DRF_RV(NVIF_RD32_,                  (p), 0, ##A)
#define NVIF_TV32(p,A...) DRF_TV(NVIF_RD32_,                  (p), 0, ##A)
#define NVIF_TD32(p,A...) DRF_TD(NVIF_RD32_,                  (p), 0, ##A)
#define NVIF_WR32(p,A...) DRF_WR(            NVIF_WR32_,      (p), 0, ##A)
#define NVIF_WV32(p,A...) DRF_WV(            NVIF_WR32_,      (p), 0, ##A)
#define NVIF_WD32(p,A...) DRF_WD(            NVIF_WR32_,      (p), 0, ##A)
#define NVIF_MR32(p,A...) DRF_MR(NVIF_RD32_, NVIF_WR32_, u32, (p), 0, ##A)
#define NVIF_MV32(p,A...) DRF_MV(NVIF_RD32_, NVIF_WR32_, u32, (p), 0, ##A)
#define NVIF_MD32(p,A...) DRF_MD(NVIF_RD32_, NVIF_WR32_, u32, (p), 0, ##A)
#endif
