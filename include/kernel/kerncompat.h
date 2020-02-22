#ifndef _KERNCOMPAT_H
#define _KERNCOMPAT_H

#include <stddef.h>
#include <stdbool.h>

#ifndef __always_inline
#define __always_inline __inline __attribute__ ((__always_inline__))
#endif

#define container_of(ptr, type, member) ({                      \
        const typeof( ((type *)0)->member ) *__mptr = (ptr);    \
	        (type *)( (char *)__mptr - offsetof(type,member) );})

#endif /* _KERNCOMPAT_H */
