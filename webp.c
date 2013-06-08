/*
  +----------------------------------------------------------------------+
  | PHP Version 5                                                        |
  +----------------------------------------------------------------------+
  | Copyright (c) 1997-2013 The PHP Group                                |
  +----------------------------------------------------------------------+
  | This source file is subject to version 3.01 of the PHP license,      |
  | that is bundled with this package in the file LICENSE, and is        |
  | available through the world-wide-web at the following url:           |
  | http://www.php.net/license/3_01.txt                                  |
  | If you did not receive a copy of the PHP license and are unable to   |
  | obtain it through the world-wide-web, please send a note to          |
  | license@php.net so we can mail you a copy immediately.               |
  +----------------------------------------------------------------------+
  | Author:                                                              |
  +----------------------------------------------------------------------+
*/

#ifdef WEBP_HAVE_PNG
#include <png.h>
#endif

#ifdef WEBP_HAVE_JPEG
#include <setjmp.h>   // note: this must be included *after* png.h
#include <jpeglib.h>
#endif

#ifdef WEBP_HAVE_TIFF
#include <tiffio.h>
#endif


#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "php.h"
#include "php_ini.h"
#include "ext/standard/info.h"
#include "php_webp.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

#include "webp/encode.h"
#ifndef WEBP_DLL
#if defined(__cplusplus) || defined(c_plusplus)
extern "C" 
{
#endif

    extern void* VP8GetCPUInfo;   // opaque forward declaration.

#if defined(__cplusplus) || defined(c_plusplus)
}    // extern "C"
#endif
#endif  // WEBP_DLL

//------------------------------------------------------------------------------

/* If you declare any globals in php_webp.h uncomment this:
ZEND_DECLARE_MODULE_GLOBALS(webp)
*/

/* True global resources - no need for thread safety here */
static int le_webp;
static int verbose = 0;

#ifdef WEBP_HAVE_JPEG
struct my_error_mgr
{/*{{{*/
    struct jpeg_error_mgr pub;
    jmp_buf setjmp_buffer;
};/*}}}*/

static void my_error_exit(j_common_ptr dinfo)
{/*{{{*/
    struct my_error_mgr* myerr = (struct my_error_mgr*) dinfo->err;
    (*dinfo->err->output_message) (dinfo);
    longjmp(myerr->setjmp_buffer, 1);
}/*}}}*/

static int ReadJPEG(unsigned char* data,const unsigned int dataSize, WebPPicture* const pic)
{/*{{{*/
    int ok = 0;
    int stride, width, height;
    uint8_t* rgb = NULL;
    uint8_t* row_ptr = NULL;
    struct jpeg_decompress_struct dinfo;
    struct my_error_mgr jerr;
    JSAMPARRAY buffer;

    dinfo.err = jpeg_std_error(&jerr.pub);
    jerr.pub.error_exit = my_error_exit;

    if (setjmp(jerr.setjmp_buffer))
    {
Error:
        jpeg_destroy_decompress(&dinfo);
        goto End;
    }

    jpeg_create_decompress(&dinfo);
    jpeg_mem_src(&dinfo, data, dataSize);
//    jpeg_stdio_src(&dinfo, in_file);
    jpeg_read_header(&dinfo, TRUE);

    dinfo.out_color_space = JCS_RGB;
    dinfo.dct_method = JDCT_IFAST;
    dinfo.do_fancy_upsampling = TRUE;

    jpeg_start_decompress(&dinfo);

    if (dinfo.output_components != 3)
    {
        goto Error;
    }

    width = dinfo.output_width;
    height = dinfo.output_height;
    stride = dinfo.output_width * dinfo.output_components * sizeof(*rgb);

    rgb = (uint8_t*)malloc(stride * height);
    if (rgb == NULL)
    {
        goto End;
    }
    row_ptr = rgb;

    buffer = (*dinfo.mem->alloc_sarray) ((j_common_ptr) &dinfo,
            JPOOL_IMAGE, stride, 1);
    if (buffer == NULL)
    {
        goto End;
    }

    while (dinfo.output_scanline < dinfo.output_height)
    {
        if (jpeg_read_scanlines(&dinfo, buffer, 1) != 1)
        {
            goto End;
        }
        memcpy(row_ptr, buffer[0], stride);
        row_ptr += stride;
    }

    jpeg_finish_decompress(&dinfo);
    jpeg_destroy_decompress(&dinfo);

    // WebP conversion.
    pic->width = width;
    pic->height = height;
    ok = WebPPictureImportRGB(pic, rgb, stride);

End:
    if (rgb)
    {
        free(rgb);
    }
    return ok;
}/*}}}*/

#else
static int ReadJPEG(unsigned char* data,const unsigned int dataSize, WebPPicture* const pic)
{/*{{{*/
    fprintf(stderr, "JPEG support not compiled. Please install the libjpeg "
            "development package before building.\n");
    return 0;
}/*}}}*/
#endif

