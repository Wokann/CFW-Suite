#pragma once

#include "types.h"

// level
//   0 = store
//   1 = compress
//   2 = compress + optimise
struct _ARGS_VPK_COMPRESS {
	char filelog[256];
	u8 level;
	u8 method;
	u32 lz_move_max;
};

struct _ARGS_VPK_DECOMPRESS {
	char filelog[256];
};

typedef struct _ARGS_VPK_COMPRESS ARGS_VPK_COMPRESS;
typedef struct _ARGS_VPK_DECOMPRESS ARGS_VPK_DECOMPRESS;

u32 decompress_part345(u8 *dst, u8 *src);
u32 compress_part345(u8 *dst, u8 *src, u32 size);
int decrypt_decompress_part12(u8* src, u8** dest);
int decompress_part12 (u8* src, u8* *dest);