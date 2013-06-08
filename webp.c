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

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "php.h"
#include "php_ini.h"
#include "ext/standard/info.h"
#include "php_webp.h"

#include "webp/encode.h"

#include "./metadata.h"
#include "./stopwatch.h"

#include "./jpegdec.h"
#include "./pngdec.h"
#include "./tiffdec.h"
#include "./wicdec.h"
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

typedef enum {
	 PNG_ = 0,
	 JPEG_,
	 TIFF_,  // 'TIFF' clashes with libtiff
	 UNSUPPORTED
} InputFileFormat;

static int MyWriter(const uint8_t* data, size_t data_size,
                    const WebPPicture* const pic) {
  FILE* const out = (FILE*)pic->custom_ptr;
  return data_size ? (fwrite(data, data_size, 1, out) == 1) : 1;
}

static InputFileFormat GetImageType(FILE* in_file) {
  InputFileFormat format = UNSUPPORTED;
  unsigned int magic;
  unsigned char buf[4];

  if ((fread(&buf[0], 4, 1, in_file) != 1) ||
      (fseek(in_file, 0, SEEK_SET) != 0)) {
    return format;
  }

  magic = (buf[0] << 24) | (buf[1] << 16) | (buf[2] << 8) | buf[3];
  if (magic == 0x89504E47U) {
    format = PNG_;
  } else if (magic >= 0xFFD8FF00U && magic <= 0xFFD8FFFFU) {
    format = JPEG_;
  } else if (magic == 0x49492A00 || magic == 0x4D4D002A) {
    format = TIFF_;
  }
  return format;
}

static int ReadYUV(FILE* in_file, WebPPicture* const pic) {
  const int use_argb = pic->use_argb;
  const int uv_width = (pic->width + 1) / 2;
  const int uv_height = (pic->height + 1) / 2;
  int y;
  int ok = 0;

  pic->use_argb = 0;
  if (!WebPPictureAlloc(pic)) return ok;

  for (y = 0; y < pic->height; ++y) {
    if (fread(pic->y + y * pic->y_stride, pic->width, 1, in_file) != 1) {
      goto End;
    }
  }
  for (y = 0; y < uv_height; ++y) {
    if (fread(pic->u + y * pic->uv_stride, uv_width, 1, in_file) != 1)
      goto End;
  }
  for (y = 0; y < uv_height; ++y) {
    if (fread(pic->v + y * pic->uv_stride, uv_width, 1, in_file) != 1)
      goto End;
  }
  ok = 1;
  if (use_argb) ok = WebPPictureYUVAToARGB(pic);

 End:
  return ok;
}

static int ReadPicture(const char* const filename, WebPPicture* const pic, int keep_alpha) {
  int ok = 0;
  FILE* in_file = fopen(filename, "rb");
  if (in_file == NULL) {
    fprintf(stderr, "Error! Cannot open input file '%s'\n", filename);
    return ok;
  }

  if (pic->width == 0 || pic->height == 0) {
    // If no size specified, try to decode it as PNG/JPEG (as appropriate).
    const InputFileFormat format = GetImageType(in_file);

    if (format == PNG_) {
      ok = ReadPNG(in_file, pic, keep_alpha, NULL);
    } else if (format == JPEG_) {
      ok = ReadJPEG(in_file, pic, NULL);
    } else if (format == TIFF_) {
      ok = ReadTIFF(filename, pic, keep_alpha, NULL);
    }
  } else {
    // If image size is specified, infer it as YUV format.
    ok = ReadYUV(in_file, pic);
  }
  if (!ok) {
    fprintf(stderr, "Error! Could not process file %s\n", filename);
  }

  fclose(in_file);
  return ok;
}
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
PHP_FUNCTION(image2webp)
{

	int keep_alpha = 1;
	WebPPicture picture;
	WebPConfig config;
	WebPAuxStats stats;
	FILE *out = NULL;

	const char *in_file = NULL, *out_file = NULL;
	zval *z_in_file, *z_out_file;
	double z_quality;

	if (zend_parse_parameters(ZEND_NUM_ARGS() TSRMLS_CC, "zz|d",
			&z_in_file, &z_out_file, &z_quality) == FAILURE) {
		WRONG_PARAM_COUNT;
	}
	in_file = Z_STRVAL_P(z_in_file);
	out_file = Z_STRVAL_P(z_out_file);

	if (!WebPPictureInit(&picture) || !WebPConfigInit(&config)) {
		php_error_docref(NULL TSRMLS_CC, E_WARNING, "Error! Version mismatch!\n");
		RETURN_FALSE;
	}

	if (z_quality >0) config.quality = z_quality;

	if (!WebPValidateConfig(&config))
	{
		php_error_docref(NULL TSRMLS_CC, E_WARNING, "Error! Invalid configuration.\n");
		goto Error;
	}

	if (!ReadPicture(in_file, &picture, keep_alpha)) {
		php_error_docref(NULL TSRMLS_CC, E_WARNING, "Error! Not JPEG or PNG image\n");
		goto Error;
	}
	picture.progress_hook = NULL;
	// Open the output
	out = fopen(out_file, "wb");
	if (out)
	{
		picture.writer = MyWriter;
		picture.custom_ptr = (void*)out;
	} else {
		php_error_docref(NULL TSRMLS_CC, E_WARNING, "No output buffer specified.\n");
		goto Error;
	}
	if (!WebPEncode(&config, &picture))
	{
		php_error_docref(NULL TSRMLS_CC, E_WARNING, "Error! Cannot encode picture as WebP\nError code: %d (%s)\n", picture.error_code, kErrorMessages[picture.error_code]);
		goto Error;
	}
	RETURN_TRUE;
Error:
	WebPPictureFree(&picture);
	fclose(out);

	RETURN_FALSE;
}
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
