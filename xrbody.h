#ifndef XRBODY_H
#define XRBODY_H

#include "../candle/ecs/ecm.h"
#include "../candle/utils/renderer.h"

typedef struct c_xrbody
{
	c_t super;
	char path[64];
	struct xrbody_internal *internal;
} c_xrbody_t;

DEF_CASTER(ct_xrbody, c_xrbody, c_xrbody_t)

c_xrbody_t *c_xrbody_new(const char *path);

#endif /* !XRBODY_H */
