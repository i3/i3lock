#ifndef _BLUR_H
#define _BLUR_H

#include <stdint.h>
#include <cairo.h>

void blur_image_surface (cairo_surface_t *surface, int radius);
void blur_impl_naive(uint32_t* src, uint32_t* dst, int width, int height, int src_stride, int dst_stride, int radius);

__attribute__((__target__(("no-avx"))))
void blur_impl_sse2(uint32_t* src, uint32_t* dst, int width, int height, float sigma);
__attribute__((__target__(("no-avx"))))
void blur_impl_horizontal_pass_sse2(uint32_t *src, uint32_t *dst, float *kernel, int width, int height);

void blur_impl_avx(uint32_t* src, uint32_t* dst, int width, int height, float sigma);
void blur_impl_horizontal_pass_avx(uint32_t *src, uint32_t *dst, float *kernel, int width, int height);

#endif

