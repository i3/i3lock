/*
 * vim:ts=4:sw=4:expandtab
 *
 * © 2017 Rodrigo Toste Gomes
 *
 * See LICENSE for licensing information
 *
 */
#include <assert.h>
#include <unistd.h>
#include <stdint.h>
#include <errno.h>
#include <stdlib.h>
#include <stdio.h>
#include <jpeglib.h>
#include <setjmp.h>
#include "img_load.h"

#define CHECK(A) if (!(A)) return false

typedef enum
{
    PNG,
    JPEG,
    NONE,
    ERROR
} supported_img_t;
typedef int file_t;
#define CHECKE(A) if (!(A)) return ERROR;

#define CHECKEXIT(A) if(!(A)) goto exit

#define CHECK_FILE_SIZE(fd, size) do                                    \
{                                                                       \
    off_t file_size = lseek(fd, 0, SEEK_END);                           \
    CHECKE(file_size != -1);                                            \
    assert(file_size > 0);                                              \
    if ((size_t)file_size < size)                                       \
    {                                                                   \
        ret = NONE;                                                     \
        goto exit;                                                      \
    }                                                                   \
} while(false);

/*
 * This function checks if the file passed in is a jpeg image. Meant to be used as a helper to
 * `get_img_type`
 *
 * This function returns JPEG in case the image is a JPEG, otherwise returns NONE. If it returns
 * ERROR, then the errno will be set. In case of error the file seek position may be arbitrary.
 *
 */
#define MIN_JPEG_FILE_SIZE 4
#if __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__
#define JPEG_HEADER 0xD8FF
#define JPEG_FOOTER 0xD9FF
#else
#define JPEG_HEADER 0xFFD8
#define JPEG_FOOTER 0xFFD9
#endif
typedef uint16_t jpeg_header_t;
typedef uint16_t jpeg_footer_t;
static supported_img_t is_jpeg_img(const file_t img_file)
{
    supported_img_t ret = NONE;

    off_t initial_offset = lseek(img_file, 0, SEEK_CUR);
    /* `lseek` returns -1 in case of error, and sets errno */
    CHECKE(initial_offset != -1);

    /*
     * Make sure file is large enough to contain a jpeg - need at least 4 bytes for header +
     * footer
     */
    CHECK_FILE_SIZE(img_file, sizeof(jpeg_header_t) + sizeof(jpeg_footer_t));

    /* check header */
    jpeg_header_t header;
    ssize_t bytes_read = pread(img_file, &header, sizeof(header), 0 /* offset */);
    CHECKE(bytes_read != -1);
    if (bytes_read != sizeof(header))
    {
        errno = EIO;
        return ERROR;
    }
    if (header != JPEG_HEADER)
    {
        ret = NONE;
        goto exit;
    }

    /* check footer */
    CHECKE(lseek(img_file, -2, SEEK_END) != -1);
    jpeg_footer_t footer;
    bytes_read = read(img_file, &footer, sizeof(footer));
    CHECKE(bytes_read != -1);
    if (bytes_read != sizeof(footer))
    {
        errno = EIO;
        return ERROR;
    }
    if (footer != JPEG_FOOTER)
    {
        ret = NONE;
        goto exit;
    }

    ret = JPEG;
  exit:
    CHECKE(lseek(img_file, initial_offset, SEEK_SET) != -1);
    return ret;
}

/*
 * This function checks if the file passed in is a png image. Meant to be used as a helper to
 * `get_img_type`
 *
 * This function returns PNG in case the image is a PNG, otherwise returns NONE. If it returns
 * ERROR, then the errno will be set. In case of error the file seek position may be arbitrary.
 *
 */
typedef uint64_t png_header_t;
#if __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__
#define PNG_HEADER 0x0A1A0A0D474E5089
#else
#define PNG_HEADER 0x89504E470D0A1A0A
#endif
static supported_img_t is_png_img(const file_t img_file)
{
    supported_img_t ret = NONE;

    off_t initial_offset = lseek(img_file, 0, SEEK_CUR);
    /* `lseek` returns -1 in case of error, and sets errno */
    CHECKE(initial_offset != -1);

    CHECK_FILE_SIZE(img_file, sizeof(png_header_t));

    /* check header */
    png_header_t header;
    ssize_t bytes_read = pread(img_file, &header, sizeof(header), 0 /* offset */);
    CHECKE(bytes_read != -1);
    if (bytes_read != sizeof(header))
    {
        errno = EIO;
        return ERROR;
    }
    if (header != PNG_HEADER)
    {
        ret = NONE;
        goto exit;
    }

    ret = PNG;
  exit:
    CHECKE(lseek(img_file, initial_offset, SEEK_SET) != -1);
    return ret;
}

