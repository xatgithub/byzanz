/* simple gif encoder
 * Copyright (C) 2005 Benjamin Otte <otte@gnome.org>
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Library General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Library General Public License for more details.
 *
 * You should have received a copy of the GNU Library General Public
 * License along with this library; if not, write to the
 * Free Software Foundation, Inc., 59 Temple Place - Suite 330,
 * Boston, MA 02111-1307, USA.
 */

#include <string.h>

#ifndef __HAVE_GIFENC_READBITS_H__
#define __HAVE_GIFENC_READBITS_H__


#if G_BYTE_ORDER == G_BIG_ENDIAN

#define GIFENC_READ_TRIPLET(some_int, some_data) \
  G_STMT_START {\
    some_int = 0; \
    memcpy (((void *) &(some_int)) + 1, (some_data), 3); \
  }G_STMT_END
#define GIFENC_WRITE_TRIPLET(some_data, some_int) \
  memcpy ((some_data), ((void *) &(some_int)) + 1, 3)

#elif G_BYTE_ORDER == G_LITTLE_ENDIAN

#define GIFENC_READ_TRIPLET(some_int, some_data) \
  G_STMT_START {\
    some_int = 0; \
    memcpy (&(some_int), (some_data), 3);\
  }G_STMT_END
#define GIFENC_WRITE_TRIPLET(some_data, some_int) \
  memcpy ((some_data), &(some_int), 3)

#else
#error "Unknown byte order."
#endif
					

#endif /* __HAVE_GIFENC_READBITS_H__ */