#ifdef WEBP_HAVE_PNG
static void PNGAPI error_function(png_structp png, png_const_charp dummy)
{/*{{{*/
    (void)dummy;  // remove variable-unused warning
    longjmp(png_jmpbuf(png), 1);
}/*}}}*/

//从内存读取PNG图片的回调函数
static void pngReadCallback(png_structp png_ptr, png_bytep data, png_size_t length)
{/*{{{*/
    ImageSource* isource = (ImageSource*)png_get_io_ptr(png_ptr);
    if(isource->offset + length <= isource->size)
    {
        memcpy(data, isource->data+isource->offset, length);
        isource->offset += length;
    }
    else
    {
        png_error(png_ptr, "pngReaderCallback failed");
    }
}/*}}}*/
static int ReadPNG(unsigned char* data,const unsigned int dataSize, WebPPicture* const pic, int keep_alpha)
{/*{{{*/
    png_structp png;
    png_infop info;
    int color_type, bit_depth, interlaced;
    int has_alpha;
    int num_passes;
    int p;
    int ok = 0;
    png_uint_32 width, height, y;
    int stride;
    uint8_t* rgb = NULL;

    png = png_create_read_struct(PNG_LIBPNG_VER_STRING, 0, 0, 0);
    if (png == NULL)
    {
        goto End;
    }

    png_set_error_fn(png, 0, error_function, NULL);
    if (setjmp(png_jmpbuf(png)))
    {
Error:
        png_destroy_read_struct(&png, NULL, NULL);
        free(rgb);
        goto End;
    }

    info = png_create_info_struct(png);
    if (info == NULL) goto Error;
    ImageSource imgsource;
    imgsource.data = data;
    imgsource.size = dataSize;
    imgsource.offset = 0;
    png_set_read_fn(png, &imgsource,pngReadCallback);
    png_set_sig_bytes(png, 0);
    png_read_info(png, info);
    if (!png_get_IHDR(png, info,
                &width, &height, &bit_depth, &color_type, &interlaced,
                NULL, NULL)) goto Error;

    png_set_strip_16(png);
    png_set_packing(png);
    if (color_type == PNG_COLOR_TYPE_PALETTE) png_set_palette_to_rgb(png);
    if (color_type == PNG_COLOR_TYPE_GRAY ||
            color_type == PNG_COLOR_TYPE_GRAY_ALPHA)
    {
        if (bit_depth < 8)
        {
            png_set_expand_gray_1_2_4_to_8(png);
        }
        png_set_gray_to_rgb(png);
    }
    if (png_get_valid(png, info, PNG_INFO_tRNS))
    {
        png_set_tRNS_to_alpha(png);
        has_alpha = 1;
    } else
    {
        has_alpha = !!(color_type & PNG_COLOR_MASK_ALPHA);
    }

    if (!keep_alpha)
    {
        png_set_strip_alpha(png);
        has_alpha = 0;
    }

    num_passes = png_set_interlace_handling(png);
    png_read_update_info(png, info);
    stride = (has_alpha ? 4 : 3) * width * sizeof(*rgb);
    rgb = (uint8_t*)malloc(stride * height);
    if (rgb == NULL) goto Error;
    for (p = 0; p < num_passes; ++p)
    {
        for (y = 0; y < height; ++y)
        {
            png_bytep row = rgb + y * stride;
            png_read_rows(png, &row, NULL, 1);
        }
    }
    png_read_end(png, info);
    png_destroy_read_struct(&png, &info, NULL);

    pic->width = width;
    pic->height = height;
    ok = has_alpha ? WebPPictureImportRGBA(pic, rgb, stride)
        : WebPPictureImportRGB(pic, rgb, stride);
    free(rgb);

    if (ok && has_alpha && keep_alpha == 2)
    {
        WebPCleanupTransparentArea(pic);
    }

End:
    return ok;
}/*}}}*/
#else
static int ReadPNG(unsigned char* data,const unsigned int dataSize, WebPPicture* const pic, int keep_alpha)
{/*{{{*/
    fprintf(stderr, "PNG support not compiled. Please install the libpng "
            "development package before building.\n");
    return 0;
}/*}}}*/
#endif

static ImageFormat GetImageType(const unsigned char* const blob)
{/*{{{*/
    ImageFormat format = UNSUPPORTED;
    unsigned int magic;
    unsigned char buf[4];

    if(NULL == blob||!memcpy(buf,blob,4))
    {
        return format;
    }

    magic = (buf[0] << 24) | (buf[1] << 16) | (buf[2] << 8) | buf[3];
    if (magic == 0x89504E47U)
    {
        format = PNG_;
    } else if (magic >= 0xFFD8FF00U && magic <= 0xFFD8FFFFU)
    {
        format = JPEG_;
    }
    return format;
}/*}}}*/

