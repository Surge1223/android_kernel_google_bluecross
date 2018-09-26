// SPDX-License-Identifier: GPL-2.0
#include <linux/libfdt_env.h>
#include <linux/types.h>
#include "../scripts/dtc/libfdt/fdt_addresses.c"

/*
 * helper functions for arm64 kexec
 * Those functions may be merged into libfdt in the future.
 */

/* This function assumes that cells is 1 or 2 */
static void cpu64_to_fdt_cells(void *buf, u64 val64, int cells)
{
	__be32 val32;

	while (cells) {
		val32 = cpu_to_fdt32(val64 >> (32 * (--cells)));
		memcpy(buf, &val32, sizeof(val32));
		buf += sizeof(val32);
	}
}

int fdt_setprop_reg(void *fdt, int nodeoffset, const char *name,
						u64 addr, u64 size)
{
	int addr_cells, size_cells;
	char buf[sizeof(__be32) * 2 * 2];
		/* assume dt_root_[addr|size]_cells <= 2 */
	void *prop;
	size_t buf_size;

	addr_cells = fdt_address_cells(fdt, 0);
	if (addr_cells < 0)
		return addr_cells;
	size_cells = fdt_size_cells(fdt, 0);
	if (size_cells < 0)
		return size_cells;

	/* if *_cells >= 2, cells can hold 64-bit values anyway */
	if ((addr_cells == 1) && ((addr > U32_MAX) ||
				  ((addr + size) > U32_MAX)))
		return -FDT_ERR_BADVALUE;

	if ((size_cells == 1) && (size > U32_MAX))
		return -FDT_ERR_BADVALUE;

	buf_size = (addr_cells + size_cells) * sizeof(u32);
	prop = buf;

	cpu64_to_fdt_cells(prop, addr, addr_cells);
	prop += addr_cells * sizeof(u32);

	cpu64_to_fdt_cells(prop, size, size_cells);

	return fdt_setprop(fdt, nodeoffset, name, buf, buf_size);
}
