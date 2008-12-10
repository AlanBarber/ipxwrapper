/* ipxwrapper - Library functions
 * Copyright (C) 2008 Daniel Collins <solemnwarning@solemnwarning.net>
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
#include <winsock2.h>
#include <wsipx.h>
#include <nspapi.h>
#include <iphlpapi.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <stdint.h>

#include "ipxwrapper.h"
#include "ipxwrapper_stubs.txt"

#define DLL_UNLOAD(dll) \
	if(dll) {\
		FreeModule(dll);\
		dll = NULL;\
	}

ipx_socket *sockets = NULL;
ipx_nic *nics = NULL;
ipx_host *hosts = NULL;
SOCKET net_fd = -1;

HMODULE winsock2_dll = NULL;
HMODULE mswsock_dll = NULL;
HMODULE wsock32_dll = NULL;

static FILE *debug_fh = NULL;
static HANDLE mutex = NULL;
static HANDLE router_thread = NULL;
static DWORD router_tid = 0;
static int mutex_locked = 0;

static HMODULE load_sysdll(char const *name);
static int init_router(void);
static DWORD WINAPI router_main(LPVOID argp);
static void add_host(unsigned char *hwaddr, uint32_t ipaddr);

BOOL WINAPI DllMain(HINSTANCE me, DWORD why, LPVOID res) {
	if(why == DLL_PROCESS_ATTACH) {
		#ifdef DEBUG
		debug_fh = fopen(DEBUG, "w");
		#endif
		
		winsock2_dll = load_sysdll("ws2_32.dll");
		mswsock_dll = load_sysdll("mswsock.dll");
		wsock32_dll = load_sysdll("wsock32.dll");
		
		if(!winsock2_dll || !mswsock_dll || !wsock32_dll) {
			return FALSE;
		}
		
		IP_ADAPTER_INFO *ifroot = get_nics();
		IP_ADAPTER_INFO *ifptr = ifroot;
		ipx_nic *enic = NULL;
		
		if(!ifptr) {
			debug("No NICs: %s", w32_error(WSAGetLastError()));
		}
		
		while(ifptr) {
			ipx_nic *nnic = malloc(sizeof(ipx_nic));
			if(!nnic) {
				return FALSE;
			}
			
			INIT_NIC(nnic);
			
			nnic->ipaddr = ntohl(inet_addr(ifptr->IpAddressList.IpAddress.String));
			nnic->netmask = ntohl(inet_addr(ifptr->IpAddressList.IpMask.String));
			nnic->bcast = nnic->ipaddr | ~nnic->netmask;
			nnic->start = (nnic->ipaddr & nnic->netmask) | 1;
			nnic->end = (nnic->ipaddr & nnic->netmask) | (~nnic->netmask & ~1);
			memcpy(nnic->hwaddr, ifptr->Address, 6);
			
			if(enic) {
				enic->next = nnic;
			}else{
				enic = nics = nnic;
			}
			
			ifptr = ifptr->Next;
		}
		
		free(ifroot);
		
		mutex = CreateMutex(NULL, FALSE, NULL);
		if(!mutex) {
			debug("Failed to create mutex");
			return FALSE;
		}
		
		WSADATA wsdata;
		int err = WSAStartup(MAKEWORD(1,1), &wsdata);
		if(err) {
			debug("Failed to initialize winsock: %s", w32_error(err));
			return FALSE;
		}
		
		if(!init_router()) {
			return FALSE;
		}
	}
	if(why == DLL_PROCESS_DETACH) {
		if(router_thread && GetCurrentThreadId() != router_tid) {
			TerminateThread(router_thread, 0);
			router_thread = NULL;
		}
		
		if(net_fd >= 0) {
			closesocket(net_fd);
			net_fd = -1;
		}
		
		if(mutex) {
			CloseHandle(mutex);
			mutex = NULL;
		}
		
		WSACleanup();
		
		DLL_UNLOAD(winsock2_dll);
		DLL_UNLOAD(mswsock_dll);
		DLL_UNLOAD(wsock32_dll);
	}
	
	return TRUE;
}

void *find_sym(char const *symbol) {
	void *addr = GetProcAddress(winsock2_dll, symbol);
	
	if(!addr) {
		addr = GetProcAddress(mswsock_dll, symbol);
	}
	if(!addr) {
		addr = GetProcAddress(wsock32_dll, symbol);
	}
	
	if(!addr) {
		debug("Unknown symbol: %s", symbol);
		abort();
	}
	
	return addr;
}

void debug(char const *fmt, ...) {
	char msg[1024];
	va_list argv;
	
	if(debug_fh) {
		va_start(argv, fmt);
		vsnprintf(msg, 1024, fmt, argv);
		va_end(argv);
		
		fprintf(debug_fh, "%s\n", msg);
		fflush(debug_fh);
	}
}

/* Lock the mutex and search the sockets list for an ipx_socket structure with
 * the requested fd, if no matching fd is found, unlock the mutex
*/
ipx_socket *get_socket(SOCKET fd) {
	lock_mutex();
	
	ipx_socket *ptr = sockets;
	
	while(ptr) {
		if(ptr->fd == fd) {
			break;
		}
		
		ptr = ptr->next;
	}
	
	if(!ptr) {
		unlock_mutex();
	}
	
	return ptr;
}

