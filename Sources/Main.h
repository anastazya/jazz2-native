﻿#pragma once

// Set default name and version if not provided by CMake
/** @brief Application name */
#if !defined(NCINE_APP)
#	define NCINE_APP "jazz2"
#endif
/** @brief Application full name */
#if !defined(NCINE_APP_NAME)
#	define NCINE_APP_NAME "Jazz² Resurrection"
#endif
/** @brief Application version */
#if !defined(NCINE_VERSION)
#	define NCINE_VERSION "3.4.0"
#endif
/** @brief Application build year */
#if !defined(NCINE_BUILD_YEAR)
#	define NCINE_BUILD_YEAR "2025"
#endif
/** @brief Application package name on Linux */
#if !defined(NCINE_LINUX_PACKAGE)
#	define NCINE_LINUX_PACKAGE NCINE_APP_NAME
#endif

// Prefer local version of shared libraries in CMake build
#if defined(CMAKE_BUILD) && defined(__has_include)
#	if __has_include("../Shared/Common.h")
#		define __HAS_LOCAL_COMMON
#	endif
#endif
#ifdef __HAS_LOCAL_COMMON
#	include "../Shared/Common.h"
#	include "../Shared/Asserts.h"
#else
#	include <Common.h>
#	include <Asserts.h>
#endif

#include <stdlib.h>

/** @brief Install prefix on Unix systems, usually `"/usr/local"` */
#if (!defined(NCINE_INSTALL_PREFIX) && defined(DEATH_TARGET_UNIX)) || defined(DOXYGEN_GENERATING_OUTPUT)
#	define NCINE_INSTALL_PREFIX "/usr/local"
#endif

// Check platform-specific capabilities
/** @brief Whether the current platform supports a gamepad rumble, see @relativeref{nCine,IInputManager::joystickRumble()} */
#if defined(WITH_SDL) || defined(DEATH_TARGET_ANDROID) || defined(DEATH_TARGET_WINDOWS_RT) || defined(DOXYGEN_GENERATING_OUTPUT)
#	define NCINE_HAS_GAMEPAD_RUMBLE
#endif
/** @brief Whether the current platform has a native (hardware) back button */
#if defined(DEATH_TARGET_ANDROID) || defined(DOXYGEN_GENERATING_OUTPUT)
#	define NCINE_HAS_NATIVE_BACK_BUTTON
#endif
/** @brief Whether the current platform has non-fullscreen windows */
#if !defined(DEATH_TARGET_ANDROID) && !defined(DEATH_TARGET_IOS) && !defined(DEATH_TARGET_SWITCH)
#	define NCINE_HAS_WINDOWS
#endif

/** @brief Function name */
#if defined(__DEATH_CURRENT_FUNCTION)
#	define NCINE_CURRENT_FUNCTION __DEATH_CURRENT_FUNCTION
#else
#	define NCINE_CURRENT_FUNCTION ""
#endif

#ifndef DOXYGEN_GENERATING_OUTPUT

// Return assert macros
#define RETURN_ASSERT(x) do { if DEATH_UNLIKELY(!(x)) { LOGE("RETURN_ASSERT(" #x ")"); return; } } while (false)

// Return false assert macros
#define RETURNF_ASSERT(x) do { if DEATH_UNLIKELY(!(x)) { LOGE("RETURNF_ASSERT(" #x ")"); return false; } } while (false)

// Fatal assert macros
#define FATAL_ASSERT(x)						\
	do {									\
		if DEATH_UNLIKELY(!(x)) {			\
			LOGF("FATAL_ASSERT(" #x ")");	\
			DEATH_ASSERT_BREAK();			\
		}									\
	} while (false)

#define FATAL_ASSERT_MSG(x, fmt, ...)		\
	do {									\
		if DEATH_UNLIKELY(!(x)) {			\
			LOGF(fmt, ##__VA_ARGS__);		\
			DEATH_ASSERT_BREAK();			\
		}									\
	} while (false)

#endif
