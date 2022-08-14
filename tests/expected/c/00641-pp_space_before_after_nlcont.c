#define nlcont1 \
      foo \
      bar \
      baz
#ifndef _XTRANSINT_H_
# define _XTRANSINT_H_
# define nlcont2 \
      foo \
      bar \
      baz

# ifndef __EMX__
#  define XTRANSDEBUG 1
#  define nlcont3 \
      foo \
      bar \
      baz
# else
#  define XTRANSDEBUG 1
#  define nlcont4 \
      foo \
      bar \
      baz
# endif
# define nlcont5 \
      foo \
      bar \
      baz

# ifdef _WIN32
#  define _WILLWINSOCK_
#  define nlcont6 \
      foo \
      bar \
      baz
# endif
# define nlcont7 \
      foo \
      bar \
      baz

# include "Xtrans.h"

# ifdef XTRANSDEBUG
#  include <stdio.h>
#  define nlcont8 \
      foo \
      bar \
      baz
# endif /* XTRANSDEBUG */
# define nlcont9 \
      foo \
      bar \
      baz

# include <errno.h>
# ifdef X_NOT_STDC_ENV
extern int errno;               /* Internal system error number. */
#  define nlcontA \
      foo \
      bar \
      baz
# endif

# ifndef _WIN32
#  ifndef MINIX
#   ifndef Lynx
#    define nlcontB \
      foo \
      bar \
      baz
#    include <sys/socket.h>
#   else
#    define nlcontC \
      foo \
      bar \
      baz
#    include <socket.h>
#   endif
#   define nlcontD \
      foo \
      bar \
      baz
#   include <netinet/in.h>
#   include <arpa/inet.h>
#  endif
#  define nlcontE \
      foo \
      bar \
      baz
#  ifdef __EMX__
#   define nlcontF \
      foo \
      bar \
      baz
#   include <sys/ioctl.h>
#  endif
#  define nlcontG \
      foo \
      bar \
      baz

#  if (defined(_POSIX_SOURCE) && !defined(AIXV3) && !defined(__QNX__)) || \
      defined(hpux) || defined(USG) || \
      defined(SVR4) || defined(SCO)
#   ifndef NEED_UTSNAME
#    define NEED_UTSNAME
#    define nlcontH \
      foo \
      bar \
      baz
#   endif
#   include <sys/utsname.h>
#   define nlcontI \
      foo \
      bar \
      baz
#  endif
#  define nlcontJ \
      foo \
      bar \
      baz

#  ifndef TRANS_OPEN_MAX

#   ifndef X_NOT_POSIX
#    ifdef _POSIX_SOURCE
#     include <limits.h>
#     define nlcontK \
      foo \
      bar \
      baz
#    else
#     define _POSIX_SOURCE
#     include <limits.h>
#     undef _POSIX_SOURCE
#    endif
#    define nlcontL \
      foo \
      bar \
      baz
#   endif
#   ifndef OPEN_MAX
#    ifdef __GNU__
#     define OPEN_MAX (sysconf(_SC_OPEN_MAX))
#     define nlcontM \
      foo \
      bar \
      baz
#    endif
#    ifdef SVR4
#     define nlcontN \
      foo \
      bar \
      baz
#     define OPEN_MAX 256
#    else
#     include <sys/param.h>
#     ifndef OPEN_MAX
#      if defined(__OSF1__) \
      || defined(__osf__)
#       define nlcontN \
      foo \
      bar \
      baz
#       define OPEN_MAX 256
#      else
#       define nlcontO \
      foo \
      bar \
      baz
#       ifdef NOFILE
#        define nlcontP \
      foo \
      bar \
      baz
#        define OPEN_MAX NOFILE
#       else
#        if !defined(__EMX__) && \
      !defined(__QNX__)
#         define OPEN_MAX NOFILES_MAX
#        else
#         define OPEN_MAX 256
#        endif
#       endif
#      endif
#     endif
#    endif
#   endif
#   define nlcontQ \
      foo \
      bar \
      baz
#   ifdef __GNU__
#    define TRANS_OPEN_MAX OPEN_MAX
#   elif OPEN_MAX > 256
#    define TRANS_OPEN_MAX 256
#   else
#    define TRANS_OPEN_MAX OPEN_MAX
#   endif /*__GNU__*/
#   define nlcontR \
      foo \
      bar \
      baz
#  endif /* TRANS_OPEN_MAX */
#  define nlcontS \
      foo \
      bar \
      baz
#  ifdef __EMX__
#   define ESET(val)
#  else
#   define ESET(val) errno = val
#   define nlcontT \
      foo \
      bar \
      baz
#  endif
#  define EGET() errno

# else /* _WIN32 */
#  define nlcontU \
      foo \
      bar \
      baz

#  include <limits.h>   /* for USHRT_MAX */

#  define ESET(val) WSASetLastError(val)
#  define EGET() WSAGetLastError()

# endif /* _WIN32 */
# define nlcontV \
      foo \
      bar \
      baz

# ifndef NULL
#  define NULL 0
#  define nlcontW \
      foo \
      bar \
      baz
# endif

# ifdef X11_t
#  define X_TCP_PORT      6000
#  define nlcontX \
      foo \
      bar \
      baz
# endif
# define nlcontY \
      foo \
      bar \
      baz

#endif

#define nlcontZ                                 \
      foo \
      bar \
      baz
