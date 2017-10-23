#ifndef _IMG_LOAD_H
#define _IMG_LOAD_H

#include <stdbool.h>
#include <cairo/cairo.h>

bool cairo_image_surface_from_file(const char* path, cairo_surface_t** surface_out);

#endif
