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
#include <tmmintrin.h>

#include <stdio.h>

#define ALIGN16 __attribute__((aligned(16)))
#define KERNEL_SIZE 15 
#define HALF_KERNEL KERNEL_SIZE / 2

// number of xmm registers needed to store 
// input pixels for given kernel size
#define REGISTERS_CNT (KERNEL_SIZE + 4/2) / 4

// scaling factor for kernel coefficients.
// higher values cause desaturation.
// used in SSSE3 implementation.
#define SCALE_FACTOR 7

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
    uint32_t* o_src = src;
    for (int row = 0; row < height; row++) {
        for (int column = 0; column < width; column++, src++) {
            __m128i rgbaIn[REGISTERS_CNT];

            // handle borders
            int leftBorder = column < HALF_KERNEL;
            int rightBorder = column > width - HALF_KERNEL;
            if (leftBorder || rightBorder) {
                uint32_t _rgbaIn[KERNEL_SIZE + 1] ALIGN16;
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
                } else {
                    for (; i < width - column; i++)
                        _rgbaIn[i] = *(src + i);
                    for (int k = 0; i < KERNEL_SIZE; i++, k++)
                        _rgbaIn[i] = *(src - k);
                }

                for (int k = 0; k < REGISTERS_CNT; k++)
                    rgbaIn[k] = _mm_load_si128((__m128i*)(_rgbaIn + 4*k));
            } else {
                for (int k = 0; k < REGISTERS_CNT; k++) {
#if 0
                    printf("%p -> %p (%ld) || %p->%p\n", 
                        o_src,
                        o_src + (height * width),
                        o_src + (height * width) - src,
                        src + 4*k - HALF_KERNEL, 
                        ((__m128i*)src + 4*k - HALF_KERNEL) + 1
                    );
#endif
                    // if this copy would go out of bounds, break
                    if ((long long) (((__m128i*) src + 4*k - HALF_KERNEL) + 1) 
                            > (long long) (o_src + (height * width)))
                        break;
                    rgbaIn[k] = _mm_loadu_si128((__m128i*)(src + 4*k - HALF_KERNEL));
                }
            }

            // unpack each pixel, convert to float,
            // multiply by corresponding kernel value
            // and add to accumulator
            __m128i tmp;
            __m128i zero = _mm_setzero_si128();
            __m128 rgba_ps;
            __m128 acc = _mm_setzero_ps();
            int counter = 0;

            for (int i = 0; i < 3; i++)
            {
                tmp = _mm_unpacklo_epi8(rgbaIn[i], zero);
                rgba_ps = _mm_cvtepi32_ps(_mm_unpacklo_epi16(tmp, zero));
                acc = _mm_add_ps(acc, _mm_mul_ps(rgba_ps, _mm_set1_ps(kernel[counter++])));
                rgba_ps = _mm_cvtepi32_ps(_mm_unpackhi_epi16(tmp, zero));
                acc = _mm_add_ps(acc, _mm_mul_ps(rgba_ps, _mm_set1_ps(kernel[counter++])));

                tmp = _mm_unpackhi_epi8(rgbaIn[i], zero);
                rgba_ps = _mm_cvtepi32_ps(_mm_unpacklo_epi16(tmp, zero));
                acc = _mm_add_ps(acc, _mm_mul_ps(rgba_ps, _mm_set1_ps(kernel[counter++])));
                rgba_ps = _mm_cvtepi32_ps(_mm_unpackhi_epi16(tmp, zero));
                acc = _mm_add_ps(acc, _mm_mul_ps(rgba_ps, _mm_set1_ps(kernel[counter++])));
            }

            tmp = _mm_unpacklo_epi8(rgbaIn[3], zero);
            rgba_ps = _mm_cvtepi32_ps(_mm_unpacklo_epi16(tmp, zero));
            acc = _mm_add_ps(acc, _mm_mul_ps(rgba_ps, _mm_set1_ps(kernel[counter++])));
            rgba_ps = _mm_cvtepi32_ps(_mm_unpackhi_epi16(tmp, zero));
            acc = _mm_add_ps(acc, _mm_mul_ps(rgba_ps, _mm_set1_ps(kernel[counter++])));

            tmp = _mm_unpackhi_epi8(rgbaIn[3], zero);
            rgba_ps = _mm_cvtepi32_ps(_mm_unpacklo_epi16(tmp, zero));
            acc = _mm_add_ps(acc, _mm_mul_ps(rgba_ps, _mm_set1_ps(kernel[counter++])));

            __m128i rgbaOut = _mm_cvtps_epi32(acc);
            rgbaOut = _mm_packs_epi32(rgbaOut, zero);
            rgbaOut = _mm_packus_epi16(rgbaOut, zero);
            *(dst + height * column + row) = _mm_cvtsi128_si32(rgbaOut);
        }
    }
}

