// SPDX-License-Identifier: GPL-2.0
/*
 * Kexec image loader

 * Copyright (C) 2018 Linaro Limited
 * Author: AKASHI Takahiro <takahiro.akashi@linaro.org>
 */

#define pr_fmt(fmt)	"kexec_file(Image): " fmt

#include <linux/err.h>
#include <linux/errno.h>
#include <linux/kernel.h>
#include <linux/kexec.h>
#include <linux/string.h>
#include <asm/byteorder.h>
#include <asm/cpufeature.h>
#include <asm/image.h>
#include <asm/memory.h>

static int image_probe(const char *kernel_buf, unsigned long kernel_len)
{
	const struct arm64_image_header *h;

	h = (const struct arm64_image_header *)(kernel_buf);

	if (!h || (kernel_len < sizeof(*h)) ||
			memcmp(&h->magic, ARM64_IMAGE_MAGIC,
				sizeof(h->magic)))
		return -EINVAL;

	return 0;
}

static void *image_load(struct kimage *image,
				char *kernel, unsigned long kernel_len,
				char *initrd, unsigned long initrd_len,
				char *cmdline, unsigned long cmdline_len)
{
	struct arm64_image_header *h;
	u64 flags, value;
	bool be_image, be_kernel;
	struct kexec_buf kbuf;
	unsigned long text_offset;
	struct kexec_segment *kernel_segment;
	int ret;

	/*
	 * We require a kernel with an unambiguous Image header. Per
	 * Documentation/booting.txt, this is the case when image_size
	 * is non-zero (practically speaking, since v3.17).
	 */
	h = (struct arm64_image_header *)kernel;
	if (!h->image_size)
		return ERR_PTR(-EINVAL);

	/* Check cpu features */
	flags = le64_to_cpu(h->flags);
	be_image = arm64_image_flag_field(flags, ARM64_IMAGE_FLAG_BE);
	be_kernel = IS_ENABLED(CONFIG_CPU_BIG_ENDIAN);

	value = arm64_image_flag_field(flags, ARM64_IMAGE_FLAG_PAGE_SIZE);

	/* Load the kernel */
	kbuf.image = image;
	kbuf.buf_min = 0;
	kbuf.buf_max = ULONG_MAX;
	kbuf.top_down = false;

	kbuf.buffer = kernel;
	kbuf.bufsz = kernel_len;
	kbuf.mem = 0;
	kbuf.memsz = le64_to_cpu(h->image_size);
	text_offset = le64_to_cpu(h->text_offset);
	kbuf.buf_align = MIN_KIMG_ALIGN;

	/* Adjust kernel segment with TEXT_OFFSET */
	kbuf.memsz += text_offset;

	ret = kexec_add_buffer(&kbuf);
	if (ret)
		return ERR_PTR(ret);

	kernel_segment = &image->segment[image->nr_segments - 1];
	kernel_segment->mem += text_offset;
	kernel_segment->memsz -= text_offset;
	image->start = kernel_segment->mem;

	pr_debug("Loaded kernel at 0x%lx bufsz=0x%lx memsz=0x%lx\n",
				kernel_segment->mem, kbuf.bufsz,
				kernel_segment->memsz);

	/* Load additional data */
	ret = load_other_segments(image,
				kernel_segment->mem, kernel_segment->memsz,
				initrd, initrd_len, cmdline);

	return ERR_PTR(ret);
}

const struct kexec_file_ops kexec_image_ops = {
	.probe = image_probe,
	.load = image_load,
};
