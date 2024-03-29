// SPDX-License-Identifier: GPL-2.0
/*
 * kexec_file for arm64
 *
 * Copyright (C) 2018 Linaro Limited
 * Author: AKASHI Takahiro <takahiro.akashi@linaro.org>
 *
 * Most code is derived from arm64 port of kexec-tools
 */

#define pr_fmt(fmt) "kexec_file: " fmt

#include <linux/ioport.h>
#include <linux/kernel.h>
#include <linux/kexec.h>
#include <linux/libfdt.h>
#include <linux/memblock.h>
#include <linux/of_fdt.h>
#include <linux/random.h>
#include <linux/slab.h>
#include <linux/string.h>
#include <linux/types.h>
#include <linux/vmalloc.h>
#include <asm/byteorder.h>

/* relevant device tree properties */
#define FDT_PSTR_KEXEC_ELFHDR	"linux,elfcorehdr"
#define FDT_PSTR_MEM_RANGE	"linux,usable-memory-range"
#define FDT_PSTR_INITRD_STA	"linux,initrd-start"
#define FDT_PSTR_INITRD_END	"linux,initrd-end"
#define FDT_PSTR_BOOTARGS	"bootargs"
#define FDT_PSTR_KASLR_SEED	"kaslr-seed"

const struct kexec_file_ops * const kexec_file_loaders[] = {
	&kexec_image_ops,
	NULL
};

int arch_kimage_file_post_load_cleanup(struct kimage *image)
{
	vfree(image->arch.dtb);
	image->arch.dtb = NULL;

	vfree(image->arch.elf_headers);
	image->arch.elf_headers = NULL;
	image->arch.elf_headers_sz = 0;

	return kexec_image_post_load_cleanup_default(image);
}

/* crng needs to have been initialized for providing kaslr-seed */
static int random_ready;

static void random_ready_notified(struct random_ready_callback *unused)
{
	random_ready = 1;
}

static struct random_ready_callback random_ready_cb = {
	.func = random_ready_notified,
};

static __init int init_random_ready_cb(void)
{
	int ret;

	ret = add_random_ready_callback(&random_ready_cb);
	if (ret == -EALREADY)
		random_ready = 1;
	else if (ret)
		pr_warn("failed to add a callback for random_ready\n");

	return 0;
}
late_initcall(init_random_ready_cb)

static int setup_dtb(struct kimage *image,
		     unsigned long initrd_load_addr, unsigned long initrd_len,
		     char *cmdline, void *dtb)
{
	int nodeoffset;
	u64 value;
	int ret;

	nodeoffset = fdt_path_offset(dtb, "/chosen");
	if (nodeoffset < 0)
		return -EINVAL;

	if (image->type == KEXEC_TYPE_CRASH) {
		/* add linux,elfcorehdr */
		ret = fdt_setprop_reg(dtb, nodeoffset, FDT_PSTR_KEXEC_ELFHDR,
				image->arch.elf_headers_mem,
				image->arch.elf_headers_sz);
		if (ret)
			return (ret == -FDT_ERR_NOSPACE ? -ENOMEM : -EINVAL);

		/* add linux,usable-memory-range */
		ret = fdt_setprop_reg(dtb, nodeoffset, FDT_PSTR_MEM_RANGE,
				crashk_res.start,
				crashk_res.end - crashk_res.start + 1);
		if (ret)
			return (ret == -FDT_ERR_NOSPACE ? -ENOMEM : -EINVAL);
	}

	/* add bootargs */
	if (cmdline) {
		ret = fdt_setprop_string(dtb, nodeoffset, FDT_PSTR_BOOTARGS,
							cmdline);
		if (ret)
			return (ret == -FDT_ERR_NOSPACE ? -ENOMEM : -EINVAL);
	} else {
		ret = fdt_delprop(dtb, nodeoffset, FDT_PSTR_BOOTARGS);
		if (ret && (ret != -FDT_ERR_NOTFOUND))
			return -EINVAL;
	}

	/* add initrd-* */
	if (initrd_load_addr) {
		ret = fdt_setprop_u64(dtb, nodeoffset, FDT_PSTR_INITRD_STA,
							initrd_load_addr);
		if (ret)
			return (ret == -FDT_ERR_NOSPACE ? -ENOMEM : -EINVAL);

		ret = fdt_setprop_u64(dtb, nodeoffset, FDT_PSTR_INITRD_END,
						initrd_load_addr + initrd_len);
		if (ret)
			return (ret == -FDT_ERR_NOSPACE ? -ENOMEM : -EINVAL);
	} else {
		ret = fdt_delprop(dtb, nodeoffset, FDT_PSTR_INITRD_STA);
		if (ret && (ret != -FDT_ERR_NOTFOUND))
			return -EINVAL;

		ret = fdt_delprop(dtb, nodeoffset, FDT_PSTR_INITRD_END);
		if (ret && (ret != -FDT_ERR_NOTFOUND))
			return -EINVAL;
	}

	/* add kaslr-seed */
	ret = fdt_delprop(dtb, nodeoffset, FDT_PSTR_KASLR_SEED);
	if (ret && (ret != -FDT_ERR_NOTFOUND))
		return -EINVAL;

	if (random_ready) {
		get_random_bytes(&value, sizeof(value));
		ret = fdt_setprop_u64(dtb, nodeoffset, FDT_PSTR_KASLR_SEED,
							value);
		if (ret)
			return (ret == -FDT_ERR_NOSPACE ? -ENOMEM : -EINVAL);
	} else {
		pr_notice("kaslr-seed won't be fed\n");
	}

	return 0;
}

/*
 * More space needed so that we can add initrd, bootargs,
 * userable-memory-range, elfcorehdr and kaslr-seed.
 */
#define DTB_EXTRA_SPACE 0x1000

