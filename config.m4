dnl $Id$
dnl config.m4 for extension webp

dnl Comments in this file start with the string 'dnl'.
dnl Remove where necessary. This file will not work
dnl without editing.

dnl If your extension references something external, use with:

dnl PHP_ARG_WITH(webp, for webp support,
dnl Make sure that the comment is aligned:
dnl [  --with-webp             Include webp support])

dnl Otherwise use enable:

PHP_ARG_ENABLE(webp, whether to enable webp support,
dnl Make sure that the comment is aligned:
[  --enable-webp           Enable webp support])

if test "$PHP_WEBP" != "no"; then
    export CPPFLAGS="$CPPFLAGS -DWEBP_HAVE_JPEG -DWEBP_HAVE_PNG"

  dnl # --with-webp -> check with-path
  dnl SEARCH_PATH="/usr/local /usr"     # you might want to change this
  dnl SEARCH_FOR="/include/webp.h"  # you most likely want to change this
  dnl if test -r $PHP_WEBP/$SEARCH_FOR; then # path given as parameter
  dnl   WEBP_DIR=$PHP_WEBP
  dnl else # search default path list
  dnl   AC_MSG_CHECKING([for webp files in default path])
  dnl   for i in $SEARCH_PATH ; do
  dnl     if test -r $i/$SEARCH_FOR; then
  dnl       WEBP_DIR=$i
  dnl       AC_MSG_RESULT(found in $i)
  dnl     fi
  dnl   done
  dnl fi
  dnl
  dnl if test -z "$WEBP_DIR"; then
  dnl   AC_MSG_RESULT([not found])
  dnl   AC_MSG_ERROR([Please reinstall the webp distribution])
  dnl fi

  dnl # --with-webp -> add include path
  PHP_ADD_INCLUDE(/usr/local/include)

  dnl # --with-webp -> check for lib and symbol presence
  dnl LIBNAME=webp # you may want to change this
  dnl LIBSYMBOL=webp # you most likely want to change this 

  dnl PHP_CHECK_LIBRARY($LIBNAME,$LIBSYMBOL,
  dnl [
  PHP_ADD_LIBRARY_WITH_PATH(jpeg, /usr/local/lib, WEBP_SHARED_LIBADD)
  PHP_ADD_LIBRARY_WITH_PATH(png, /usr/local/lib, WEBP_SHARED_LIBADD)
  PHP_ADD_LIBRARY_WITH_PATH(webp, /usr/local/lib, WEBP_SHARED_LIBADD)
  dnl   AC_DEFINE(HAVE_WEBPLIB,1,[ ])
  dnl ],[
  dnl   AC_MSG_ERROR([wrong webp lib version or lib not found])
  dnl ],[
  dnl   -L$WEBP_DIR/lib -lm
  dnl ])
  dnl
  PHP_SUBST(WEBP_SHARED_LIBADD)

  PHP_NEW_EXTENSION(webp, webp.c, $ext_shared)
fi