static int ReadPicture(unsigned char* blob,int datasize, WebPPicture* const pic,
        int keep_alpha)
{/*{{{*/
    int ok = 0;
    if (blob == NULL||!strlen(blob))
    {
        fprintf(stderr, "Error! No img data provided\n");
        return ok;
    }

    // If no size specified, try to decode it as PNG/JPEG (as appropriate).
    const ImageFormat format = GetImageType(blob);
    if (format == PNG_)
    {
        ok = ReadPNG(blob,datasize, pic, keep_alpha);
    } else if (format == JPEG_)
    {
        ok = ReadJPEG(blob,datasize, pic);
    }
    if (!ok)
    {
        fprintf(stderr, "Error! Not JPEG or PNG image\n");
    }

    return ok;
}/*}}}*/

//------------------------------------------------------------------------------
static int MemoryWriter(const uint8_t* data, size_t data_size,
        WebPPicture* pic)
{/*{{{*/
    out_buf_t* const out = (out_buf_t*)pic->custom_ptr;
    if(!data_size)
        return 1;
    memcpy(out->start + out->len, data,data_size);
    out->len += data_size;
    return 1;
}/*}}}*/

// Error messages

static const char* const kErrorMessages[] =
{/*{{{*/
    "OK",
    "OUT_OF_MEMORY: Out of memory allocating objects",
    "BITSTREAM_OUT_OF_MEMORY: Out of memory re-allocating byte buffer",
    "NULL_PARAMETER: NULL parameter passed to function",
    "INVALID_CONFIGURATION: configuration is invalid",
    "BAD_DIMENSION: Bad picture dimension. Maximum width and height "
        "allowed is 16383 pixels.",
    "PARTITION0_OVERFLOW: Partition #0 is too big to fit 512k.\n"
        "To reduce the size of this partition, try using less segments "
        "with the -segments option, and eventually reduce the number of "
        "header bits using -partition_limit. More details are available "
        "in the manual (`man cwebp`)",
    "PARTITION_OVERFLOW: Partition is too big to fit 16M",
    "BAD_WRITE: Picture writer returned an I/O error",
    "FILE_TOO_BIG: File would be too big to fit in 4G",
    "USER_ABORT: encoding abort requested by user"
};/*}}}*/

static int cwebp(unsigned char* blob,int datasize,out_buf_t* out)
{/*{{{*/
    int return_value = -1;
    int c;
    int short_output = 0;
    int keep_alpha = 1;
    WebPPicture picture;
    WebPConfig config;
    WebPAuxStats stats;

    if (blob == NULL)
    {
		php_error_docref(NULL TSRMLS_CC, E_WARNING, "No blob specified!\n");
        goto Error;
    }
    if (!WebPPictureInit(&picture) ||
            !WebPConfigInit(&config))
    {
    	php_error_docref(NULL TSRMLS_CC, E_WARNING, "Error! Version mismatch!\n");
        return -1;
    }

    // Check for unsupported command line options for lossless mode and log
    // warning for such options.

    if (!WebPValidateConfig(&config))
    {
    	php_error_docref(NULL TSRMLS_CC, E_WARNING, "Error! Invalid configuration.\n");
        goto Error;
    }

    // Read the input
    if (!ReadPicture(blob, datasize,&picture, keep_alpha))
    {
    	php_error_docref(NULL TSRMLS_CC, E_WARNING, "Error! Not JPEG or PNG image\n");
        goto Error;
    }
    picture.progress_hook = NULL;

    // Open the output
    if (out)
    {
        picture.writer = (WebPWriterFunction)MemoryWriter;
        picture.custom_ptr = (void*)out;
    } else
    {
    	php_error_docref(NULL TSRMLS_CC, E_WARNING, "No output buffer specified.\n");
        goto Error;
    }
    if (!WebPEncode(&config, &picture))
    {
    	php_error_docref(NULL TSRMLS_CC, E_WARNING, "Error! Cannot encode picture as WebP\nError code: %d (%s)\n", picture.error_code, kErrorMessages[picture.error_code]);
        goto Error;
    }
    return_value = 0;

Error:
    WebPPictureFree(&picture);

    return return_value;
}/*}}}*/

/* {{{ webp_functions[]
 *
 * Every user visible function must have an entry in webp_functions[].
 */
const zend_function_entry webp_functions[] = {
	/*PHP_FE(confirm_webp_compiled,	NULL)*/		/* For testing, remove later. */
	PHP_FE(image2webp,	NULL)		/* For testing, remove later. */
    {NULL,NULL,NULL}/* Must be the last line in webp_functions[] */
};
/* }}} */

/* {{{ webp_module_entry
 */
