#pragma once

#ifndef WITH_WEBRTC
#if defined(WEBRTC_WIN) || defined(WEBRTC_POSX) || defined(WEBRTC_ORBIS)
#define WITH_WEBRTC 1
#endif
#endif

#if WITH_WEBRTC

#include "Windows/AllowWindowsPlatformTypes.h"
#include "Windows/AllowWindowsPlatformAtomics.h"
THIRD_PARTY_INCLUDES_START
// WebRTC requires `htonll` to be defined, which depends either on `NTDDI_VERION` value or
// `INCL_EXTRA_HTON_FUNCTIONS` to be defined
#if !defined(INCL_EXTRA_HTON_FUNCTIONS)
#	if defined(UNDEF_INCL_EXTRA_HTON_FUNCTIONS)
#		pragma message(": Error: `UNDEF_INCL_EXTRA_HTON_FUNCTIONS` already defined, use another name")
#	endif
#	define UNDEF_INCL_EXTRA_HTON_FUNCTIONS
#	define INCL_EXTRA_HTON_FUNCTIONS
#endif
// C4582/3: constructor/desctructor is not implicitly called in "api/rtcerror.h", treated as an error by UE4
// for some unknown reasons we have to disable it inside those UE4's windows-related includes
// C6323: Use of arithmetic operator on Boolean type(s).
#pragma warning(push)
#pragma warning(disable: 4582 4583 6323)
#include "rtc_base/win32.h"
#include "rtc_base/win32socketinit.h"
#include "rtc_base/win32socketserver.h"
// uses Win32 Interlocked* functions
#include "rtc_base/refcountedobject.h"
#pragma warning(pop)
#if defined(UNDEF_INCL_EXTRA_HTON_FUNCTIONS)
#	undef UNDEF_INCL_EXTRA_HTON_FUNCTIONS
#	undef INCL_EXTRA_HTON_FUNCTIONS
#endif
THIRD_PARTY_INCLUDES_END
#include "Windows/HideWindowsPlatformAtomics.h"
#include "Windows/HideWindowsPlatformTypes.h"
THIRD_PARTY_INCLUDES_START
// C4582: constructor is not implicitly called in "api/rtcerror.h", treated as an error by UE4
// C6319: Use of the comma-operator in a tested expression causes the left argument to be ignored when it has no side-effects.
// C6323: Use of arithmetic operator on Boolean type(s).
#pragma warning(push)
#pragma warning(disable: 4582 6319 6323)
// WebRTC include
#include "common_audio/vad/include/webrtc_vad.h"
#pragma warning(pop)
// because WebRTC uses STL
#include <string>
#include <memory>
THIRD_PARTY_INCLUDES_END

#endif