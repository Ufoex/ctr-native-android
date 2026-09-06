#ifndef NATIVE_WIN32_H
#define NATIVE_WIN32_H

#if defined(_WIN32) || defined(__CYGWIN__)

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN 1
#endif

#ifndef NOMINMAX
#define NOMINMAX 1
#endif

#ifndef NOUSER
#define NOUSER 1
#endif

#include <windows.h>

/*
 * ENet (externals/enet) needs <winsock2.h> later in this same unity build,
 * and its structs use the `far`/`near` pointer qualifiers Windows still
 * defines. Pull it in now, while those macros are intact, so its own
 * #include <winsock2.h> further down the translation unit becomes a no-op
 * against its include guard - undef'ing far/near below would otherwise
 * turn every `far *` in ws2def.h into a syntax error.
 */
#include <winsock2.h>

/*
 * This project is built as one large C translation unit. Keep Win32's ANSI
 * name-selection macros from rewriting PS1 SDK/game function names later.
 */
#ifdef OpenEvent
#undef OpenEvent
#endif

#ifdef LoadImage
#undef LoadImage
#endif

#ifdef far
#undef far
#endif

#ifdef near
#undef near
#endif

#endif

#endif
