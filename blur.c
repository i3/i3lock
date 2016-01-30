#include <cairo.h>
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>

#include "blur.h"

void _blur_bitmap(uint8_t bytes_per_pixel, uint8_t *source, uint8_t *result,
                  uint32_t width, uint32_t height, int ksize) {
  uint32_t *temp;
  int px;

  temp = malloc(sizeof(uint32_t) * bytes_per_pixel * width * height);

  for (int y = 0; y < height; ++y) {
    for (int x = 0; x < width; ++x) {
      px = bytes_per_pixel * (x + width * y);
      for (int i = 0; i < bytes_per_pixel; ++i) {
        temp[px + i] = source[px + i];
        if (x != 0)
          temp[px + i] += temp[px - bytes_per_pixel + i];
        if (y != 0)
          temp[px + i] += temp[px - bytes_per_pixel * width + i];
        if (x != 0 && y != 0)
          temp[px + i] -= temp[px - bytes_per_pixel * (width + 1) + i];
      }
    }
  }

  double filter = (double)1 / (4 * ksize * ksize);
  uint32_t t;

  for (int y = 0; y < height; ++y) {
    for (int x = 0; x < width; ++x) {
      int x_min, x_max, y_min, y_max;

      if ((x_min = x - ksize) < 0)
        x_min = 0;
      if ((x_max = x + ksize) > width - 1)
        x_max = width - 1;
      if ((y_min = y - ksize) < 0)
        y_min = 0;
      if ((y_max = y + ksize) > height - 1)
        y_max = height - 1;

      px = bytes_per_pixel * (x + width * y);

      for (int i = 0; i < bytes_per_pixel; ++i) {
        t = temp[bytes_per_pixel * (x_min + width * y_min) + i];
        t += temp[bytes_per_pixel * (x_max + width * y_max) + i];
        t -= temp[bytes_per_pixel * (x_max + width * y_min) + i];
        t -= temp[bytes_per_pixel * (x_min + width * y_max) + i];
        t *= filter;
        result[px + i] = t;
      }
    }
  }

  free(temp);
}

void blur_surface(cairo_surface_t *img, int kernel_size) {
  unsigned char *data, *temp;

  uint32_t width, height;
  uint8_t bytes_per_pixel;

  cairo_format_t pixel_format = cairo_image_surface_get_format(img);
  width = cairo_image_surface_get_width(img);
  height = cairo_image_surface_get_height(img);
  data = cairo_image_surface_get_data(img);

  switch (pixel_format) {
  case CAIRO_FORMAT_RGB24:
  case CAIRO_FORMAT_ARGB32:
    bytes_per_pixel = 4;
    break;
  default:
    // We don't handle other formats
    return;
  }

  temp = malloc(bytes_per_pixel * sizeof(uint8_t) * width * height);

  cairo_surface_flush(img);

  // 3 times would be enough, but doing it equal number of times so we
  // get result in right buffer
  _blur_bitmap(bytes_per_pixel, data, temp, width, height, kernel_size);
  _blur_bitmap(bytes_per_pixel, temp, data, width, height, kernel_size);
  _blur_bitmap(bytes_per_pixel, data, temp, width, height, kernel_size);
  _blur_bitmap(bytes_per_pixel, temp, data, width, height, kernel_size);

  cairo_surface_mark_dirty(img);

  free(temp);
}