/* Lock the mutex */
void lock_mutex(void) {
	if(mutex_locked) {
		return;
	}
	
	WaitForSingleObject(mutex, INFINITE);
	mutex_locked++;
}

/* Unlock the mutex */
void unlock_mutex(void) {
	mutex_locked = 0;
	ReleaseMutex(mutex);
}

IP_ADAPTER_INFO *get_nics(void) {
	IP_ADAPTER_INFO *buf, tbuf;
	ULONG bufsize = sizeof(IP_ADAPTER_INFO);
	
	int rval = GetAdaptersInfo(&tbuf, &bufsize);
	if(rval != ERROR_SUCCESS && rval != ERROR_BUFFER_OVERFLOW) {
		WSASetLastError(rval);
		return NULL;
	}
	
	buf = malloc(bufsize);
	if(!buf) {
		WSASetLastError(ERROR_OUTOFMEMORY);
		return NULL;
	}
	
	rval = GetAdaptersInfo(buf, &bufsize);
	if(rval != ERROR_SUCCESS) {
		WSASetLastError(rval);
		free(buf);
		
		return NULL;
	}
	
	return buf;
}

/* Load a system DLL */
static HMODULE load_sysdll(char const *name) {
	char sysdir[1024], path[1024];
	HMODULE ret = NULL;
	
	GetSystemDirectory(sysdir, 1024);
	snprintf(path, 1024, "%s\\%s", sysdir, name);
	
	ret = LoadLibrary(path);
	if(!ret) {
		debug("Error loading %s: %s", path, w32_error(GetLastError()));
	}
	
	return ret;
}

/* Initialize and start the router thread */
static int init_router(void) {
	net_fd = socket(AF_INET, SOCK_DGRAM, 0);
	if(net_fd == -1) {
		debug("Failed to create listener socket: %s", w32_error(WSAGetLastError()));
		return 0;
	}
	
	struct sockaddr_in bind_addr;
	bind_addr.sin_family = AF_INET;
	bind_addr.sin_addr.s_addr = inet_addr("0.0.0.0");
	bind_addr.sin_port = htons(PORT);
	
	if(bind(net_fd, (struct sockaddr*)&bind_addr, sizeof(bind_addr)) == -1) {
		debug("Failed to bind listener socket");
		return 0;
	}
	
	BOOL broadcast = TRUE;
	int bufsize = 524288;	/* 512KiB */
	
	setsockopt(net_fd, SOL_SOCKET, SO_BROADCAST, (char*)&broadcast, sizeof(BOOL));
	setsockopt(net_fd, SOL_SOCKET, SO_RCVBUF, (char*)&bufsize, sizeof(int));
	setsockopt(net_fd, SOL_SOCKET, SO_SNDBUF, (char*)&bufsize, sizeof(int));
	
	/* Memory leak, ah well... */
	void *pbuf = malloc(PACKET_BUF_SIZE);
	if(!pbuf) {
		debug("Not enough memory for packet buffer (64KiB)");
		return 0;
	}
	
	router_thread = CreateThread(NULL, 0, &router_main, pbuf, 0, &router_tid);
	if(!router_thread) {
		debug("Failed to create router thread");
		return 0;
	}
	
	return 1;
}