/*
 * This function tries to determine the image type of the file whose open and valid file descriptor
 * is passed in. Returns the value of `supported_img_t` that corresponds to that type, NONE if the
 * file is not of a supported type, or ERROR if an error occurs (errno will be set).
 *
 * Note: in case of error the seek position in the file can be arbitrary. If all goes successfuly,
 * though, the seek posiiton will be returned to its original value when this function was called.
 *
 */
static supported_img_t get_img_type(FILE* const img_fp)
{
    assert(img_fp != NULL);
    file_t img_file = fileno(img_fp);
    assert(img_file >= 0);

    supported_img_t ret = is_jpeg_img(img_file);
    CHECKE(ret != ERROR);
    CHECKEXIT(ret == NONE); /* fallthrough if NONE was returned */

    ret = is_png_img(img_file);
    CHECKE(ret != ERROR);
    CHECKEXIT(ret == NONE); /* fallthrough if NONE was returned */

  exit:
    return ret;
}

/*
 * Used to handle errors when decompressing jpeg - this error manager will cause execution to jump
 * back to the owner of the error manager rather than failing the program. The caller will then
 * handle the error in a non-fatal way.
 *
 */
struct jmp_error_mgr
{
    struct jpeg_error_mgr mgr;
    jmp_buf setjmp_buffer;
};

/*
 * This function displays the error message, and then gives control back to the caller via longjmp.
 *
 */
void jmp_error_exit (j_common_ptr cinfo)
{
    /* cinfo->err is a jmp_error_mgr object, even if it's stored as a different pointer type */
    struct jmp_error_mgr* myerr = (struct jmp_error_mgr*) cinfo->err;
    myerr->mgr.output_message(cinfo);
    longjmp(myerr->setjmp_buffer, 1);
}

/*
 * This function reads a jpeg image file, converts it into data that can be passed into
 * `cairo_image_surface_create_for_data`, and loads it into a cairo surface object. Based on the
 * following guide:
 *
 * https://www4.cs.fau.de/Services/Doc/graphics/doc/jpeg/libjpeg.html
 *
 * https://raw.githubusercontent.com/LuaDist/libjpeg/master/example.c
 *
 * It asumes that the passed in FILE object is valid.
 *
 * The resulting surface is stored in the output parameter `surface`. In case of errors, this
 * function returns `false`, with errno set, otherwise it returns `true`.
 *
 */
