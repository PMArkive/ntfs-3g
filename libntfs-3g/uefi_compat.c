/*
 * uefi_compat.c - Definition of standard function calls for UEFI.
 *
 * Copyright © 2021 Pete Batard <pete@akeo.ie>
 *
 * This program/include file is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as published
 * by the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program/include file is distributed in the hope that it will be
 * useful, but WITHOUT ANY WARRANTY; without even the implied warranty
 * of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include <stdarg.h>
#include <stdlib.h>

#include "compat.h"
#include "logging.h"
#include "uefi_support.h"

#ifdef __MAKEWITH_GNUEFI
#include <efi.h>
#include <efilib.h>

#else /* __MAKEWITH_GNUEFI */

#include <Base.h>
#include <Uefi.h>
#include <Library/BaseLib.h>
#include <Library/UefiLib.h>
#include <Library/PrintLib.h>
#include <Library/BaseMemoryLib.h>
#include <Library/MemoryAllocationLib.h>
#include <Library/UefiBootServicesTableLib.h>
#include <Library/UefiRuntimeServicesTableLib.h>

/* This is required to avoid LNK2043 errors with EDK2/MSVC/IA32 */
#if defined(_MSC_VER) && defined(_M_IX86)
#pragma comment(linker, "/INCLUDE:_MultS64x64")
#pragma comment(linker, "/INCLUDE:_DivU64x64Remainder")
#pragma comment(linker, "/INCLUDE:_DivS64x64Remainder")
#endif

#endif /* __MAKEWITH_GNUEFI */

static int __errno;

#ifdef _MSC_VER
int* _errno(void)
{
	return &__errno;
}
#else
int* __errno_location(void)
{
	return &__errno;
}
#endif

int ffs(int i)
{
#if __GNUC__ > 3 || (__GNUC__ == 3 && __GNUC_MINOR__ >= 4)
	return __builtin_ffs(i);
#elif defined _MSC_VER
	unsigned long bit = 0;
	if (_BitScanForward(&bit, i))
		return bit + 1;
	else
		return 0;
#else
#warn "ffs() not implemented"
#endif
}

void* malloc(size_t size)
{
	return AllocatePool(size);
}

void* calloc(size_t nmemb, size_t size)
{
	return AllocateZeroPool(size * nmemb);
}

void* realloc(void* p, size_t new_size)
{
	size_t* ptr = (size_t*)p;

	if (ptr != NULL) {
		ptr = &ptr[-1];
#ifdef __MAKEWITH_GNUEFI
		ptr = ReallocatePool(ptr, (UINTN)*ptr, (UINTN)(new_size + sizeof(size_t)));
#else
		ptr = ReallocatePool((UINTN)*ptr, (UINTN)(new_size + sizeof(size_t)), ptr);
#endif
		if (ptr != NULL)
			*ptr++ = new_size;
	}
	return ptr;
}

/*
 * Depending on the compiler, the arch, and the toolchain, these
 * function may or may not need to be provided...
 */
#ifndef USE_COMPILER_INTRINSICS_LIB
#ifndef __MAKEWITH_GNUEFI
void* memcpy(void* dest, const void* src, size_t n)
{
	CopyMem(dest, src, n);
	return dest;
}

void* memset(void* s, int c, size_t n)
{
	SetMem(s, n, (UINT8)c);
	return s;
}
#endif /* __MAKEWITH_GNUEFI */

void* memmove(void* dest, const void* src, size_t n)
{
	/* CopyMem() supports the handling of overlapped regions */
	CopyMem(dest, src, n);
	return dest;
}

int memcmp(const void* s1, const void* s2, size_t n)
{
	return (int)CompareMem(s1, s2, n);
}
#endif /* USE_COMPILER_INTRINSICS_LIB */

void free(void* a)
{
	FreePool(a);
}

/* TODO: Secure all these string functions */
int atoi(const char* c)
{
	int value = 0;
	int sign = 1;

	if (*c == '+' || *c == '-') {
		if (*c == '-')
			sign = -1;
		c++;
	}

	while ((*c >= '0') && (*c <= '9')) {
		value *= 10;
		value += (int)(*c - '0');
		c++;
	}

	return value * sign;
}

size_t strlen(const char* s)
{
#ifdef __MAKEWITH_GNUEFI
	return strlena(s);
#else
	return AsciiStrLen(s);
#endif
}

int strcmp(const char* s1, const char* s2)
{
#ifdef __MAKEWITH_GNUEFI
	return (int)strcmpa(s1, s2);
#else
	return (int)AsciiStrCmp(s1, s2);
#endif
}

int strncmp(const char* s1, const char* s2, size_t n)
{
#ifdef __MAKEWITH_GNUEFI
	return (int)strncmpa(s1, s2, n);
#else
	return (int)AsciiStrnCmp(s1, s2, n);
#endif
}

char* strcpy(char* dest, const char* src)
{
	return memcpy(dest, src, strlen(src) + 1);
}

#ifndef min
#define min(x,y)                ((x)<(y)?(x):(y))
#endif

char* strncpy(char* dest, const char* src, size_t n)
{
	return memcpy(dest, src, min(strlen(src) + 1, n));
}

char* strcat(char* dest, const char* src)
{
	return strcpy(&dest[strlen(dest)], src);
}

char* strdup(const char* s)
{
	char* ret = malloc(strlen(s) + 1);
	if (ret == NULL)
		return NULL;
	return memcpy(ret, s, strlen(s) + 1);
}

