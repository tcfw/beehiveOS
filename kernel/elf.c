#include <elf_arch.h>
#include <errno.h>
#include <kernel/elf.h>
#include <kernel/mm.h>
#include <kernel/tty.h>
#include <kernel/vm.h>
#include <kernel/strings.h>

int validate_elf(struct elf_hdr_64_t *hdr)
{
	if (is_elf_hdr(hdr) == 0)
		return 0;

	if (hdr->e_ident[ELF_EI_OFFSET_OSABI] != ELF_OSABI_BEEHIVE)
		return 0;

	if (hdr->e_machine != ARCH_ELF_MACHINE)
		return 0;

	return 1;
}

// load_elf assumes the position of the hdr includes the whole binary
int load_elf(vm_t *vm, struct elf_hdr_64_t *hdr)
{
	struct elf_phdr_64_t *phdr = (struct elf_phdr_64_t *)((uint8_t *)hdr + hdr->e_phoff);

	for (int ph = 0; ph < hdr->e_phnum; ph++, phdr++)
	{
		if (phdr->p_type != ELF_PT_LOAD || phdr->p_memsz == 0)
			continue;

		uint64_t flags = MEMORY_TYPE_USER;
		// VM does not currently support write only memory regions
		if ((phdr->p_flags & PF_X) == 0)
			flags |= MEMORY_NON_EXEC;

		if ((phdr->p_flags & PF_W) != 0)
			flags |= MEMORY_PERM_W;

		if ((phdr->p_flags & PF_R) != 0 && (phdr->p_flags & PF_W) == 0)
			flags |= MEMORY_PERM_RO;

		void *page = 0;
		uintptr_t paddr = vm_va_to_pa(vm_get_current_table(), (uintptr_t)((uint8_t *)hdr + phdr->p_offset));
		size_t region_size = (phdr->p_memsz & ~(PAGE_SIZE - 1)) + PAGE_SIZE - 1;

		if (phdr->p_memsz != phdr->p_filesz)
		{
			page = page_alloc_s(phdr->p_memsz);
			if (page == 0)
				return -ERRNOMEM;

			memcpy(page, (uint8_t *)hdr + phdr->p_offset, phdr->p_filesz);

			paddr = vm_va_to_pa(vm_get_current_table(), (uintptr_t)page);
		}

		int ret = vm_map_region(vm->vm_table, paddr, (uintptr_t)phdr->p_vaddr, region_size, flags);
		if (ret < 0)
			return ret;

		vm_mapping *map = kmalloc(sizeof(vm_mapping));
		if (!map)
			return ret;

		map->flags = VM_MAP_FLAG_PHY_KERNEL | flags;
		map->phy_addr = (uintptr_t)paddr;
		map->vm_addr = phdr->p_vaddr;
		map->length = phdr->p_memsz;
		map->page = page;
		list_add(&map->list, &vm->vm_maps);

		if (map->vm_addr + map->length > vm->brk)
			vm->start_brk = map->vm_addr + map->length;

		if ((phdr->p_flags & PF_X) > 0)
		{
			if (phdr->p_vaddr < vm->start_code || vm->start_code == 0)
				vm->start_code = phdr->p_vaddr;
			if (phdr->p_vaddr + phdr->p_memsz > vm->end_code)
				vm->end_code = phdr->p_vaddr + phdr->p_memsz;
		}
		else
		{
			if (phdr->p_vaddr < vm->start_data || vm->start_data == 0)
				vm->start_data = phdr->p_vaddr;
			if (phdr->p_vaddr + phdr->p_memsz > vm->end_data)
				vm->end_data = phdr->p_vaddr + phdr->p_memsz;
		}
	}

	vm->brk = vm->start_brk;

	return 0;
}