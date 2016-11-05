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
#include <immintrin.h>

#define ALIGN16 __attribute__((aligned(16)))
#define KERNEL_SIZE 15 
#define HALF_KERNEL KERNEL_SIZE / 2

// number of xmm registers needed to store 
// input pixels for given kernel size
#define REGISTERS_CNT (KERNEL_SIZE + 4/2) / 4

// scaling factor for kernel coefficients.
// higher values cause desaturation.
// used in SSSE3 implementation.
#define SCALE_FACTOR 14

// AVX intrinsics missing in GCC
#define _mm256_set_m128i(v0, v1)  _mm256_insertf128_si256(_mm256_castsi128_si256(v1), (v0), 1)
#define _mm256_setr_m128i(v0, v1) _mm256_set_m128i((v1), (v0))
#define _mm256_set_m128(v0, v1)   _mm256_insertf128_ps(_mm256_castps128_ps256(v1), (v0), 1)
#define _mm256_setr_m128(v0, v1)  _mm256_set_m128((v1), (v0))

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

            __m128i tmp;
            __m128i zero = _mm_setzero_si128();
            __m128i acc = _mm_setzero_si128();

            for (int i = 0; i < 3; i++)
            {
                acc = _mm_add_epi16(acc, _mm_unpacklo_epi8(rgbaIn[i], zero));
                acc = _mm_add_epi16(acc, _mm_unpackhi_epi8(rgbaIn[i], zero));
            }

            acc = _mm_add_epi16(acc, _mm_unpacklo_epi8(rgbaIn[3], zero));

            tmp = _mm_unpackhi_epi8(rgbaIn[3], zero);
            // set 16th pixel to zeroes
            tmp = _mm_andnot_si128(_mm_set_epi16(0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF, 0,0,0,0), tmp);
            acc = _mm_add_epi16(acc, tmp);
            acc = _mm_add_epi32(_mm_unpacklo_epi16(acc, zero), _mm_unpackhi_epi16(acc, zero));

            acc = _mm_cvtps_epi32(_mm_mul_ps(_mm_cvtepi32_ps(acc),
                                             _mm_set1_ps(1/((float)(KERNEL_SIZE)))));

            acc = _mm_packs_epi32(acc, zero);
            acc = _mm_packus_epi16(acc, zero);
            *(dst + height * column + row) = _mm_cvtsi128_si32(acc);
        }
    }
}

void blur_impl_avx(uint32_t *src, uint32_t *dst, int width, int height, float sigma) {
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
    blur_impl_horizontal_pass_avx(src, dst, kernel, width, height);
    blur_impl_horizontal_pass_avx(dst, src, kernel, height, width);
}

void blur_impl_horizontal_pass_avx(uint32_t *src, uint32_t *dst, float *kernel, int width, int height) {
    __m256 kernels[HALF_KERNEL];
    for (int i = 0, k = 0; i < HALF_KERNEL; i++, k += 2)
        kernels[i] = _mm256_setr_m128(_mm_set1_ps(kernel[k]), _mm_set1_ps(kernel[k+1]));

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

            // unpack each pixel, convert to float,
            // multiply by corresponding kernel value
            // and add to accumulator
            __m128i tmp;
            __m128i zero = _mm_setzero_si128();
            __m128 rgba_ps_128;
            __m256 rgba_ps;
            __m256 acc = _mm256_setzero_ps();
            int counter = 0;

            for (int i = 0; i < 3; i++)
            {
                tmp = _mm_unpacklo_epi8(rgbaIn[i], zero);
                rgba_ps = _mm256_cvtepi32_ps(_mm256_setr_m128i(_mm_unpacklo_epi16(tmp, zero),
                                                               _mm_unpackhi_epi16(tmp, zero)));
                acc = _mm256_add_ps(acc, _mm256_mul_ps(rgba_ps, kernels[counter++]));

                tmp = _mm_unpackhi_epi8(rgbaIn[i], zero);
                rgba_ps = _mm256_cvtepi32_ps(_mm256_setr_m128i(_mm_unpacklo_epi16(tmp, zero),
                                                               _mm_unpackhi_epi16(tmp, zero)));
                acc = _mm256_add_ps(acc, _mm256_mul_ps(rgba_ps, kernels[counter++]));
            }

            tmp = _mm_unpacklo_epi8(rgbaIn[3], zero);
            rgba_ps = _mm256_cvtepi32_ps(_mm256_setr_m128i(_mm_unpacklo_epi16(tmp, zero),
                                                           _mm_unpackhi_epi16(tmp, zero)));
            acc = _mm256_add_ps(acc, _mm256_mul_ps(rgba_ps, kernels[counter]));

            tmp = _mm_unpackhi_epi8(rgbaIn[3], zero);
            rgba_ps_128 = _mm_cvtepi32_ps(_mm_unpacklo_epi16(tmp, zero));
            rgba_ps_128 = _mm_mul_ps(rgba_ps_128, _mm_set1_ps(kernel[KERNEL_SIZE-1]));
            rgba_ps_128 = _mm_add_ps(rgba_ps_128, _mm_add_ps(_mm256_extractf128_ps(acc, 0),
                                                             _mm256_extractf128_ps(acc, 1)));

            __m128i rgbaOut = _mm_packs_epi32(_mm_cvtps_epi32(rgba_ps_128), zero);
            rgbaOut = _mm_packus_epi16(rgbaOut, zero);
            *(dst + height * column + row) = _mm_cvtsi128_si32(rgbaOut);
        }
    }
}