void blur_impl_ssse3(uint32_t *src, uint32_t *dst, int width, int height, float sigma) {
    // prepare kernel
    float kernelf[KERNEL_SIZE];
    int8_t kernel[KERNEL_SIZE + 1];
    float coeff = 1.0 / sqrtf(2 * M_PI * sigma * sigma), sum = 0;

    for (int i = 0; i < KERNEL_SIZE; i++) {
        float x = HALF_KERNEL - i;
        kernelf[i] = coeff * expf(-x * x / (2.0 * sigma * sigma));
        sum += kernelf[i];
    }

    // normalize kernel
    for (int i = 0; i < KERNEL_SIZE; i++)
        kernelf[i] /= sum;

    // round to nearest integer and convert to int
    for (int i = 0; i < KERNEL_SIZE; i++)
        kernel[i] = (int8_t)rintf(kernelf[i] * (1 << SCALE_FACTOR));
    kernel[KERNEL_SIZE] = 0;

    // horizontal pass includes image transposition:
    // instead of writing pixel src[x] to dst[x],
    // we write it to transposed location.
    // (to be exact: dst[height * current_column + current_row])
    blur_impl_horizontal_pass_ssse3(src, dst, kernel, width, height);
    blur_impl_horizontal_pass_ssse3(dst, src, kernel, height, width);
}