/* Router thread main function
 *
 * The router thread recieves packets from the listening port and forwards them
 * to the UDP sockets which emulate IPX.
*/
static DWORD WINAPI router_main(LPVOID buf) {
	ipx_packet *packet = buf;
	int addrlen, rval, sval;
	unsigned char f6[] = {0xFF,0xFF,0xFF,0xFF,0xFF,0xFF};
	struct sockaddr_in addr;
	ipx_socket *sockptr;
	
	while(1) {
		addrlen = sizeof(addr);
		rval = recvfrom(net_fd, (char*)packet, PACKET_BUF_SIZE, 0, (struct sockaddr*)&addr, &addrlen);
		if(rval <= 0) {
			debug("Error recieving packet: %s", w32_error(WSAGetLastError()));
			continue;
		}
		
		if(rval < sizeof(ipx_packet)) {
			debug("Recieved undersized packet, discarding");
			continue;
		}
		
		packet->dest_socket = ntohs(packet->dest_socket);
		packet->src_socket = ntohs(packet->src_socket);
		packet->size = ntohs(packet->size);
		
		/* Prevent buffer overflows */
		if(packet->size > MAX_PACKET_SIZE) {
			debug("Recieved oversized packet, discarding");
			continue;
		}
		
		lock_mutex();
		
		add_host(packet->src_node, ntohl(addr.sin_addr.s_addr));
		
		for(sockptr = sockets; sockptr; sockptr = sockptr->next) {
			if(
				sockptr->flags & IPX_BOUND &&
				sockptr->flags & IPX_RECV &&
				packet->dest_socket == sockptr->socket &&
				(
					!(sockptr->flags & IPX_FILTER) ||
					packet->ptype == sockptr->f_ptype
				) && (
					memcmp(packet->dest_net, sockptr->netnum, 4) == 0 ||
					(
						memcmp(packet->dest_net, f6, 4) == 0 &&
						sockptr->flags & IPX_BROADCAST
					)
				) && (
					memcmp(packet->dest_node, sockptr->nodenum, 6) == 0 ||
					(
						memcmp(packet->dest_node, f6, 6) == 0 &&
						sockptr->flags & IPX_BROADCAST
					)
				)
			) {
				addrlen = sizeof(addr);
				if(r_getsockname(sockptr->fd, (struct sockaddr*)&addr, &addrlen) == -1) {
					continue;
				}
				
				sval = r_sendto(sockptr->fd, (char*)buf, rval, 0, (struct sockaddr*)&addr, addrlen);
				if(sval == -1) {
					debug("Error relaying packet: %s", w32_error(WSAGetLastError()));
				}
			}
		}
		
		unlock_mutex();
	}
	
	return 0;
}

/* Convert a windows error number to an error message */
char const *w32_error(DWORD errnum) {
	static char buf[1024] = {'\0'};
	
	FormatMessage(FORMAT_MESSAGE_FROM_SYSTEM, NULL, errnum, 0, buf, 1023, NULL);
	buf[strcspn(buf, "\r\n")] = '\0';
	return buf;	
}

/* Add a host to the hosts list */
static void add_host(unsigned char *hwaddr, uint32_t ipaddr) {
	ipx_host *hptr = hosts;
	
	while(hptr) {
		if(memcmp(hptr->hwaddr, hwaddr, 6) == 0) {
			hptr->ipaddr = ipaddr;
			return;
		}
		
		hptr = hptr->next;
	}
	
	hptr = malloc(sizeof(ipx_host));
	if(!hptr) {
		debug("No memory for hosts list entry");
		return;
	}
	
	INIT_HOST(hptr);
	
	memcpy(hptr->hwaddr, hwaddr, 6);
	hptr->ipaddr = ipaddr;
	
	hptr->next = hosts;
	hosts = hptr;
}

/* Search the hosts list */
ipx_host *find_host(unsigned char *hwaddr) {
	ipx_host *hptr = hosts;
	
	while(hptr) {
		if(memcmp(hptr->hwaddr, hwaddr, 6) == 0) {
			return hptr;
		}
		
		hptr = hptr->next;
	}
	
	return NULL;
}