#if __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__
#define JPEG_COLOR_SPACE JCS_EXT_BGRA
#else
#define JPEG_COLOR_SPACE JCS_EXT_ARGB
#endif
static bool jpeg_to_cairo_surface(FILE* const img_file, cairo_surface_t** const surface)
{
    assert(img_file != NULL);
    assert(fileno(img_file) != -1);

    struct jpeg_decompress_struct cinfo;
    struct jmp_error_mgr jerr;
    JSAMPARRAY buffer = NULL;
    unsigned char* data = NULL;

    bool ret = false;
    cinfo.err = jpeg_std_error(&jerr.mgr);
    jerr.mgr.error_exit = jmp_error_exit;
    if (setjmp(jerr.setjmp_buffer))
    {
        errno = EINVAL; /* catch-all error code, the actual error was printed by the mgr */
        goto exit;
    }

    jpeg_create_decompress(&cinfo);
    jpeg_stdio_src(&cinfo, img_file);

    /*
     * We can ignore the return value of `jpeg_read_header` since suspension is not possible on a
     * stdio source, and we pass true to reject a tables-only JPEG file as an error.
     *
     * Similarly we ignore the return value of both `jpeg_start_decompress` and
     * `jpeg_finish_decompress` as suspension is not possible.
     *
     */

    (void) jpeg_read_header(&cinfo, TRUE);

    /*
     * Manipulate the output colorspace into one that cairo will work with. The most compatible
     * format that I could find is jpeg's XBGR/RGBX -> cairo's RGB24.
     *
     */
    cairo_format_t data_format;
    cinfo.out_color_space = JPEG_COLOR_SPACE;
    data_format = CAIRO_FORMAT_RGB24;

    (void) jpeg_start_decompress(&cinfo);

    JDIMENSION width = cinfo.output_width;
    JDIMENSION height = cinfo.output_height;
    int stride = cairo_format_stride_for_width(data_format, width);

    /*
     * Allocate a contiguous block of memory for data, since that's what cairo takes.
     *
     * libjpeg still needs a list of pointers, so we also allocate a list of pointers, and assign
     * them to the correct places in the data array.
     *
     */
    assert(sizeof(JSAMPLE) == 1);
    data = malloc(stride * height);
    CHECKEXIT(data != NULL);
    buffer = malloc(height * sizeof(JSAMPROW));
    CHECKEXIT(buffer != NULL);
    for (size_t r = 0; r < height; ++r)
    {
        buffer[r] = &data[stride*r];
    }

    do
    {
        /*
         * The number of scanlines read so far is kept in cinfo.output_scanline, so it is not
         * necessary to store the output of jpeg_read_scanlines anywhere.
         *
         */
        (void) jpeg_read_scanlines(
            &cinfo,
            buffer+cinfo.output_scanline,
            height-cinfo.output_scanline);
    } while(cinfo.output_scanline < height);

    (void) jpeg_finish_decompress(&cinfo);

    /*
     * Now that we've filled the data from the jpeg file, create the cairo surface and make it own
     * the data buffer. Note that after this the surface is going to be valid in some way, even if
     * its status is failed. At that point the caller is responsible for checking the status of the
     * surface for errors, and this function returns `true`.
     *
     */
    ret = true;
    *surface = cairo_image_surface_create_for_data(data, data_format, width, height, stride);
    CHECKEXIT(cairo_surface_status(*surface) == CAIRO_STATUS_SUCCESS);
    /* set jpeg mime data */
    CHECKEXIT(cairo_surface_set_mime_data(
                  *surface,
                  CAIRO_MIME_TYPE_JPEG,
                  data,
                  height * stride,
                  free,
                  data) == CAIRO_STATUS_SUCCESS);
    data = NULL;

  exit:
    jpeg_destroy_decompress(&cinfo);
    if (data != NULL)
    {
        free(data);
    }
    if (buffer != NULL)
    {
        free(buffer);
    }

    return ret;
}

/*
 * This function sets `*surface_out` to a valid pointer to a `cairo_surface_t` object set from the
 * image file in `path`. It assumes `*surface_out` is NULL, otherwise we risk leaking memory.
 *
 * In case of an error prior to generating the cairo surface, this function returns `false`, errno
 * will be set, and `*surface_out` will be unchanged. It is possible for the cairo code to hit an
 * error generating the cairo surface. In that case this function still follows its contract - it
 * sets `*surface_out` to a valid cairo surface, and it is the responsibility of the caller to check
 * the status of that surface.
 *
 * All callers must check both the return value of this function (in case it is `false` more
 * information exists in the errno) and the status of the cairo surface.
 */
bool cairo_image_surface_from_file(const char* path, cairo_surface_t** surface_out)
{
    assert(*surface_out == NULL);
    FILE* img_file = fopen(path, "rb");
    CHECK(img_file != NULL);

    bool ret = true;
    supported_img_t img_type = get_img_type(img_file);
    switch (img_type)
    {
    case PNG:
        /*
         * We already have a file descriptor so this could be made a bit more efficient. It is,
         * however, less risky and complex to just use cairo's builtin PNG extractor.
         */
        *surface_out = cairo_image_surface_create_from_png(path);
        break;
    case JPEG:
        ret = jpeg_to_cairo_surface(img_file, surface_out);
        break;
    case NONE:
        errno = EINVAL; /* path is an invalid argument - not a supported image type */
        /* fallthrough */
    case ERROR:
        ret = false;
    }

    CHECK(fclose(img_file) == 0);
    return ret;
}
