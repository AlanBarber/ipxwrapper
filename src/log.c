/* ipxwrapper - Logging functions
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

#include <windows.h>
#include <stdlib.h>
#include <stdio.h>
#include <stdarg.h>

#include "ipxwrapper.h"

static HANDLE log_fh = NULL;
static HANDLE log_mutex = NULL;

void log_open() {
	if(!(log_mutex = CreateMutex(NULL, FALSE, NULL))) {
		abort();
	}
	
	log_fh = CreateFile(
		"ipxwrapper.log",
		GENERIC_READ | GENERIC_WRITE,
		FILE_SHARE_DELETE | FILE_SHARE_READ | FILE_SHARE_WRITE,
		NULL,
		OPEN_ALWAYS,
		FILE_ATTRIBUTE_NORMAL,
		NULL
	);
	
	if(log_fh == INVALID_HANDLE_VALUE) {
		log_fh = NULL;
	}
}

void log_close() {
	if(log_fh) {
		CloseHandle(log_fh);
		log_fh = NULL;
	}
	
	CloseHandle(log_mutex);
	log_mutex = NULL;
}

void log_printf(const char *fmt, ...) {
	DWORD called = GetTickCount();
	
	WaitForSingleObject(log_mutex, INFINITE);
	
	if(!log_fh) {
		ReleaseMutex(log_mutex);
		return;
	}
	
	va_list argv;
	char msg[1024], tstr[32];
	
	va_start(argv, fmt);
	vsnprintf(msg, 1024, fmt, argv);
	va_end(argv);
	
	snprintf(tstr, 32, "[%u.%02u] ", (unsigned int)(called/1000), (unsigned int)((called % 1000) / 10));
	
	OVERLAPPED off;
	off.Offset = 0;
	off.OffsetHigh = 0;
	off.hEvent = 0;
	
	if(!LockFileEx(log_fh, LOCKFILE_EXCLUSIVE_LOCK, 0, 1, 0, &off)) {
		ReleaseMutex(log_mutex);
		return;
	}
	
	if(SetFilePointer(log_fh, 0, NULL, FILE_END) != INVALID_SET_FILE_POINTER) {
		DWORD written;
		
		WriteFile(log_fh, tstr, strlen(tstr), &written, NULL);
		WriteFile(log_fh, msg, strlen(msg), &written, NULL);
		WriteFile(log_fh, "\r\n", 2, &written, NULL);
		
		FlushFileBuffers(log_fh);
	}
	
	UnlockFile(log_fh, 0, 0, 1, 0);
	
	ReleaseMutex(log_mutex);
}
