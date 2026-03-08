#include "image.h"

#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void set_error(char *err, size_t err_len, const char *msg)
{
	if (!err || err_len == 0)
		return;
	snprintf(err, err_len, "%s", msg);
}

static inline void store_pixel(uint8_t *dst, uint8_t r, uint8_t g, uint8_t b, uint8_t a)
{
#if __BYTE_ORDER__ == __ORDER_BIG_ENDIAN__
	dst[0] = a;
	dst[1] = r;
	dst[2] = g;
	dst[3] = b;
#else /* little endian */
	dst[0] = b;
	dst[1] = g;
	dst[2] = r;
	dst[3] = a;
#endif
}

enum image_error_code image_load(const char *path, struct image *out, char *err, size_t err_len)
{
	int width = 0;
	int height = 0;
	int channels = 0;
	uint8_t *rgba = NULL;
	uint8_t *pixels = NULL;

	memset(out, 0, sizeof *out);

	rgba = stbi_load(path, &width, &height, &channels, 4);
	if (!rgba) {
		const char *reason = stbi_failure_reason();
		set_error(err, err_len, reason ? reason : "failed to decode image");
		return IMAGE_ERR_DECODE;
	}

	pixels = malloc((size_t)width * (size_t)height * 4);
	if (!pixels) {
		stbi_image_free(rgba);
		set_error(err, err_len, "out of memory");
		return IMAGE_ERR_ALLOC;
	}

	/* fill output */
	out->width = width;
	out->height = height;
	out->has_alpha = (channels == 2 || channels == 4);
	out->pixels = pixels;

	/* convert to ARGB8888 or XRGB8888 */
	for (int y = 0; y < height; ++y) {
		/* RGBA from stbi */
		const uint8_t *src = rgba + (size_t)y * (size_t)width * 4;
		uint8_t *dst = pixels + (size_t)y * (size_t)width * 4;

		/* store in native endianess */
		for (int x = 0; x < width; ++x) {
			uint8_t r = src[x * 4 + 0];
			uint8_t g = src[x * 4 + 1];
			uint8_t b = src[x * 4 + 2];
			uint8_t a = src[x * 4 + 3];

			store_pixel(dst + x * 4, r, g, b, a);
			if (a != 0xFF) /* if not fully opaque */
				out->has_alpha = true;
		}
	}

	stbi_image_free(rgba);
	return IMAGE_OK;
}

void image_free(struct image *image)
{
	if (!image)
		return;

	free(image->pixels);
	image->pixels = NULL;
}

void image_force_opaque(struct image *image)
{
	int x;
	int y;
	uint8_t *pixels;

	if (!image || !image->pixels)
		return;

	/* set alpha to 0xFF */
	pixels = image->pixels;
	for (y = 0; y < image->height; ++y) {
		uint8_t *row = pixels + (size_t)y * (size_t)image->width * 4;

		for (x = 0; x < image->width; ++x)
			row[x * 4 + 3] = 0xFF;
	}

	image->has_alpha = false;
}
