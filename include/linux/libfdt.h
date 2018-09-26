#ifndef _INCLUDE_LIBFDT_H_
#define _INCLUDE_LIBFDT_H_

#include <linux/libfdt_env.h>
#include "../../scripts/dtc/libfdt/fdt.h"
#include "../../scripts/dtc/libfdt/libfdt.h"

/**
 * fdt_setprop_reg - add/set a memory region property
 * @fdt: pointer to the device tree blob
 * @nodeoffset: offset of the node to add a property at
 * @name: name of property
 * @addr: physical start address
 * @size: size of region
 *
 * returns:
 *	0, on success
 *      -FDT_ERR_BADLAYOUT,
 *	-FDT_ERR_BADMAGIC,
 *	-FDT_ERR_BADNCELLS, if the node has a badly formatted or invalid
 *		#address-cells property
 *      -FDT_ERR_BADOFFSET, nodeoffset did not point to FDT_BEGIN_NODE tag
 *	-FDT_ERR_BADSTATE,
 *	-FDT_ERR_BADSTRUCTURE,
 *	-FDT_ERR_BADVERSION,
 *	-FDT_ERR_BADVALUE, addr or size doesn't fit to respective cells size
 *      -FDT_ERR_NOSPACE, there is insufficient free space in the blob to
 *              contain a new property
 *	-FDT_ERR_TRUNCATED, standard meanings
 */
int fdt_setprop_reg(void *fdt, int nodeoffset, const char *name,
					       u64 addr, u64 size);

#endif /* _INCLUDE_LIBFDT_H_ */
