/* IPXWrapper - Address cache
 * Copyright (C) 2008-2012 Daniel Collins <solemnwarning@solemnwarning.net>
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2 as published by
 * the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program; if not, write to the Free Software Foundation, Inc., 51
 * Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
*/

#ifndef _ADDRCACHE_H
#define _ADDRCACHE_H

#include <winsock2.h>
#include <windows.h>
#include <stdint.h>

#include "common.h"

void addr_cache_init(void);
void addr_cache_cleanup(void);

int addr_cache_get(SOCKADDR_STORAGE *addr, size_t *addrlen, addr32_t net, addr48_t node, uint16_t sock);
void addr_cache_set(const struct sockaddr *addr, size_t addrlen, addr32_t net, addr48_t node, uint16_t sock);

#endif /* !_ADDRCACHE_H */
