#include <kernel/cls.h>
#include <kernel/mm.h>
#include <kernel/arch.h>
#include "stdint.h"

static cls_t *cls;

void init_cls(uint8_t n)
{
	cls = (cls_t *)page_alloc_s(n * sizeof(cls_t));

	for (int i = 0; i < n; i++)
	{
		cls_t *ccls = (cls_t *)(cls + i);
		ccls->id = (uint64_t)i;
	}
}

cls_t *get_cls(void)
{
	uint32_t id = cpu_id();
	return (cls_t *)(cls + id);
}