void blur_impl_horizontal_pass_ssse3(uint32_t *src, uint32_t *dst, int8_t *kernel, int width, int height) {
    uint32_t* o_src = src;
    __m128i _kern = _mm_loadu_si128((__m128i*)kernel);
    __m128i rgbaIn[REGISTERS_CNT];

    for (int row = 0; row < height; row++) {
        for (int column = 0; column < width; column++, src++) {
            uint32_t _rgbaIn[KERNEL_SIZE + 1] ALIGN16;
#if 0
            for (int j = 0; j < KERNEL_SIZE; ++j) {
                printf("_rgbaIn[%d]: %p->%p\n", j, &_rgbaIn[j], &_rgbaIn[j] + 1);
            }
#endif
            // handle borders
            int leftBorder = column < HALF_KERNEL;
            int rightBorder = column > width - HALF_KERNEL;
            if (leftBorder || rightBorder) {
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
                } else {
                    for (; i < width - column; i++)
                        _rgbaIn[i] = *(src + i);
                    for (int k = 0; i < KERNEL_SIZE; i++, k++)
                        _rgbaIn[i] = *(src - k);
                }

                for (int k = 0; k < REGISTERS_CNT; k++) {
#if 0
                    printf("K: %d; p: %p, p+4*k: %p, end of p: %p\n", k, _rgbaIn, _rgbaIn+4*k, ((__m128i*) (_rgbaIn +4*k)) + 1);
#endif
                    rgbaIn[k] = _mm_load_si128((__m128i*)(_rgbaIn + 4*k));
                }
            } else {
                for (int k = 0; k < REGISTERS_CNT; k++) {
                    if ((long long) (((__m128i*) src + 4*k - HALF_KERNEL) + 1) 
                            > (long long) (o_src + (height * width)))
                        break;
#if 0
                    printf("K: %d; p: %p -> %p\n", k, src+4*k - HALF_KERNEL, ((__m128i*) (src +4*k - HALF_KERNEL)) + 1);
                    printf("%p->%p, %p->%p (%ld)\n", (__m128i*) src + 4*k - HALF_KERNEL, ((__m128i*) src + 4*k - HALF_KERNEL) + 1, o_src, o_src + (width * height), o_src + (width * height) - src);
#endif
                    rgbaIn[k] = _mm_loadu_si128((__m128i*)(src + 4*k - HALF_KERNEL));
                }
            }

            // basis of this implementation is _mm_maddubs_epi16 (aka pmaddubsw).
            // 'rgba' holds 16 unsigned bytes, so 4 pixels.
            // 'kern' holds 16 signed bytes kernel values multiplied by (1 << SCALE_FACTOR).
            // before multiplication takes place, vectors need to be prepared:
            // 'rgba' is shuffled from R1B1G1A1...R4B4G4A4 to R1R2R3R4...A1A2A3A4
            // 'kern' is shuffled from w1w2w3w4...w13w14w15w16 to w1w2w3w4 repeated 4 times
            // then we call _mm_maddubs_epi16 and we get:
            // --------------------------------------------------------------------------------------
            // | R1*w1 + R2*w2 | R3*w3 + R4*w4 | G1*w1 + G2*w2 | G3*w3 + G4*w4 | repeat for B and A |
            // --------------------------------------------------------------------------------------
            // each 'rectangle' is a 16-byte signed int.
            // then we repeat the process for the rest of input pixels,
            // call _mm_hadds_epi16 to add adjacent ints and shift right to scale by SCALE_FACTOR.

            __m128i rgba, kern;
            __m128i zero = _mm_setzero_si128();
            __m128i acc = _mm_setzero_si128();

            const __m128i rgba_shuf_mask = _mm_setr_epi8(0, 4, 8,  12,
                                                         1, 5, 9,  13,
                                                         2, 6, 10, 14,
                                                         3, 7, 11, 15);

            const __m128i kern_shuf_mask = _mm_setr_epi8(0, 1, 2, 3,
                                                         0, 1, 2, 3,
                                                         0, 1, 2, 3,
                                                         0, 1, 2, 3);

            rgba = _mm_shuffle_epi8(rgbaIn[0], rgba_shuf_mask);
            kern = _mm_shuffle_epi8(_kern, kern_shuf_mask);
            acc = _mm_adds_epi16(acc, _mm_maddubs_epi16(rgba, kern));

            rgba = _mm_shuffle_epi8(rgbaIn[1], rgba_shuf_mask);
            kern = _mm_shuffle_epi8(_mm_srli_si128(_kern, 4), kern_shuf_mask);
            acc = _mm_adds_epi16(acc, _mm_maddubs_epi16(rgba, kern));

            rgba = _mm_shuffle_epi8(rgbaIn[2], rgba_shuf_mask);
            kern = _mm_shuffle_epi8(_mm_srli_si128(_kern, 8), kern_shuf_mask);
            acc = _mm_adds_epi16(acc, _mm_maddubs_epi16(rgba, kern));

            rgba = _mm_shuffle_epi8(rgbaIn[3], rgba_shuf_mask);
            kern = _mm_shuffle_epi8(_mm_srli_si128(_kern, 12), kern_shuf_mask);
            acc = _mm_adds_epi16(acc, _mm_maddubs_epi16(rgba, kern));

            acc = _mm_hadds_epi16(acc, zero);
            acc = _mm_srai_epi16(acc, SCALE_FACTOR);

            // Cairo sets alpha channel to 255
            // (or -1, depending how you look at it)
            // this quickly overflows accumulator,
            // and alpha is calculated completely wrong.
            // I assume most people don't use semi-transparent
            // lock screen images, so no one will mind if we
            // 'correct it' by setting alpha to 255.
            *(dst + height * column + row) =
                _mm_cvtsi128_si32(_mm_packus_epi16(acc, zero)) | 0xFF000000;
        }
    }
}
