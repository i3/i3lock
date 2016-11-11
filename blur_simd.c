/*
 * vim:ts=4:sw=4:expandtab
 *
 * © 2016 Sebastian Frysztak 
 *
 * See LICENSE for licensing information
 *
 */

#include "blur.h"
#include <math.h>
#include <xmmintrin.h>

#define ALIGN16 __attribute__((aligned(16)))
#define KERNEL_SIZE 7 
#define HALF_KERNEL KERNEL_SIZE / 2

// number of xmm registers needed to store 
// input pixels for given kernel size
#define REGISTERS_CNT (KERNEL_SIZE + 4/2) / 4

void blur_impl_sse2(uint32_t *src, uint32_t *dst, int width, int height, float sigma) {
    // prepare kernel
    float kernel[KERNEL_SIZE];
    float coeff = 1.0 / sqrtf(2 * M_PI * sigma * sigma), sum = 0;

    for (int i = 0; i < KERNEL_SIZE; i++) {
        float x = HALF_KERNEL - i;
        kernel[i] = coeff * expf(-x * x / (2.0 * sigma * sigma));
        sum += kernel[i];
    }

    // normalize kernel
    for (int i = 0; i < KERNEL_SIZE; i++)
        kernel[i] /= sum;

    // horizontal pass includes image transposition:
    // instead of writing pixel src[x] to dst[x],
    // we write it to transposed location.
    // (to be exact: dst[height * current_column + current_row])
    blur_impl_horizontal_pass_sse2(src, dst, kernel, width, height);
    blur_impl_horizontal_pass_sse2(dst, src, kernel, height, width);
}

void blur_impl_horizontal_pass_sse2(uint32_t *src, uint32_t *dst, float *kernel, int width, int height) {
    for (int row = 0; row < height; row++) {
        for (int column = 0; column < width; column++, src++) {
            __m128i rgbaIn[REGISTERS_CNT];

            // handle borders
            int leftBorder = column < HALF_KERNEL;
            int rightBorder = column > width - HALF_KERNEL;
            uint32_t _rgbaIn[KERNEL_SIZE] ALIGN16;
            int i = 0;
            if (leftBorder) {
                // for kernel size 7x7 and column == 0, we have:
                // x x x P0 P1 P2 P3
                // first loop mirrors P{0..3} to fill x's,
                // second one loads P{0..3}
                for (; i < HALF_KERNEL - column; i++)
                    _rgbaIn[i] = *(src + (HALF_KERNEL - i));
                for (; i < KERNEL_SIZE; i++)
                    _rgbaIn[i] = *(src - (HALF_KERNEL - i));

                for (int k = 0; k < REGISTERS_CNT; k++)
                    rgbaIn[k] = _mm_load_si128((__m128i*)(_rgbaIn + 4*k));
            } else if (rightBorder) {
                for (; i < width - column; i++)
                    _rgbaIn[i] = *(src + i);
                for (int k = 0; i < KERNEL_SIZE; i++, k++)
                    _rgbaIn[i] = *(src - k);

                for (int k = 0; k < REGISTERS_CNT; k++)
                    rgbaIn[k] = _mm_load_si128((__m128i*)(_rgbaIn + 4*k));
            } else {
                for (int k = 0; k < REGISTERS_CNT; k++)
                    rgbaIn[k] = _mm_loadu_si128((__m128i*)(src + 4*k - HALF_KERNEL));
            }

            __m128i zero = _mm_setzero_si128();
            __m128i acc = _mm_setzero_si128();

            acc = _mm_add_epi16(acc, _mm_unpacklo_epi8(rgbaIn[0], zero));
            acc = _mm_add_epi16(acc, _mm_unpackhi_epi8(rgbaIn[0], zero));
            acc = _mm_add_epi16(acc, _mm_unpacklo_epi8(rgbaIn[1], zero));

            // kernel size equals to 7, but we can only load multiples of 4 pixels
            // we have to set 8th pixel to zero
            acc = _mm_add_epi16(acc, _mm_andnot_si128(_mm_set_epi32(0xFFFFFFFF, 0xFFFFFFFF, 0, 0), 
                                                      _mm_unpackhi_epi8(rgbaIn[1], zero)));
            acc = _mm_add_epi32(_mm_unpacklo_epi16(acc, zero), 
                                _mm_unpackhi_epi16(acc, zero));

            acc = _mm_cvtps_epi32(_mm_mul_ps(_mm_cvtepi32_ps(acc),
                                             _mm_set1_ps(1/((float)KERNEL_SIZE))));

            *(dst + height * column + row) = 
                _mm_cvtsi128_si32(_mm_packus_epi16(_mm_packs_epi32(acc, zero), zero));
        }
    }
}
