#ifndef OPENXR_H
#define OPENXR_H

#include "../candle/ecs/ecm.h"
#include "../candle/utils/renderer.h"

typedef struct c_openxr
{
	c_t super;
	struct openxr_internal *internal;
	renderer_t *renderer;
} c_openxr_t;

DEF_CASTER(ct_openxr, c_openxr, c_openxr_t)

c_openxr_t *c_openxr_new();

#endif /* !OPENXR_H */
