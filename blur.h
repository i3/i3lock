#ifndef _BLUR_H
#define _BLUR_H

#include <stdint.h>
#include <cairo.h>

void blur_image_surface (cairo_surface_t *surface, int radius);
void blur_impl_naive(uint32_t* src, uint32_t* dst, int width, int height, int src_stride, int dst_stride, int radius);

#endif