zend_module_entry webp_module_entry = {
#if ZEND_MODULE_API_NO >= 20010901
	STANDARD_MODULE_HEADER,
#endif
	"webp",
	webp_functions,
	PHP_MINIT(webp),
	PHP_MSHUTDOWN(webp),
	PHP_RINIT(webp),		/* Replace with NULL if there's nothing to do at request start */
	PHP_RSHUTDOWN(webp),	/* Replace with NULL if there's nothing to do at request end */
	PHP_MINFO(webp),
#if ZEND_MODULE_API_NO >= 20010901
	"0.1", /* Replace with version number for your extension */
#endif
	STANDARD_MODULE_PROPERTIES
};
/* }}} */

#ifdef COMPILE_DL_WEBP
ZEND_GET_MODULE(webp)
#endif

/* {{{ PHP_INI
 */
/* Remove comments and fill if you need to have entries in php.ini
PHP_INI_BEGIN()
    STD_PHP_INI_ENTRY("webp.global_value",      "42", PHP_INI_ALL, OnUpdateLong, global_value, zend_webp_globals, webp_globals)
    STD_PHP_INI_ENTRY("webp.global_string", "foobar", PHP_INI_ALL, OnUpdateString, global_string, zend_webp_globals, webp_globals)
PHP_INI_END()
*/
/* }}} */

/* {{{ php_webp_init_globals
 */
/* Uncomment this function if you have INI entries
static void php_webp_init_globals(zend_webp_globals *webp_globals)
{
	webp_globals->global_value = 0;
	webp_globals->global_string = NULL;
}
*/
/* }}} */

/* {{{ PHP_MINIT_FUNCTION
 */
PHP_MINIT_FUNCTION(webp)
{
	/* If you have INI entries, uncomment these lines 
	REGISTER_INI_ENTRIES();
	*/
	return SUCCESS;
}
/* }}} */

/* {{{ PHP_MSHUTDOWN_FUNCTION
 */
PHP_MSHUTDOWN_FUNCTION(webp)
{
	/* uncomment this line if you have INI entries
	UNREGISTER_INI_ENTRIES();
	*/
	return SUCCESS;
}
/* }}} */

/* Remove if there's nothing to do at request start */
/* {{{ PHP_RINIT_FUNCTION
 */
PHP_RINIT_FUNCTION(webp)
{
	return SUCCESS;
}
/* }}} */

/* Remove if there's nothing to do at request end */
/* {{{ PHP_RSHUTDOWN_FUNCTION
 */
PHP_RSHUTDOWN_FUNCTION(webp)
{
	return SUCCESS;
}
/* }}} */

/* {{{ PHP_MINFO_FUNCTION
 */
PHP_MINFO_FUNCTION(webp)
{
	php_info_print_table_start();
	php_info_print_table_header(2, "webp support", "enabled");
	php_info_print_table_end();

	/* Remove comments if you have entries in php.ini
	DISPLAY_INI_ENTRIES();
	*/
}
/* }}} */


/* Remove the following function when you have succesfully modified config.m4
   so that your module can be compiled into PHP, it exists only for testing
   purposes. */

/* Every user-visible function in PHP should document itself in the source */
/* {{{ proto string confirm_webp_compiled(string arg)
   Return a string to confirm that the module is compiled in */
/*PHP_FUNCTION(confirm_webp_compiled)
{
	char *arg = NULL;
	int arg_len, len;
	char *strg;

	if (zend_parse_parameters(ZEND_NUM_ARGS() TSRMLS_CC, "s", &arg, &arg_len) == FAILURE) {
		return;
	}

	len = spprintf(&strg, 0, "Congratulations! You have successfully modified ext/%.78s/config.m4. Module %.78s is now compiled into PHP.", "webp", arg);
	RETURN_STRINGL(strg, len, 0);
}*/
/* }}} */
/* The previous line is meant for vim and emacs, so it can correctly fold and 
   unfold functions in source code. See the corresponding marks just before 
   function definition, where the functions purpose is also documented. Please 
   follow this convention for the convenience of others editing your code.
*/

/*
 * Local variables:
 * tab-width: 4
 * c-basic-offset: 4
 * End:
 * vim600: noet sw=4 ts=4 fdm=marker
 * vim<600: noet sw=4 ts=4
 */
PHP_FUNCTION(image2webp)
{
	char *blob = NULL;
	int datasize, len;
	char *strg;
    uint8_t *out = emalloc(2*1024*1024);
    out_buf_t out_buf;
    out_buf.start = out;
    out_buf.len = 0;

	if (zend_parse_parameters(ZEND_NUM_ARGS() TSRMLS_CC, "s", &blob, &datasize) == FAILURE) {
		return;
	}
    cwebp(blob,datasize,&out_buf);
	RETURN_STRINGL(out_buf.start, out_buf.len, 0);
}
