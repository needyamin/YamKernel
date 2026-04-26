#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

/*
 * Format: 4 bytes width, 4 bytes height, then W*H*4 bytes of BGRA data
 *
 * Usage:
 *   img2raw <input_image> <output_bin>                  (original size)
 *   img2raw <input_image> <output_bin> <max_width>      (downscale if wider)
 */

/* Nearest-neighbor downscale */
static unsigned char *downscale(const unsigned char *src, int sw, int sh,
                                int dw, int dh) {
    unsigned char *dst = (unsigned char *)malloc((size_t)dw * dh * 4);
    if (!dst) return NULL;
    for (int y = 0; y < dh; y++) {
        int sy = (y * sh) / dh;
        for (int x = 0; x < dw; x++) {
            int sx = (x * sw) / dw;
            const unsigned char *sp = src + ((size_t)sy * sw + sx) * 4;
            unsigned char       *dp = dst + ((size_t)y  * dw + x)  * 4;
            dp[0] = sp[0]; dp[1] = sp[1]; dp[2] = sp[2]; dp[3] = sp[3];
        }
    }
    return dst;
}

int main(int argc, char **argv) {
    if (argc < 3) {
        fprintf(stderr, "Usage: %s <input_image> <output_bin> [max_width]\n", argv[0]);
        return 1;
    }

    int max_width = 0;
    if (argc >= 4) {
        max_width = atoi(argv[3]);
    }

    int width, height, channels;
    unsigned char *img = stbi_load(argv[1], &width, &height, &channels, 4);
    if (!img) {
        fprintf(stderr, "Failed to load image %s: %s\n", argv[1], stbi_failure_reason());
        return 1;
    }

    int out_w = width, out_h = height;
    unsigned char *pixels = img;
    unsigned char *scaled = NULL;

    /* Downscale if image exceeds max_width */
    if (max_width > 0 && width > max_width) {
        out_w = max_width;
        out_h = (height * max_width) / width; /* keep aspect ratio */
        fprintf(stderr, "Downscaling %dx%d -> %dx%d\n", width, height, out_w, out_h);
        scaled = downscale(img, width, height, out_w, out_h);
        if (!scaled) {
            fprintf(stderr, "Failed to allocate memory for downscale\n");
            stbi_image_free(img);
            return 1;
        }
        pixels = scaled;
    }

    FILE *out = fopen(argv[2], "wb");
    if (!out) {
        fprintf(stderr, "Failed to open output file %s\n", argv[2]);
        stbi_image_free(img);
        free(scaled);
        return 1;
    }

    uint32_t w = out_w;
    uint32_t h = out_h;

    /* Write header */
    fwrite(&w, sizeof(uint32_t), 1, out);
    fwrite(&h, sizeof(uint32_t), 1, out);

    /* Convert RGBA to BGRA (framebuffer pixel order) */
    size_t num_pixels = (size_t)out_w * out_h;
    for (size_t i = 0; i < num_pixels; i++) {
        unsigned char r = pixels[i * 4 + 0];
        unsigned char g = pixels[i * 4 + 1];
        unsigned char b = pixels[i * 4 + 2];
        unsigned char a = pixels[i * 4 + 3];

        pixels[i * 4 + 0] = b;
        pixels[i * 4 + 1] = g;
        pixels[i * 4 + 2] = r;
        pixels[i * 4 + 3] = a;
    }

    fwrite(pixels, 4, num_pixels, out);

    fclose(out);
    stbi_image_free(img);
    free(scaled);

    printf("Successfully converted %s to %s (%dx%d)\n", argv[1], argv[2], out_w, out_h);
    return 0;
}
