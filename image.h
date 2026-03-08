#ifndef WIV_IMAGE_H
#define WIV_IMAGE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

enum image_error_code {
	IMAGE_OK = 0,
	IMAGE_ERR_OPEN,
	IMAGE_ERR_FORMAT,
	IMAGE_ERR_DECODE,
	IMAGE_ERR_ALLOC
};

struct image {
	int width;
	int height;
	uint8_t *pixels; /* ARGB8888 in native endianness */
	bool has_alpha;
};

enum image_error_code image_load(const char *path, struct image *out, char *err,
                                 size_t err_len);

void image_free(struct image *image);

void image_force_opaque(struct image *image);

#endif