void blur_impl_ssse3(uint32_t *src, uint32_t *dst, int width, int height, float sigma) {
    // prepare kernel
    float kernelf[KERNEL_SIZE];
    int16_t kernel[KERNEL_SIZE + 1];
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
        kernel[i] = (int16_t)lrintf(kernelf[i] * (1 << SCALE_FACTOR));
    kernel[KERNEL_SIZE] = 0;

    // horizontal pass includes image transposition:
    // instead of writing pixel src[x] to dst[x],
    // we write it to transposed location.
    // (to be exact: dst[height * current_column + current_row])
    blur_impl_horizontal_pass_ssse3(src, dst, kernel, width, height);
    blur_impl_horizontal_pass_ssse3(dst, src, kernel, height, width);
}


void blur_impl_horizontal_pass_ssse3(uint32_t *src, uint32_t *dst, int16_t *kernel, int width, int height) {
    __m128i _kern[2];
    _kern[0] = _mm_loadu_si128((__m128i*)kernel);
    _kern[1] = _mm_loadu_si128((__m128i*)(kernel + 8));
    __m128i rgbaIn[REGISTERS_CNT];

    for (int row = 0; row < height; row++) {
        for (int column = 0; column < width; column++, src++) {
            uint32_t _rgbaIn[KERNEL_SIZE] ALIGN16;
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

                for (int k = 0; k < REGISTERS_CNT; k++)
                    rgbaIn[k] = _mm_load_si128((__m128i*)(_rgbaIn + 4*k));
            } else {
                for (int k = 0; k < REGISTERS_CNT; k++)
                    rgbaIn[k] = _mm_loadu_si128((__m128i*)(src + 4*k - HALF_KERNEL));
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

            __m128i rgba, rg, ba, kern;
            __m128i zero = _mm_setzero_si128();
            __m128i acc_rg = _mm_setzero_si128();
            __m128i acc_ba = _mm_setzero_si128();

            const __m128i rgba_shuf_mask = _mm_setr_epi8(0, 4, 8,  12,
                                                         1, 5, 9,  13,
                                                         2, 6, 10, 14,
                                                         3, 7, 11, 15);

            const __m128i kern_shuf_mask = _mm_setr_epi8(0, 1, 2, 3,
                                                         4, 5, 6, 7,
                                                         0, 1, 2, 3,
                                                         4, 5, 6, 7);

            rgba = _mm_shuffle_epi8(rgbaIn[0], rgba_shuf_mask);
            rg = _mm_unpacklo_epi8(rgba, zero);
            ba = _mm_unpackhi_epi8(rgba, zero);
            kern = _mm_shuffle_epi8(_kern[0], kern_shuf_mask);
            acc_rg = _mm_add_epi32(acc_rg, _mm_madd_epi16(rg, kern));
            acc_ba = _mm_add_epi32(acc_ba, _mm_madd_epi16(ba, kern));

            rgba = _mm_shuffle_epi8(rgbaIn[1], rgba_shuf_mask);
            rg = _mm_unpacklo_epi8(rgba, zero);
            ba = _mm_unpackhi_epi8(rgba, zero);
            kern = _mm_shuffle_epi8(_mm_srli_si128(_kern[0], 8), kern_shuf_mask);
            acc_rg = _mm_add_epi32(acc_rg, _mm_madd_epi16(rg, kern));
            acc_ba = _mm_add_epi32(acc_ba, _mm_madd_epi16(ba, kern));
    
            rgba = _mm_shuffle_epi8(rgbaIn[2], rgba_shuf_mask);
            rg = _mm_unpacklo_epi8(rgba, zero);
            ba = _mm_unpackhi_epi8(rgba, zero);
            kern = _mm_shuffle_epi8(_kern[1], kern_shuf_mask);
            acc_rg = _mm_add_epi32(acc_rg, _mm_madd_epi16(rg, kern));
            acc_ba = _mm_add_epi32(acc_ba, _mm_madd_epi16(ba, kern));

            rgba = _mm_shuffle_epi8(rgbaIn[3], rgba_shuf_mask);
            rg = _mm_unpacklo_epi8(rgba, zero);
            ba = _mm_unpackhi_epi8(rgba, zero);
            kern = _mm_shuffle_epi8(_mm_srli_si128(_kern[1], 8), kern_shuf_mask);
            acc_rg = _mm_add_epi32(acc_rg, _mm_madd_epi16(rg, kern));
            acc_ba = _mm_add_epi32(acc_ba, _mm_madd_epi16(ba, kern));

            rgba = _mm_hadd_epi32(acc_rg, acc_ba);
            rgba = _mm_srai_epi32(rgba, SCALE_FACTOR);

            // Cairo sets alpha channel to 255
            // (or -1, depending how you look at it)
            // this quickly overflows accumulator,
            // and alpha is calculated completely wrong.
            // I assume most people don't use semi-transparent
            // lock screen images, so no one will mind if we
            // 'correct it' by setting alpha to 255.
            *(dst + height * column + row) =
                _mm_cvtsi128_si32(_mm_shuffle_epi8(rgba, rgba_shuf_mask));
        }
    }
}
