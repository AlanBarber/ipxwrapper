/* IPXWrapper - Common header
 * Copyright (C) 2011 Daniel Collins <solemnwarning@solemnwarning.net>
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

#ifndef IPXWRAPPER_COMMON_H
#define IPXWRAPPER_COMMON_H

#include <windows.h>
#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>

#include "addr.h"

enum ipx_log_level {
	LOG_CALL = 1,
	LOG_DEBUG,
	LOG_INFO = 4,
	LOG_WARNING,
	LOG_ERROR
};

extern enum ipx_log_level min_log_level;

const char *w32_error(DWORD errnum);

HKEY reg_open_main(bool readwrite);
HKEY reg_open_subkey(HKEY parent, const char *path, bool readwrite);
void reg_close(HKEY key);

bool reg_get_bin(HKEY key, const char *name, void *buf, size_t size, const void *default_value);
bool reg_set_bin(HKEY key, const char *name, void *buf, size_t size);

DWORD reg_get_dword(HKEY key, const char *name, DWORD default_value);
bool reg_set_dword(HKEY key, const char *name, DWORD value);

void load_dll(unsigned int dllnum);
void unload_dlls(void);
void __stdcall *find_sym(unsigned int dllnum, const char *symbol);
void __stdcall log_call(unsigned int dllnum, const char *symbol);

void log_open(const char *file);
void log_close();
void log_printf(enum ipx_log_level level, const char *fmt, ...);

#endif /* !IPXWRAPPER_COMMON_H */
