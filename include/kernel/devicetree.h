#ifndef _DEVICETREE_H
#define _DEVICETREE_H

#include <kernel/stdint.h>
#include <kernel/endian.h>

#define FDT_MAGIC BIG_ENDIAN_UINT32(0xd00dfeed)

#define FDT_BEGIN_NODE BIG_ENDIAN_UINT32(0x00000001)
#define FDT_END_NODE BIG_ENDIAN_UINT32(0x00000002)
#define FDT_PROP BIG_ENDIAN_UINT32(0x00000003)
#define FDT_NOP BIG_ENDIAN_UINT32(0x00000004)
#define FDT_END BIG_ENDIAN_UINT32(0x00000009)

#define FDT_DEVICE_TYPE_PROP ("device_type")

struct fdt_header_t
{
	uint32_t magic;
	uint32_t totalsize;
	uint32_t off_dt_struct;
	uint32_t off_dt_strings;
	uint32_t off_mem_rsvmap;
	uint32_t version;
	uint32_t last_comp_version;
	uint32_t boot_cpuid_phys;
	uint32_t size_dt_strings;
	uint32_t size_dt_struct;
};

struct fdt_reserve_entry_t
{
	uint64_t address;
	uint64_t size;
};

struct fdt_prop_t
{
	uint32_t len;
	uint32_t nameoff;
};

// Dump the device tree to terminal
void dumpdevicetree();

// Remap the device tree source to the given offset in memory
void remaped_devicetreeoffset(uintptr_t offset);

// Count the number of matching device types
uint32_t devicetree_count_dev_type(char *);

// Get the first node of a device with a matching type
void *devicetree_first_with_device_type(char *type);

void *devicetree_first_with_property(char *prop);

void *devicetree_find_node(char *path);

void *devicetree_get_next_node(void *current);

void *devicetree_get_root_node();

char *devicetree_get_property(void *node, char *propkey);

uint32_t devicetree_get_property_len(void *node, char *propkey);

char *devicetree_get_node_name(void *node);

uintptr_t devicetree_get_bar(void *node);

uint64_t devicetree_get_bar_size(void *node);

char *devicetree_get_root_property(char *propkey);

#endif