int snprintf(char* str, size_t size, const char* format, ...)
{
	size_t i, ret;
	VA_LIST args;
	char* ascii_format;

	if (!str || !format) {
		errno = EINVAL;
		return -1;
	}

	ascii_format = strdup(format);
	if (ascii_format == NULL) {
		/* To silence an MSVC warning */
		str[0] = 0;
		errno = ENOMEM;
		return -1;
	}

	/* Convert %s sequences to %a */
	for (i = 1; i < strlen(format); i++)
		if ((ascii_format[i] == 's') && (ascii_format[i - 1] == '%'))
			ascii_format[i] = 'a';

	VA_START(args, format);
	ret = AsciiVSPrint(str, size, ascii_format, args);
	VA_END(args);

	free(ascii_format);
	return (int)ret;
}

char* strchr(const char* s, int c)
{
	do {
		if (*s == c)
			return (char*)s;
	} while (*s++);

	return NULL;
}

char* strrchr(const char* s, int c)
{
	char* p = NULL;
	do {
		if (*s == c)
			p = (char*)s;
	} while (*s++);

	return p;
}

char* strstr(const char* s1, const char* s2)
{
	const char* p1 = s1;
	const char* p2 = s2;
	const char* r = *p2 == 0 ? s1 : 0;

	while (*p1 != 0 && *p2 != 0) {
		if (*p1 == *p2) {
			if (r == 0)
				r = p1;
			p2++;
		} else {
			p2 = s2;
			if (r != 0)
				p1 = r + 1;
			if (*p1 == *p2) {
				r = p1;
				p2++;
			} else {
				r = 0;
			}
		}
		p1++;
	}

	return *p2 == 0 ? (char*)r : NULL;
}

char* strerror(int errnum)
{
	static char default_message[] = "Unknown error code 000";

	/* Mostly taken from ReactOS */
	switch (errnum) {
	case 0:
		return "No Error";
	case EPERM:
		return "Operation not permitted (EPERM)";
	case ENOENT:
		return "No such file or directory (ENOENT)";
	case ESRCH:
		return "No such process (ESRCH)";
	case EINTR:
		return "Interrupted system call (EINTR)";
	case EIO:
		return "Input or output error (EIO)";
	case ENXIO:
		return "No such device or address (ENXIO)";
	case E2BIG:
		return "Argument list too long (E2BIG)";
	case ENOEXEC:
		return "Unable to execute file (ENOEXEC)";
	case EBADF:
		return "Bad file descriptor (EBADF)";
	case ECHILD:
		return "No child processes (ECHILD)";
	case EAGAIN:
		return "Resource temporarily unavailable (EAGAIN)";
	case ENOMEM:
		return "Not enough memory (ENOMEM)";
	case EACCES:
		return "Permission denied (EACCES)";
	case EFAULT:
		return "Bad address (EFAULT)";
	case EBUSY:
		return "Resource busy (EBUSY)";
	case EEXIST:
		return "File exists (EEXIST)";
	case EXDEV:
		return "Improper link (EXDEV)";
	case ENODEV:
		return "No such device (ENODEV)";
	case ENOTDIR:
		return "Not a directory (ENOTDIR)";
	case EISDIR:
		return "Is a directory (EISDIR)";
	case EINVAL:
		return "Invalid argument (EINVAL)";
	case ENFILE:
	case EMFILE:
		return "Too many open files (ENFILE/EMFILE)";
	case ENOTTY:
		return "Inappropriate I/O control operation (ENOTTY)";
	case EFBIG:
		return "File too large (EFBIG)";
	case ENOSPC:
		return "No space left on drive (ENOSPC)";
	case ESPIPE:
		return "Invalid seek (ESPIPE)";
	case EROFS:
		return "Read-only file system (EROFS)";
	case EMLINK:
		return "Too many links (EMLINK)";
	case EPIPE:
		return "Broken pipe (EPIPE)";
	case EDOM:
	case ERANGE:
		return "Input or output to function out of range (EDOM/ERANGE)";
	case EDEADLK:
		return "Resource deadlock (EDEADLK)";
	case ENAMETOOLONG:
		return "File name too long (ENAMETOOLONG)";
	case ENOLCK:
		return "No locks available (ENOLCK)";
	case ENOSYS:
		return "Function not implemented (ENOSYS)";
	case ENOTEMPTY:
		return "Directory not empty (ENOTEMPTY)";
	case EILSEQ:
		return "Illegal byte sequence (EILSEQ)";
	default:
		errnum &= 0xff;
		default_message[19] = (errnum / 100) + '0';
		errnum %= 100;
		default_message[20] = (errnum / 10) + '0';
		default_message[21] = (errnum % 10) + '0';
		return default_message;
	}
}



/*
 * Returns the current time in a timespec struct.
 */
int clock_gettime(clockid_t clk_id, struct timespec* now)
{
	EFI_TIME Time;
	EFI_STATUS Status;

	now->tv_sec = 0;
	now->tv_nsec = 0;

	if (clk_id != CLOCK_REALTIME) {
		ntfs_log_perror("%s: Unsupported clock id %d", __FUNCTION__, clk_id);
		errno = ENOSYS;
		return -1;
	}

	Status = gRT->GetTime(&Time, NULL);
	if (EFI_ERROR(Status)) {
		ntfs_log_perror("%s: Failed to get the time (%r)", __FUNCTION__, Status);
		errno = EIO;
		return -1;
	}

	now->tv_sec = EfiTimeToUnixTime(&Time);
	now->tv_nsec = Time.Nanosecond;
	return 0;
}