static int create_dtb(struct kimage *image,
		      unsigned long initrd_load_addr, unsigned long initrd_len,
		      char *cmdline, void **dtb)
{
	void *buf;
	size_t buf_size;
	int ret;

	buf_size = fdt_totalsize(initial_boot_params)
			+ strlen(cmdline) + DTB_EXTRA_SPACE;

	for (;;) {
		buf = vmalloc(buf_size);
		if (!buf)
			return -ENOMEM;

		/* duplicate a device tree blob */
		ret = fdt_open_into(initial_boot_params, buf, buf_size);
		if (ret)
			return -EINVAL;

		ret = setup_dtb(image, initrd_load_addr, initrd_len,
				cmdline, buf);
		if (ret) {
			vfree(buf);
			if (ret == -ENOMEM) {
				/* unlikely, but just in case */
				buf_size += DTB_EXTRA_SPACE;
				continue;
			} else {
				return ret;
			}
		}

		/* trim it */
		fdt_pack(buf);
		*dtb = buf;

		return 0;
	}
}

static int prepare_elf_headers(void **addr, unsigned long *sz)
{
	struct crash_mem *cmem;
	unsigned int nr_ranges;
	int ret;
	u64 i;
	phys_addr_t start, end;

	nr_ranges = 1; /* for exclusion of crashkernel region */
	for_each_mem_range(i, &memblock.memory, NULL, NUMA_NO_NODE,
					MEMBLOCK_NONE, &start, &end, NULL)
		nr_ranges++;

	cmem = kmalloc(sizeof(struct crash_mem) +
			sizeof(struct crash_mem_range) * nr_ranges, GFP_KERNEL);
	if (!cmem)
		return -ENOMEM;

	cmem->max_nr_ranges = nr_ranges;
	cmem->nr_ranges = 0;
	for_each_mem_range(i, &memblock.memory, NULL, NUMA_NO_NODE,
					MEMBLOCK_NONE, &start, &end, NULL) {
		cmem->ranges[cmem->nr_ranges].start = start;
		cmem->ranges[cmem->nr_ranges].end = end - 1;
		cmem->nr_ranges++;
	}

	/* Exclude crashkernel region */
	ret = crash_exclude_mem_range(cmem, crashk_res.start, crashk_res.end);

	if (!ret)
		ret =  crash_prepare_elf64_headers(cmem, true, addr, sz);

	kfree(cmem);
	return ret;
}

int load_other_segments(struct kimage *image,
			unsigned long kernel_load_addr,
			unsigned long kernel_size,
			char *initrd, unsigned long initrd_len,
			char *cmdline)
{
	struct kexec_buf kbuf;
	void *headers, *dtb = NULL;
	unsigned long headers_sz, initrd_load_addr = 0, dtb_len;
	int ret = 0;

	kbuf.image = image;
	/* not allocate anything below the kernel */
	kbuf.buf_min = kernel_load_addr + kernel_size;

	/* load elf core header */
	if (image->type == KEXEC_TYPE_CRASH) {
		ret = prepare_elf_headers(&headers, &headers_sz);
		if (ret) {
			pr_err("Preparing elf core header failed\n");
			goto out_err;
		}

		kbuf.buffer = headers;
		kbuf.bufsz = headers_sz;
		kbuf.mem = 0;
		kbuf.memsz = headers_sz;
		kbuf.buf_align = SZ_64K; /* largest supported page size */
		kbuf.buf_max = ULONG_MAX;
		kbuf.top_down = true;

		ret = kexec_add_buffer(&kbuf);
		if (ret) {
			vfree(headers);
			goto out_err;
		}
		image->arch.elf_headers = headers;
		image->arch.elf_headers_mem = kbuf.mem;
		image->arch.elf_headers_sz = headers_sz;

		pr_debug("Loaded elf core header at 0x%lx bufsz=0x%lx memsz=0x%lx\n",
			 image->arch.elf_headers_mem, headers_sz, headers_sz);
	}

	/* load initrd */
	if (initrd) {
		kbuf.buffer = initrd;
		kbuf.bufsz = initrd_len;
		kbuf.mem = 0;
		kbuf.memsz = initrd_len;
		kbuf.buf_align = 0;
		/* within 1GB-aligned window of up to 32GB in size */
		kbuf.buf_max = round_down(kernel_load_addr, SZ_1G)
						+ (unsigned long)SZ_1G * 32;
		kbuf.top_down = false;

		ret = kexec_add_buffer(&kbuf);
		if (ret)
			goto out_err;
		initrd_load_addr = kbuf.mem;

		pr_debug("Loaded initrd at 0x%lx bufsz=0x%lx memsz=0x%lx\n",
				initrd_load_addr, initrd_len, initrd_len);
	}

	/* load dtb */
	ret = create_dtb(image, initrd_load_addr, initrd_len, cmdline, &dtb);
	if (ret) {
		pr_err("Preparing for new dtb failed\n");
		goto out_err;
	}

	dtb_len = fdt_totalsize(dtb);
	kbuf.buffer = dtb;
	kbuf.bufsz = dtb_len;
	kbuf.mem = 0;
	kbuf.memsz = dtb_len;
	/* not across 2MB boundary */
	kbuf.buf_align = SZ_2M;
	kbuf.buf_max = ULONG_MAX;
	kbuf.top_down = true;

	ret = kexec_add_buffer(&kbuf);
	if (ret)
		goto out_err;
	image->arch.dtb = dtb;
	image->arch.dtb_mem = kbuf.mem;

	pr_debug("Loaded dtb at 0x%lx bufsz=0x%lx memsz=0x%lx\n",
			kbuf.mem, dtb_len, dtb_len);

	return 0;

out_err:
	vfree(dtb);
	return ret;
}
