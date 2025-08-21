// Copyright © 2013-2024 Google Inc. All Rights Reserved.
// Copyright © 2024 Dan R.
//
// Permission is hereby granted, free of charge, to any person obtaining a
// copy of this software and associated documentation files (the "Software"),
// to deal in the Software without restriction, including without limitation
// the rights to use, copy, modify, merge, publish, distribute, sublicense,
// and/or sell copies of the Software, and to permit persons to whom the
// Software is furnished to do so, subject to the following conditions:
//
// The above copyright notice and this permission notice shall be included
// in all copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
// THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
// FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
// DEALINGS IN THE SOFTWARE.

#ifndef BACKWARD_INCLUDED
#define BACKWARD_INCLUDED

#include "../CommonBase.h"
#include "../Containers/DateTime.h"
#include "../IO/Stream.h"

#if DEATH_CXX_STANDARD >= 201703L
#	define BACKWARD_ATLEAST_CXX17
#endif

#if !(defined(BACKWARD_TARGET_LINUX) || defined(BACKWARD_TARGET_APPLE) || defined(BACKWARD_TARGET_WINDOWS))
#	if defined(__linux) || defined(__linux__)
#		define BACKWARD_TARGET_LINUX
#	elif defined(DEATH_TARGET_APPLE)
#		define BACKWARD_TARGET_APPLE
#	elif defined(DEATH_TARGET_WINDOWS)
#		define BACKWARD_TARGET_WINDOWS
#	endif
#endif

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <new>
#include <streambuf>
#include <string>
#include <vector>
#include <exception>
#include <iterator>

#if defined(BACKWARD_TARGET_LINUX)

// On linux, backtrace can back-trace or "walk" the stack using the following
// libraries:
//
// #define BACKWARD_HAS_UNWIND
//  - unwind comes from libgcc, but I saw an equivalent inside clang itself.
//  - with unwind, the stacktrace is as accurate as it can possibly be, since
//  this is used by the C++ runtime in gcc/clang for stack unwinding on
//  exception.
//  - normally libgcc is already linked to your program by default.
//
// #define BACKWARD_HAS_LIBUNWIND
//  - libunwind provides, in some cases, a more accurate stacktrace as it knows
//  to decode signal handler frames and lets us edit the context registers when
//  unwinding, allowing stack traces over bad function references.
//
// #define BACKWARD_HAS_BACKTRACE
//  - backtrace seems to be a little bit more portable than libunwind, but on
//  linux, it uses unwind anyway, but abstract away a tiny information that is
//  sadly really important in order to get perfectly accurate stack traces.
//  - backtrace is part of the (e)glib library.
//
// The default is:
// #define BACKWARD_HAS_UNWIND
//
// Note that only one of the define should be defined at a time.
//
#if !defined(BACKWARD_HAS_UNWIND) && !defined(BACKWARD_HAS_LIBUNWIND) && !defined(BACKWARD_HAS_BACKTRACE)
#	define BACKWARD_HAS_UNWIND
#endif

// On linux, backward can extract detailed information about a stack trace
// using one of the following libraries:
//
// #define BACKWARD_HAS_DW
//  - libdw gives you the most juicy details out of your stack traces:
//    - object filename
//    - function name
//    - source filename
//    - line and column numbers
//    - source code snippet (assuming the file is accessible)
//    - variable names (if not optimized out)
//    - variable values (not supported by backward-cpp)
//  - You need to link with the lib "dw":
//    - apt-get install libdw-dev
//    - g++/clang++ -ldw ...
//
// #define BACKWARD_HAS_BFD
//  - With libbfd, you get a fair amount of details:
//    - object filename
//    - function name
//    - source filename
//    - line numbers
//    - source code snippet (assuming the file is accessible)
//  - You need to link with the lib "bfd":
//    - apt-get install binutils-dev
//    - g++/clang++ -lbfd ...
//
// #define BACKWARD_HAS_DWARF
//  - libdwarf gives you the most juicy details out of your stack traces:
//    - object filename
//    - function name
//    - source filename
//    - line and column numbers
//    - source code snippet (assuming the file is accessible)
//    - variable names (if not optimized out)
//    - variable values (not supported by backward-cpp)
//  - You need to link with the lib "dwarf":
//    - apt-get install libdwarf-dev
//    - g++/clang++ -ldwarf ...
//
// #define BACKWARD_HAS_BACKTRACE_SYMBOL
//  - backtrace provides minimal details for a stack trace:
//    - object filename
//    - function name
//  - backtrace is part of the (e)glib library.
//
// The default is:
// #define BACKWARD_HAS_BACKTRACE_SYMBOL
//
// Note that only one of the define should be defined at a time.
//
#if !defined(BACKWARD_HAS_DW) && !defined(BACKWARD_HAS_BFD) && !defined(BACKWARD_HAS_DWARF) && !defined(BACKWARD_HAS_BACKTRACE_SYMBOL)
#	define BACKWARD_HAS_BACKTRACE_SYMBOL
#endif

#include <cxxabi.h>
#include <fcntl.h>
#if defined(DEATH_TARGET_ANDROID)
// Old Android API levels define _Unwind_Ptr in both link.h and unwind.h
// Rename the one in link.h as we are not going to be using it
#	define _Unwind_Ptr _Unwind_Ptr_Custom
#	include <link.h>
#	undef _Unwind_Ptr
#else
#	include <link.h>
#endif
#if defined(DEATH_TARGET_POWERPC)
// Linux kernel header required for the struct pt_regs definition to access the NIP (Next Instruction Pointer) register value
#	include <asm/ptrace.h>
#endif
#include <signal.h>
#include <sys/stat.h>
#include <syscall.h>
#include <unistd.h>
#if !defined(_GNU_SOURCE)
#	define _GNU_SOURCE
#	include <dlfcn.h>
#	include <string.h>
#	undef _GNU_SOURCE
#else
#	include <dlfcn.h>
#	include <string.h>
#endif

#if defined(BACKWARD_HAS_BFD)
// NOTE: defining PACKAGE{,_VERSION} is required before including bfd.h on some platforms, see also:
// https://sourceware.org/bugzilla/show_bug.cgi?id=14243
#	if !defined(PACKAGE)
#		define PACKAGE
#	endif
#	if !defined(PACKAGE_VERSION)
#		define PACKAGE_VERSION
#	endif
#	include <bfd.h>
#endif

#if defined(BACKWARD_HAS_DW)
#	include <dwarf.h>
#	include <elfutils/libdw.h>
#	include <elfutils/libdwfl.h>
#endif

#if defined(BACKWARD_HAS_DWARF)
#	include <algorithm>
#	include <dwarf.h>
#	include <libdwarf.h>
#	include <libelf.h>
#	include <map>
#endif

#if defined(BACKWARD_HAS_BACKTRACE) || defined(BACKWARD_HAS_BACKTRACE_SYMBOL)
#	include <execinfo.h>
#endif

#endif

#if defined(BACKWARD_TARGET_APPLE)
// On Darwin, backtrace can back-trace or "walk" the stack using the following
// libraries:
//
// #define BACKWARD_HAS_UNWIND
//  - unwind comes from libgcc, but I saw an equivalent inside clang itself.
//  - with unwind, the stacktrace is as accurate as it can possibly be, since
//  this is used by the C++ runtime in gcc/clang for stack unwinding on
//  exception.
//  - normally libgcc is already linked to your program by default.
//
// #define BACKWARD_HAS_LIBUNWIND
//  - libunwind comes from clang, which implements an API compatible version.
//  - libunwind provides, in some cases, a more accurate stacktrace as it knows
//  to decode signal handler frames and lets us edit the context registers when
//  unwinding, allowing stack traces over bad function references.
//
// #define BACKWARD_HAS_BACKTRACE
//  - backtrace is available by default, though it does not produce as much
//  information as another library might.
//
// The default is:
// #define BACKWARD_HAS_UNWIND
//
// Note that only one of the define should be defined at a time.
//
#if !defined(BACKWARD_HAS_UNWIND) && !defined(BACKWARD_HAS_BACKTRACE) && !defined(BACKWARD_HAS_LIBUNWIND)
#	define BACKWARD_HAS_UNWIND
#endif

// On Darwin, backward can extract detailed information about a stack trace
// using one of the following libraries:
//
// #define BACKWARD_HAS_BACKTRACE_SYMBOL
//  - backtrace provides minimal details for a stack trace:
//    - object filename
//    - function name
//
// The default is:
// #define BACKWARD_HAS_BACKTRACE_SYMBOL
//
#if !defined(BACKWARD_HAS_BACKTRACE_SYMBOL)
#	define BACKWARD_HAS_BACKTRACE_SYMBOL
#endif

#include <cxxabi.h>
#include <fcntl.h>
#include <pthread.h>
#include <signal.h>
#include <sys/stat.h>
#include <unistd.h>

#if defined(BACKWARD_HAS_BACKTRACE) || defined(BACKWARD_HAS_BACKTRACE_SYMBOL)
#	include <execinfo.h>
#endif
#endif

#if defined(BACKWARD_TARGET_WINDOWS)

#include <condition_variable>
#include <mutex>
#include <thread>

#include <basetsd.h>

#if defined(DEATH_TARGET_32BIT)
typedef std::int32_t ssize_t;
#else
typedef SSIZE_T ssize_t;
#endif

#include "../CommonWindows.h"
#include "../Containers/StringStl.h"
#include "../Utf8.h"

#include <psapi.h>
#include <signal.h>
#include <winioctl.h>

#if defined(DEATH_TARGET_MSVC)
#	pragma comment(lib, "psapi")
#	pragma comment(lib, "dbghelp")
#endif

// Comment / packing is from stackoverflow:
// https://stackoverflow.com/questions/6205981/windows-c-stack-trace-from-a-running-app/28276227#28276227
// Some versions of imagehlp.dll lack the proper packing directives themselves, so we need to do it.
#pragma pack(push, before_imagehlp, 8)
#define _IMAGEHLP64
#include <imagehlp.h>
#pragma pack(pop, before_imagehlp)

// TODO maybe these should be undefined somewhere else?
#undef BACKWARD_HAS_UNWIND
#undef BACKWARD_HAS_BACKTRACE
#if !defined(BACKWARD_HAS_PDB_SYMBOL)
#	define BACKWARD_HAS_PDB_SYMBOL
#endif

#endif

#if defined(BACKWARD_HAS_UNWIND)

#include <unwind.h>
// while gcc's unwind.h defines something like that:
//  extern _Unwind_Ptr _Unwind_GetIP (struct _Unwind_Context *);
//  extern _Unwind_Ptr _Unwind_GetIPInfo (struct _Unwind_Context *, int *);
//
// clang's unwind.h defines something like this:
//  uintptr_t _Unwind_GetIP(struct _Unwind_Context* __context);
//
// Even if the _Unwind_GetIPInfo can be linked to, it is not declared, worse we
// cannot just redeclare it because clang's unwind.h doesn't define _Unwind_Ptr
// anyway.
//
// Luckily we can play on the fact that the guard macros have a different name:
#if defined(__CLANG_UNWIND_H)
// In fact, this function still comes from libgcc (on my different linux boxes,
// clang links against libgcc).
#	include <inttypes.h>
extern "C" uintptr_t _Unwind_GetIPInfo(_Unwind_Context*, int*);
#endif

#endif // BACKWARD_HAS_UNWIND

#if defined(BACKWARD_HAS_LIBUNWIND)
#	define UNW_LOCAL_ONLY
#	include <libunwind.h>
#endif // BACKWARD_HAS_LIBUNWIND

#include <unordered_map>
#include <utility> // for std::swap

namespace Death { namespace Backward {
//###==##====#=====--==~--~=~- --- -- -  -  -   -

	namespace Implementation
	{
		template<typename K, typename V>
		struct Hashtable {
			typedef std::unordered_map<K, V> type;
		};

#if defined(BACKWARD_TARGET_WINDOWS)
		const char BackwardPathDelimiter[] = ";";
#else
		const char BackwardPathDelimiter[] = ":";
#endif

		template<typename T>
		struct rm_ptr {
			typedef T type;
		};

		template<typename T>
		struct rm_ptr<T*> {
			typedef T type;
		};

		template<typename T>
		struct rm_ptr<const T*> {
			typedef const T type;
		};

		template<typename R, typename T, R(*F)(T)>
		struct Deleter {
			template <typename U> void operator()(U& ptr) const {
				(*F)(ptr);
			}
		};

		template<typename T>
		struct DefaultDelete {
			void operator()(T& ptr) const {
				delete ptr;
			}
		};

		template<typename T, typename Deleter = Deleter<void, void*, &::free>>
		class Handle {
			struct dummy;
			T _val;
			bool _empty;

			Handle(const Handle&) = delete;
			Handle& operator=(const Handle&) = delete;

		public:
			~Handle() {
				if (!_empty) {
					Deleter()(_val);
				}
			}

			explicit Handle() : _val(), _empty(true) {}
			explicit Handle(T val) : _val(val), _empty(false) {
				if (!_val)
					_empty = true;
			}

			Handle(Handle&& from) noexcept : _empty(true) {
				swap(from);
			}
			Handle& operator=(Handle&& from) noexcept {
				swap(from);
				return *this;
			}

			void reset(T newValue) {
				Handle tmp(newValue);
				swap(tmp);
			}

			void update(T newValue) {
				_val = newValue;
				_empty = !static_cast<bool>(newValue);
			}

			operator const dummy* () const {
				if (_empty) {
					return nullptr;
				}
				return reinterpret_cast<const dummy*>(_val);
			}
			T get() {
				return _val;
			}
			T release() {
				_empty = true;
				return _val;
			}
			void swap(Handle& b) {
				using std::swap;
				swap(b._val, _val);     // can throw, we are safe here.
				swap(b._empty, _empty); // should not throw: if you cannot swap two
				// bools without throwing... It's a lost cause anyway!
			}

			T& operator->() {
				return _val;
			}
			const T& operator->() const {
				return _val;
			}

			typedef typename rm_ptr<T>::type& ref_t;
			typedef const typename rm_ptr<T>::type& const_ref_t;

			ref_t operator*() {
				return *_val;
			}
			const_ref_t operator*() const {
				return *_val;
			}
			ref_t operator[](std::size_t idx) {
				return _val[idx];
			}

			// Watch out, we've got a badass over here
			T* operator&() {
				_empty = false;
				return &_val;
			}
		};

#if defined(BACKWARD_TARGET_LINUX) || defined(BACKWARD_TARGET_APPLE)
		struct Demangler {
			Demangler() : _demangleBufferLength(0) {}

			std::string Demangle(const char* funcName) {
				using namespace Implementation;
				char* result = abi::__cxa_demangle(funcName, _demangleBuffer.get(), &_demangleBufferLength, nullptr);
				if (result != nullptr) {
					_demangleBuffer.update(result);
					return result;
				}
				return funcName;
			}

		private:
			Implementation::Handle<char*> _demangleBuffer;
			std::size_t _demangleBufferLength;
		};
#else
		// Default demangler implementation (do nothing)
		struct Demangler {
			static std::string Demangle(const char* funcname) {
				return funcname;
			}
		};
#endif

		// Split a string on the platform's PATH delimiter. Example: if delimiter is ":" then:
		//   ""              --> []
		//   ":"             --> ["",""]
		//   "::"            --> ["","",""]
		//   "/a/b/c"        --> ["/a/b/c"]
		//   "/a/b/c:/d/e/f" --> ["/a/b/c","/d/e/f"]
		//   etc.
		inline std::vector<std::string> SplitSourcePrefixes(const std::string& s) {
			std::vector<std::string> out;
			std::size_t last = 0;
			std::size_t next = 0;
			std::size_t delimiterSize = sizeof(BackwardPathDelimiter) - 1;
			while ((next = s.find(BackwardPathDelimiter, last)) != std::string::npos) {
				std::size_t length = next - last;
				if (length > 0) {
					out.push_back(s.substr(last, length));
				}
				last = next + delimiterSize;
			}
			if (last <= s.length()) {
				out.push_back(s.substr(last));
			}
			return out;
		}

	} // namespace Implementation

	/** @brief Raw trace item */
	struct Trace {
		void* Address;
		std::size_t Index;

		Trace() : Address(nullptr), Index(0) {}

		explicit Trace(void* addr, std::size_t idx) : Address(addr), Index(idx) {}
	};

	/** @brief Resolved trace item */
	struct ResolvedTrace : public Trace {

		/** @brief Source location description */
		struct SourceLoc {
			/** @brief Function name */
			std::string Function;
			/** @brief File name */
			std::string Filename;
			/** @brief Line */
			std::int32_t Line;
			/** @brief Column */
			std::int32_t Column;

			SourceLoc() : Line(0), Column(0) {}

			bool operator==(const SourceLoc& b) const {
				return Function == b.Function && Filename == b.Filename && Line == b.Line && Column == b.Column;
			}

			bool operator!=(const SourceLoc& b) const {
				return !(*this == b);
			}
		};

		// In which binary object this trace is located.
		std::string ObjectFilename;
		// Base address of the binary object
		void* ObjectBaseAddress;

		// The function in the object that contain the trace. This is not the same
		// as source.function which can be an function inlined in object_function.
		std::string ObjectFunction;

		// The source location of this trace. It is possible for filename to be
		// empty and for line/col to be invalid (value 0) if this information
		// couldn't be deduced, for example if there is no debug information in the
		// binary object.
		SourceLoc Source;

		// An optionals list of "inliners". All the successive sources location
		// from where the source location of the trace (the attribute right above)
		// is inlined. It is especially useful when you compiled with optimization.
		typedef std::vector<SourceLoc> source_locs_t;
		source_locs_t Inliners;

		ResolvedTrace() : Trace(), ObjectBaseAddress(nullptr) {}
		ResolvedTrace(const Trace& baseTrace) : Trace(baseTrace), ObjectBaseAddress(nullptr) {}
	};

	/** @brief Base class of stack trace */
	class StackTraceBase {
	public:
		StackTraceBase()
			: _threadId(0), _skip(0), _context(nullptr), _errorAddr(nullptr) {
		}

		std::size_t GetThreadId() const {
			return _threadId;
		}

		void SetSkipFrames(std::size_t n) {
			_skip = n;
		}

		std::size_t size() const {
			return (_stacktrace.size() >= GetSkipFrames())
				? _stacktrace.size() - GetSkipFrames()
				: 0;
		}
		Trace operator[](std::size_t idx) const {
			if (idx >= size()) {
				return Trace();
			}
			return Trace(_stacktrace[idx + GetSkipFrames()], idx);
		}
		void* const* begin() const {
			if (size()) {
				return &_stacktrace[GetSkipFrames()];
			}
			return nullptr;
		}

	protected:
#ifndef DOXYGEN_GENERATING_OUTPUT
		std::vector<void*> _stacktrace;

		std::size_t _threadId;
		std::size_t _skip;
		void* _context;
		void* _errorAddr;
#endif

		void LoadThreadInfo() {
#if defined(BACKWARD_TARGET_LINUX)
#	if !defined(DEATH_TARGET_ANDROID)
			_threadId = static_cast<std::size_t>(syscall(SYS_gettid));
#	else
			_threadId = static_cast<std::size_t>(gettid());
#	endif
			if (_threadId == static_cast<std::size_t>(getpid())) {
				// If the thread is the main one, let's hide that.
				_threadId = 0;
			}
#elif defined(BACKWARD_TARGET_APPLE)
			_threadId = reinterpret_cast<std::size_t>(pthread_self());
			if (pthread_main_np() == 1) {
				// If the thread is the main one, let's hide that.
				_threadId = 0;
			}
#endif
		}

		void SetContext(void* context) {
			_context = context;
		}
		void* GetContext() const {
			return _context;
		}

		void SetErrorAddress(void* errorAddr) {
			_errorAddr = errorAddr;
		}
		void* GetErrorAddress() const {
			return _errorAddr;
		}

		std::size_t GetSkipFrames() const {
			return _skip;
		}
	};

#if defined(BACKWARD_HAS_UNWIND)

	namespace Implementation
	{
		template<typename F> class Unwinder {
		public:
			std::size_t operator()(F& f, std::size_t depth) {
				_f = &f;
				_index = -1;
				_depth = depth;
				_Unwind_Backtrace(&this->BacktraceTrampoline, this);
				if (_index == -1) {
					// _Unwind_Backtrace has failed to obtain any backtraces
					return 0;
				} else {
					return static_cast<std::size_t>(_index);
				}
			}

		private:
			F* _f;
			ssize_t _index;
			std::size_t _depth;

			static _Unwind_Reason_Code BacktraceTrampoline(_Unwind_Context* ctx, void* self) {
				return (static_cast<Unwinder*>(self))->Backtrace(ctx);
			}

			_Unwind_Reason_Code Backtrace(_Unwind_Context* ctx) {
				if (_index >= 0 && static_cast<std::size_t>(_index) >= _depth) {
					return _URC_END_OF_STACK;
				}

				int ip_before_instruction = 0;
				std::uintptr_t ip = _Unwind_GetIPInfo(ctx, &ip_before_instruction);

				if (!ip_before_instruction) {
					// calculating 0-1 for unsigned, looks like a possible bug to sanitizers,
					// so let's do it explicitly:
					if (ip == 0) {
						ip = std::numeric_limits<std::uintptr_t>::max(); // set it to 0xffff... (as from casting 0-1)
					} else {
						ip -= 1; // else just normally decrement it (no overflow/underflow will happen)
					}
				}

				if (_index >= 0) { // ignore first frame.
					(*_f)(static_cast<std::size_t>(_index), reinterpret_cast<void*>(ip));
				}
				_index += 1;
				return _URC_NO_REASON;
			}
		};

		template<typename F>
		std::size_t Unwind(F f, std::size_t depth) {
			Unwinder<F> unwinder;
			return unwinder(f, depth);
		}

	} // namespace Implementation

	class StackTrace : public StackTraceBase {
	public:
		DEATH_NEVER_INLINE std::size_t LoadHere(std::size_t depth = 32, void* context = nullptr, void* errorAddr = nullptr) {
			LoadThreadInfo();
			SetContext(context);
			SetErrorAddress(errorAddr);
			if (depth == 0) {
				return 0;
			}
			_stacktrace.resize(depth);
			std::size_t count = Implementation::Unwind(callback(*this), depth);
			_stacktrace.resize(count);
			SetSkipFrames(0);
			return size();
		}
		std::size_t LoadFrom(void* addr, std::size_t depth = 32, void* context = nullptr, void* errorAddr = nullptr) {
			LoadHere(depth + 8, context, errorAddr);

			for (std::size_t i = 0; i < _stacktrace.size(); ++i) {
				if (_stacktrace[i] == addr) {
					SetSkipFrames(i);
					break;
				}
			}

			_stacktrace.resize(std::min(_stacktrace.size(), GetSkipFrames() + depth));
			return size();
		}

	private:
		struct callback {
			StackTrace& self;
			callback(StackTrace& _self) : self(_self) {}

			void operator()(std::size_t idx, void* addr) {
				self._stacktrace[idx] = addr;
			}
		};
	};

#elif defined(BACKWARD_HAS_LIBUNWIND)

	class StackTrace : public StackTraceBase {
	public:
		DEATH_NEVER_INLINE std::size_t LoadHere(std::size_t depth = 32, void* context_ = nullptr, void* errorAddr = nullptr) {
			SetContext(context_);
			SetErrorAddress(errorAddr);
			LoadThreadInfo();
			if (depth == 0) {
				return 0;
			}
			_stacktrace.resize(depth + 1);

			int result = 0;
			unw_context_t ctx;
			std::size_t index = 0;

			// Add the tail call. If the Instruction Pointer is the crash address it
			// means we got a bad function pointer dereference, so we "unwind" the
			// bad pointer manually by using the return address pointed to by the
			// Stack Pointer as the Instruction Pointer and letting libunwind do
			// the rest

			if (context()) {
				ucontext_t* uctx = reinterpret_cast<ucontext_t*>(GetContext());
#	if defined(REG_RIP)		// x86_64
				if (uctx->uc_mcontext.gregs[REG_RIP] == reinterpret_cast<greg_t>(GetErrorAddress())) {
					uctx->uc_mcontext.gregs[REG_RIP] = *reinterpret_cast<std::size_t*>(uctx->uc_mcontext.gregs[REG_RSP]);
				}
				_stacktrace[index] = reinterpret_cast<void*>(uctx->uc_mcontext.gregs[REG_RIP]);
				++index;
				ctx = *reinterpret_cast<unw_context_t*>(uctx);
#	elif defined(REG_EIP)	// x86_32
				if (uctx->uc_mcontext.gregs[REG_EIP] == reinterpret_cast<greg_t>(GetErrorAddress())) {
					uctx->uc_mcontext.gregs[REG_EIP] = *reinterpret_cast<std::size_t*>(uctx->uc_mcontext.gregs[REG_ESP]);
				}
				_stacktrace[index] = reinterpret_cast<void*>(uctx->uc_mcontext.gregs[REG_EIP]);
				++index;
				ctx = *reinterpret_cast<unw_context_t*>(uctx);
#	elif defined(__arm__)	// clang libunwind/arm
				// libunwind uses its own context type for ARM unwinding.
				// Copy the registers from the signal handler's context so we can unwind
				unw_getcontext(&ctx);
				ctx.regs[UNW_ARM_R0] = uctx->uc_mcontext.arm_r0;
				ctx.regs[UNW_ARM_R1] = uctx->uc_mcontext.arm_r1;
				ctx.regs[UNW_ARM_R2] = uctx->uc_mcontext.arm_r2;
				ctx.regs[UNW_ARM_R3] = uctx->uc_mcontext.arm_r3;
				ctx.regs[UNW_ARM_R4] = uctx->uc_mcontext.arm_r4;
				ctx.regs[UNW_ARM_R5] = uctx->uc_mcontext.arm_r5;
				ctx.regs[UNW_ARM_R6] = uctx->uc_mcontext.arm_r6;
				ctx.regs[UNW_ARM_R7] = uctx->uc_mcontext.arm_r7;
				ctx.regs[UNW_ARM_R8] = uctx->uc_mcontext.arm_r8;
				ctx.regs[UNW_ARM_R9] = uctx->uc_mcontext.arm_r9;
				ctx.regs[UNW_ARM_R10] = uctx->uc_mcontext.arm_r10;
				ctx.regs[UNW_ARM_R11] = uctx->uc_mcontext.arm_fp;
				ctx.regs[UNW_ARM_R12] = uctx->uc_mcontext.arm_ip;
				ctx.regs[UNW_ARM_R13] = uctx->uc_mcontext.arm_sp;
				ctx.regs[UNW_ARM_R14] = uctx->uc_mcontext.arm_lr;
				ctx.regs[UNW_ARM_R15] = uctx->uc_mcontext.arm_pc;

				// If we have crashed in the PC use the LR instead, as this was a bad function dereference
				if (reinterpret_cast<unsigned long>(GetErrorAddress()) == uctx->uc_mcontext.arm_pc) {
					ctx.regs[UNW_ARM_R15] = uctx->uc_mcontext.arm_lr - sizeof(unsigned long);
				}
				_stacktrace[index] = reinterpret_cast<void*>(ctx.regs[UNW_ARM_R15]);
				++index;
#	elif defined(__aarch64__)	// gcc libunwind/arm64
				unw_getcontext(&ctx);
				// If the IP is the same as the crash address we have a bad function
				// dereference The caller's address is pointed to by the link pointer, so
				// we dereference that value and set it to be the next frame's IP.
				if (uctx->uc_mcontext.pc == reinterpret_cast<__uint64_t>(GetErrorAddress())) {
					uctx->uc_mcontext.pc = uctx->uc_mcontext.regs[UNW_TDEP_IP];
				}

				// 29 general purpose registers
				for (int i = UNW_AARCH64_X0; i <= UNW_AARCH64_X28; i++) {
					ctx.uc_mcontext.regs[i] = uctx->uc_mcontext.regs[i];
				}
				ctx.uc_mcontext.sp = uctx->uc_mcontext.sp;
				ctx.uc_mcontext.pc = uctx->uc_mcontext.pc;
				ctx.uc_mcontext.fault_address = uctx->uc_mcontext.fault_address;
				_stacktrace[index] = reinterpret_cast<void*>(ctx.uc_mcontext.pc);
				++index;
#	elif defined(DEATH_TARGET_APPLE) && defined(__x86_64__)
				unw_getcontext(&ctx);
				// OS X's implementation of libunwind uses its own context object so we need to convert the passed context
				// to libunwind's format (information about the data layout taken from unw_getcontext.s in Apple's libunwind source
				ctx.data[0] = uctx->uc_mcontext->__ss.__rax;
				ctx.data[1] = uctx->uc_mcontext->__ss.__rbx;
				ctx.data[2] = uctx->uc_mcontext->__ss.__rcx;
				ctx.data[3] = uctx->uc_mcontext->__ss.__rdx;
				ctx.data[4] = uctx->uc_mcontext->__ss.__rdi;
				ctx.data[5] = uctx->uc_mcontext->__ss.__rsi;
				ctx.data[6] = uctx->uc_mcontext->__ss.__rbp;
				ctx.data[7] = uctx->uc_mcontext->__ss.__rsp;
				ctx.data[8] = uctx->uc_mcontext->__ss.__r8;
				ctx.data[9] = uctx->uc_mcontext->__ss.__r9;
				ctx.data[10] = uctx->uc_mcontext->__ss.__r10;
				ctx.data[11] = uctx->uc_mcontext->__ss.__r11;
				ctx.data[12] = uctx->uc_mcontext->__ss.__r12;
				ctx.data[13] = uctx->uc_mcontext->__ss.__r13;
				ctx.data[14] = uctx->uc_mcontext->__ss.__r14;
				ctx.data[15] = uctx->uc_mcontext->__ss.__r15;
				ctx.data[16] = uctx->uc_mcontext->__ss.__rip;

				// If the IP is the same as the crash address we have a bad function dereference The caller's address
				// is pointed to by %rsp, so we dereference that value and set it to be the next frame's IP.
				if (uctx->uc_mcontext->__ss.__rip == reinterpret_cast<__uint64_t>(GetErrorAddress())) {
					ctx.data[16] = *reinterpret_cast<__uint64_t*>(uctx->uc_mcontext->__ss.__rsp);
				}
				_stacktrace[index] = reinterpret_cast<void*>(ctx.data[16]);
				++index;
#	elif defined(DEATH_TARGET_APPLE)
				unw_getcontext(&ctx);
				// TODO: Convert the ucontext_t to libunwind's unw_context_t like we do in 64 bits
				if (ctx.uc_mcontext->__ss.__eip == reinterpret_cast<greg_t>(GetErrorAddress())) {
					ctx.uc_mcontext->__ss.__eip = ctx.uc_mcontext->__ss.__esp;
				}
				_stacktrace[index] = reinterpret_cast<void*>(ctx.uc_mcontext->__ss.__eip);
				++index;
#	endif
			}

			unw_cursor_t cursor;
			if (GetContext()) {
#	if defined(UNW_INIT_SIGNAL_FRAME)
				result = unw_init_local2(&cursor, &ctx, UNW_INIT_SIGNAL_FRAME);
#	else
				result = unw_init_local(&cursor, &ctx);
#	endif
			} else {
				unw_getcontext(&ctx);
				result = unw_init_local(&cursor, &ctx);
			}

			if (result != 0) {
				return 1;
			}

			unw_word_t ip = 0;

			while (index <= depth && unw_step(&cursor) > 0) {
				result = unw_get_reg(&cursor, UNW_REG_IP, &ip);
				if (result == 0) {
					_stacktrace[index] = reinterpret_cast<void*>(--ip);
					++index;
				}
			}
			--index;

			_stacktrace.resize(index + 1);
			SetSkipFrames(0);
			return size();
		}

		std::size_t LoadLrom(void* addr, std::size_t depth = 32, void* context = nullptr, void* errorAddr = nullptr) {
			LoadHere(depth + 8, context, errorAddr);

			for (std::size_t i = 0; i < _stacktrace.size(); ++i) {
				if (_stacktrace[i] == addr) {
					SetSkipFrames(i);
					_stacktrace[i] = (void*)((std::uintptr_t)_stacktrace[i]);
					break;
				}
			}

			_stacktrace.resize(std::min(_stacktrace.size(), GetSkipFrames() + depth));
			return size();
		}
	};

#elif defined(BACKWARD_HAS_BACKTRACE)

	class StackTrace : public StackTraceBase {
	public:
		DEATH_NEVER_INLINE std::size_t LoadHere(std::size_t depth = 32, void* context = nullptr, void* errorAddr = nullptr) {
			SetContext(context);
			SetErrorAddress(errorAddr);
			LoadThreadInfo();
			if (depth == 0) {
				return 0;
			}
			_stacktrace.resize(depth + 1);
			std::size_t count = backtrace(&_stacktrace[0], _stacktrace.size());
			_stacktrace.resize(count);
			SetSkipFrames(1);
			return size();
		}

		std::size_t LoadFrom(void* addr, std::size_t depth = 32, void* context = nullptr, void* errorAddr = nullptr) {
			LoadHere(depth + 8, context, errorAddr);

			for (std::size_t i = 0; i < _stacktrace.size(); ++i) {
				if (_stacktrace[i] == addr) {
					SetSkipFrames(i);
					_stacktrace[i] = (void*)((std::uintptr_t)_stacktrace[i] + 1);
					break;
				}
			}

			_stacktrace.resize(std::min(_stacktrace.size(), GetSkipFrames() + depth));
			return size();
		}
	};

#elif defined(BACKWARD_TARGET_WINDOWS)

	struct ExceptionContext {
		CONTEXT Context;
		EXCEPTION_RECORD ExceptionRecord;
	};

	class StackTrace : public StackTraceBase {
	public:
		// We have to load the machine type from the image info
		// So we first initialize the resolver, and it tells us this info
		void SetMachineType(DWORD machine_type) {
			_machineType = machine_type;
		}
		void SetContext(CONTEXT* ctx) {
			_ctx = ctx;
		}
		void SetThreadHandle(HANDLE handle) {
			_thread = handle;
		}

		DEATH_NEVER_INLINE std::size_t LoadHere(std::size_t depth = 32, void* context = nullptr, void* errorAddr = nullptr) {
			SetContext(&(static_cast<ExceptionContext*>(context)->Context));
			SetErrorAddress(errorAddr);
			
			/*if (context == nullptr) {
				_stacktrace.resize(depth);
				const WORD capturedFrames = ::RtlCaptureStackBackTrace(0, DWORD(depth), _stacktrace.data(), NULL);
				_stacktrace.resize(capturedFrames);
				return capturedFrames;
			}*/

			CONTEXT localCtx; // Used when no context is provided

			if (depth == 0) {
				return 0;
			}

			if (_ctx == nullptr) {
				_ctx = &localCtx;
#	if (defined(_M_IX86) || defined(__i386__)) && defined(DEATH_TARGET_MSVC)
				// RtlCaptureContext() doesn't work on i386
				std::memset(&localCtx, 0, sizeof(CONTEXT));
				localCtx.ContextFlags = CONTEXT_CONTROL;
				__asm {
				label:
					mov[localCtx.Ebp], ebp;
					mov[localCtx.Esp], esp;
					mov eax, [label];
					mov[localCtx.Eip], eax;
				}
#	else
				::RtlCaptureContext(_ctx);
#	endif
			}

			if (_thread == NULL) {
				_thread = ::GetCurrentThread();
			}

			_threadId = (std::size_t)::GetThreadId(_thread);

			HANDLE process = ::GetCurrentProcess();

			STACKFRAME64 s;
			std::memset(&s, 0, sizeof(STACKFRAME64));

			s.AddrStack.Mode = AddrModeFlat;
			s.AddrFrame.Mode = AddrModeFlat;
			s.AddrPC.Mode = AddrModeFlat;
#	if defined(_M_X64) || defined(__x86_64__)
			s.AddrPC.Offset = _ctx->Rip;
			s.AddrStack.Offset = _ctx->Rsp;
			s.AddrFrame.Offset = _ctx->Rbp;
#	elif defined(_M_ARM64) || defined(_M_ARM64EC)
			s.AddrPC.Offset = _ctx->Pc;
			s.AddrStack.Offset = _ctx->Sp;
			s.AddrFrame.Offset = _ctx->Fp;
#	elif defined(_M_ARM)
			s.AddrPC.Offset = _ctx->Pc;
			s.AddrStack.Offset = _ctx->Sp;
			s.AddrFrame.Offset = _ctx->R11;
#	elif defined(_M_IA64)
			s.AddrBStore.Mode = AddrModeFlat;
			s.AddrPC.Offset = _ctx->StIIP;
			s.AddrFrame.Offset = _ctx->IntSp;
			s.AddrBStore.Offset = _ctx->RsBSP;
			s.AddrStack.Offset = _ctx->IntSp;
#	else
			s.AddrPC.Offset = _ctx->Eip;
			s.AddrStack.Offset = _ctx->Esp;
			s.AddrFrame.Offset = _ctx->Ebp;
#	endif

			if (_machineType == 0) {
#	if defined(_M_X64) || defined(__x86_64__)
				_machineType = IMAGE_FILE_MACHINE_AMD64;
#	elif defined(_M_ARM64) || defined(_M_ARM64EC)
				_machineType = IMAGE_FILE_MACHINE_ARM64;
#	elif defined(_M_ARM)
				_machineType = IMAGE_FILE_MACHINE_ARM;
#	elif defined(_M_IA64)
				_machineType = IMAGE_FILE_MACHINE_IA64;
#	else
				_machineType = IMAGE_FILE_MACHINE_I386;
#	endif
			}

			while (true) {
				// NOTE: This only works if PDBs are already loaded!
				::SetLastError(0);
				if (!::StackWalk64(_machineType, process, _thread, &s,
					_machineType == IMAGE_FILE_MACHINE_I386 ? NULL : _ctx, NULL,
					::SymFunctionTableAccess64, ::SymGetModuleBase64, NULL)) {
					break;
				}

				if (s.AddrReturn.Offset == 0) {
					break;
				}

				_stacktrace.push_back(reinterpret_cast<void*>(s.AddrPC.Offset - 1));

				if (size() >= depth) {
					break;
				}
			}

			return size();
		}

		std::size_t LoadFrom(void* addr, std::size_t depth = 32, void* context = nullptr, void* errorAddr = nullptr) {
			LoadHere(depth + 8, context, errorAddr);

			for (std::size_t i = 0; i < _stacktrace.size(); ++i) {
				if (_stacktrace[i] == addr) {
					SetSkipFrames(i);
					break;
				}
			}

			_stacktrace.resize(std::min(_stacktrace.size(), GetSkipFrames() + depth));
			return size();
		}

	private:
		DWORD _machineType = 0;
		HANDLE _thread = 0;
		CONTEXT* _ctx = nullptr;
	};

#endif

	/** @brief Base class for trace resolving */
	class TraceResolverBase {
	public:
		virtual ~TraceResolverBase() {}

		/** @brief Loads symbols from given addresses */
		virtual void LoadAddresses(void* const* addresses, std::int32_t addressCount) {
			(void)addresses;
			(void)addressCount;
		}

		/** @brief Loads symbols from a given stack trace */
		template<class ST>
		void LoadStacktrace(ST& st) {
			LoadAddresses(st.begin(), static_cast<std::int32_t>(st.size()));
		}

		/** @brief Resolves symbols in a given trace item */
		virtual ResolvedTrace Resolve(ResolvedTrace t) {
			return t;
		}

	protected:
		/** @brief Demangles the specified function name */
		std::string Demangle(const char* funcName) {
			return _demangler.Demangle(funcName);
		}

	private:
		Implementation::Demangler _demangler;
	};

#if defined(BACKWARD_TARGET_LINUX)

	class TraceResolverLinuxBase : public TraceResolverBase {
	public:
		TraceResolverLinuxBase()
			: _argv0(get_argv0()), _execPath(read_symlink("/proc/self/exe")) {}

		std::string resolve_exec_path(Dl_info& symbol_info) const {
			// Mutates symbol_info.dli_fname to be filename to open and returns filename to display
			if (symbol_info.dli_fname == _argv0) {
				// dladdr returns argv[0] in dli_fname for symbols contained in the main executable, which is not
				// a valid path if the executable was found by a search of the PATH environment variable; In that case,
				// we actually open /proc/self/exe, which is always the actual executable (even if it was deleted/replaced!),
				// but display the path that /proc/self/exe links to. However, this right away reduces probability
				// of successful symbol resolution, because libbfd may try to find *.debug files in the same dir,
				// in case symbols are stripped. As a result, it may try to find a file /proc/self/<exe_name>.debug,
				// which obviously does not exist. /proc/self/exe is a last resort. First load attempt should go
				// for the original executable file path.
				symbol_info.dli_fname = "/proc/self/exe";
				return _execPath;
			} else {
				return symbol_info.dli_fname;
			}
		}

	private:
		std::string _argv0;
		std::string _execPath;

		static std::string get_argv0() {
			std::string argv0;
			std::ifstream ifs("/proc/self/cmdline");
			std::getline(ifs, argv0, '\0');
			return argv0;
		}

		static std::string read_symlink(std::string const& symlink_path) {
			std::string path;
			path.resize(100);

			while (true) {
				ssize_t len = ::readlink(symlink_path.c_str(), &*path.begin(), path.size());
				if (len < 0) {
					return "";
				}
				if (static_cast<std::size_t>(len) == path.size()) {
					path.resize(path.size() * 2);
				} else {
					path.resize(static_cast<std::string::size_type>(len));
					break;
				}
			}

			return path;
		}
	};

#if defined(BACKWARD_HAS_BACKTRACE_SYMBOL)

	class TraceResolver : public TraceResolverLinuxBase {
	public:
		void LoadAddresses(void* const* addresses, std::int32_t addressCount) override {
			if (addressCount == 0) {
				return;
			}
			_symbols.reset(backtrace_symbols(addresses, addressCount));
		}

		ResolvedTrace Resolve(ResolvedTrace trace) override {
			char* filename = _symbols[trace.Index];
			char* funcname = filename;
			while (funcname[0] != '\0' && funcname[0] != '(') {
				funcname++;
			}
			trace.ObjectFilename.assign(filename, funcname); // ok even if funcname is the ending \0 (then we assign entire string)

			if (funcname[0] != '\0') { // if it's not end of string (e.g. from last frame ip==0)
				funcname++;
				char* funcname_end = funcname;
				while (*funcname_end && *funcname_end != ')' && *funcname_end != '+') {
					funcname_end++;
				}
				*funcname_end = '\0';
				trace.ObjectFunction = this->Demangle(funcname);
				trace.Source.Function = trace.ObjectFunction; // we cannot do better.
			}
			return trace;
		}

	private:
		Implementation::Handle<char**> _symbols;
	};

#endif // BACKWARD_HAS_BACKTRACE_SYMBOL

#if defined(BACKWARD_HAS_BFD)

	class TraceResolver : public TraceResolverLinuxBase {
	public:
		TraceResolver() : _bfd_loaded(false) {}

		ResolvedTrace Resolve(ResolvedTrace trace) override {
			Dl_info symbol_info;

			// trace.addr is a virtual address in memory pointing to some code.
			// Let's try to find from which loaded object it comes from. The loaded object can be yourself btw.
			if (!dladdr(trace.Address, &symbol_info)) {
				return trace; // dat broken trace...
			}

			// Now we get in symbol_info:
			// .dli_fname:
			//		pathname of the shared object that contains the address.
			// .dli_fbase:
			//		where the object is loaded in memory.
			// .dli_sname:
			//		the name of the nearest symbol to trace.addr, we expect a
			//		function name.
			// .dli_saddr:
			//		the exact address corresponding to .dli_sname.

			if (symbol_info.dli_sname) {
				trace.ObjectFunction = Demangle(symbol_info.dli_sname);
			}

			if (!symbol_info.dli_fname) {
				return trace;
			}

			trace.ObjectFilename = resolve_exec_path(symbol_info);
			bfd_fileobject* fobj;
			// Before rushing to resolution need to ensure the executable file still can be used. For that compare
			// inode numbers of what is stored by the executable's file path, and in the dli_fname, which not necessarily
			// equals to the executable. It can be a shared library, or /proc/self/exe, and in the latter case has drawbacks.
			// See the exec path resolution for details. In short - the dli object should be used only as the last resort.
			// If inode numbers are equal, it is known dli_fname and the executable file are the same. This is guaranteed
			// by Linux, because if the executable file is changed/deleted, it will be done in a new inode. The old file
			// will be preserved in "/proc/self/exe", and may even have inode 0. The latter can happen if the inode was
			// actually reused, and the file was kept only in the main memory.
			struct stat obj_stat;
			struct stat dli_stat;
			if (::stat(trace.ObjectFilename.c_str(), &obj_stat) == 0 &&
				::stat(symbol_info.dli_fname, &dli_stat) == 0 &&
				obj_stat.st_ino == dli_stat.st_ino) {
				// The executable file, and the shared object containing the  address are the same file. Safe to use
				// the original path. this is preferable. Libbfd will search for stripped debug symbols in the same directory.
				fobj = load_object_with_bfd(trace.ObjectFilename);
			} else {
				// The original object file was *deleted*! The only hope is that the debug symbols are either inside
				// the shared object file, or are in the same directory, and this is not /proc/self/exe.
				fobj = nullptr;
			}
			if (fobj == nullptr || !fobj->handle) {
				fobj = load_object_with_bfd(symbol_info.dli_fname);
				if (!fobj->handle) {
					return trace;
				}
			}

			find_sym_result* details_selected; // to be filled.

			// trace.addr is the next instruction to be executed after returning from the nested stack frame. In C++
			// this usually relate to the next statement right after the function call that leaded to a new stack
			// frame. This is not usually what you want to see when printing out a stacktrace...
			find_sym_result details_call_site = find_symbol_details(fobj, trace.Address, symbol_info.dli_fbase);
			details_selected = &details_call_site;

#	if defined(BACKWARD_HAS_UNWIND)
			// ...this is why we also try to resolve the symbol that is right before the return address. If we are lucky
			// enough, we will get the line of the function that was called. But if the code is optimized, we might
			// get something absolutely not related since the compiler can reschedule the return address with inline
			// functions and tail-call optimization (among other things that I don't even know or cannot even dream
			// about with my tiny limited brain).
			find_sym_result details_adjusted_call_site = find_symbol_details(fobj, (void*)(uintptr_t(trace.Address) - 1), symbol_info.dli_fbase);

			// In debug mode, we should always get the right thing(TM).
			if (details_call_site.found && details_adjusted_call_site.found) {
				// Ok, we assume that details_adjusted_call_site is a better estimation.
				details_selected = &details_adjusted_call_site;
				trace.Address = (void*)(uintptr_t(trace.Address) - 1);
			}

			if (details_selected == &details_call_site && details_call_site.found) {
				// we have to re-resolve the symbol in order to reset some internal state in BFD... so we can call
				// backtrace_inliners thereafter...
				details_call_site = find_symbol_details(fobj, trace.Address, symbol_info.dli_fbase);
			}
#	endif // BACKWARD_HAS_UNWIND

			if (details_selected->found) {
				if (details_selected->filename) {
					trace.Source.Filename = details_selected->filename;
				}
				trace.Source.Line = details_selected->line;

				if (details_selected->funcname) {
					// this time we get the name of the function where the code is located, instead of the function were
					// the address is located. In short, if the code was inlined, we get the function corresponding
					// to the code. Else we already got in trace.function.
					trace.source.function = Demangle(details_selected->funcname);

					if (!symbol_info.dli_sname) {
						// for the case dladdr failed to find the symbol name of the function, we might as well try
						// to put something here.
						trace.ObjectFunction = trace.Source.Function;
					}
				}

				// Maybe the source of the trace got inlined inside the function (trace.source.function). Let's see
				// if we can get all the inlined calls along the way up to the initial call site.
				trace.Inliners = backtrace_inliners(fobj, *details_selected);

#	if 0
				if (trace.Inliners.empty()) {
					// Maybe the trace was not inlined... or maybe it was and we are lacking the debug information.
					// Let's try to make the world better and see if we can get the line number of the function
					// (trace.source.function) now.
					//
					// We will get the location of where the function start (to be exact: the first instruction that
					// really start the function), not where the name of the function is defined. This can be quite far
					// away from the name of the function btw.
					//
					// If the source of the function is the same as the source of the trace, we cannot say if the trace
					// was really inlined or not. However, if the filename of the source is different between the function
					// and the trace... we can declare it as an inliner. This is not 100% accurate, but better than nothing.

					if (symbol_info.dli_saddr) {
						find_sym_result details = find_symbol_details(fobj, symbol_info.dli_saddr, symbol_info.dli_fbase);
						if (details.found) {
							ResolvedTrace::SourceLoc diy_inliner;
							diy_inliner.Line = details.Line;
							if (details.Filename) {
								diy_inliner.Filename = details.Filename;
							}
							if (details.funcname) {
								diy_inliner.Function = Demangle(details.funcname);
							} else {
								diy_inliner.Function = trace.source.function;
							}
							if (diy_inliner != trace.Source) {
								trace.Inliners.push_back(diy_inliner);
							}
						}
					}
				}
#	endif
			}

			return trace;
		}

	private:
		bool _bfd_loaded;

		typedef Implementation::Handle<bfd*, Implementation::Deleter<bfd_boolean, bfd*, &bfd_close>> bfd_handle_t;
		typedef Implementation::Handle<asymbol**> bfd_symtab_t;

		struct bfd_fileobject {
			bfd_handle_t handle;
			bfd_vma base_addr;
			bfd_symtab_t symtab;
			bfd_symtab_t dynamic_symtab;
		};

		typedef Implementation::Hashtable<std::string, bfd_fileobject>::type fobj_bfd_map_t;
		fobj_bfd_map_t _fobj_bfd_map;

		bfd_fileobject* load_object_with_bfd(const std::string& filename_object) {
			using namespace Implementation;

			if (!_bfd_loaded) {
				bfd_init();
				_bfd_loaded = true;
			}

			fobj_bfd_map_t::iterator it = _fobj_bfd_map.find(filename_object);
			if (it != _fobj_bfd_map.end()) {
				return &it->second;
			}

			// this new object is empty for now.
			bfd_fileobject* r = &_fobj_bfd_map[filename_object];

			// we do the work temporary in this one;
			bfd_handle_t bfd_handle;

			int fd = ::open(filename_object.c_str(), O_RDONLY);
			bfd_handle.reset(bfd_fdopenr(filename_object.c_str(), "default", fd));
			if (!bfd_handle) {
				close(fd);
				return r;
			}

			if (!bfd_check_format(bfd_handle.get(), bfd_object)) {
				return r; // not an object? You lose.
			}

			if ((bfd_get_file_flags(bfd_handle.get()) & HAS_SYMS) == 0) {
				return r; // that's what happen when you forget to compile in debug.
			}

			ssize_t symtab_storage_size = bfd_get_symtab_upper_bound(bfd_handle.get());
			ssize_t dyn_symtab_storage_size = bfd_get_dynamic_symtab_upper_bound(bfd_handle.get());

			if (symtab_storage_size <= 0 && dyn_symtab_storage_size <= 0) {
				return r; // weird, is the file is corrupted?
			}

			bfd_symtab_t symtab, dynamic_symtab;
			ssize_t symcount = 0, dyn_symcount = 0;

			if (symtab_storage_size > 0) {
				symtab.reset(static_cast<bfd_symbol**>(std::malloc(static_cast<std::size_t>(symtab_storage_size))));
				symcount = bfd_canonicalize_symtab(bfd_handle.get(), symtab.get());
			}

			if (dyn_symtab_storage_size > 0) {
				dynamic_symtab.reset(static_cast<bfd_symbol**>(std::malloc(static_cast<std::size_t>(dyn_symtab_storage_size))));
				dyn_symcount = bfd_canonicalize_dynamic_symtab(bfd_handle.get(), dynamic_symtab.get());
			}

			if (symcount <= 0 && dyn_symcount <= 0) {
				return r; // damned, that's a stripped file that you got there!
			}

			r->handle = std::move(bfd_handle);
			r->symtab = std::move(symtab);
			r->dynamic_symtab = std::move(dynamic_symtab);
			return r;
		}

		struct find_sym_result {
			bool found;
			const char* filename;
			const char* funcname;
			unsigned int line;
		};

		struct find_sym_context {
			TraceResolver* self;
			bfd_fileobject* fobj;
			void* addr;
			void* base_addr;
			find_sym_result result;
		};

		find_sym_result find_symbol_details(bfd_fileobject* fobj, void* addr, void* baseAddr) {
			find_sym_context context;
			context.self = this;
			context.fobj = fobj;
			context.addr = addr;
			context.base_addr = baseAddr;
			context.result.found = false;
			bfd_map_over_sections(fobj->handle.get(), &find_in_section_trampoline, static_cast<void*>(&context));
			return context.result;
		}

		static void find_in_section_trampoline(bfd*, asection* section, void* data) {
			find_sym_context* context = static_cast<find_sym_context*>(data);
			context->self->find_in_section(
				reinterpret_cast<bfd_vma>(context->addr),
				reinterpret_cast<bfd_vma>(context->base_addr), context->fobj, section,
				context->result);
		}

		void find_in_section(bfd_vma addr, bfd_vma base_addr, bfd_fileobject* fobj, asection* section, find_sym_result& result) {
			if (result.found) {
				return;
			}

#	if defined(bfd_get_section_flags)
			if ((bfd_get_section_flags(fobj->handle.get(), section) & SEC_ALLOC) == 0)
#	else
			if ((bfd_section_flags(section) & SEC_ALLOC) == 0)
#	endif
				return; // Debug section is never loaded automatically.
			
#	if defined(bfd_get_section_vma)
			bfd_vma sec_addr = bfd_get_section_vma(fobj->handle.get(), section);
#	else
			bfd_vma sec_addr = bfd_section_vma(section);
#	endif
#	if defined(bfd_get_section_size)
			bfd_size_type size = bfd_get_section_size(section);
#	else
			bfd_size_type size = bfd_section_size(section);
#	endif

			// Are we in the boundaries of the section?
			if (addr < sec_addr || addr >= sec_addr + size) {
				addr -= base_addr; // Oops, a relocated object, lets try again...
				if (addr < sec_addr || addr >= sec_addr + size) {
					return;
				}
			}

#if defined(DEATH_TARGET_CLANG)
#	pragma clang diagnostic push
#	pragma clang diagnostic ignored "-Wzero-as-null-pointer-constant"
#endif
			if (!result.found && fobj->symtab) {
				result.found = bfd_find_nearest_line(
					fobj->handle.get(), section, fobj->symtab.get(), addr - sec_addr,
					&result.filename, &result.funcname, &result.line);
			}

			if (!result.found && fobj->dynamic_symtab) {
				result.found = bfd_find_nearest_line(
					fobj->handle.get(), section, fobj->dynamic_symtab.get(),
					addr - sec_addr, &result.filename, &result.funcname, &result.line);
			}
#if defined(DEATH_TARGET_CLANG)
#	pragma clang diagnostic pop
#endif
		}

		ResolvedTrace::source_locs_t backtrace_inliners(bfd_fileobject* fobj, find_sym_result previous_result) {
			// This function can be called ONLY after a SUCCESSFUL call to
			// find_symbol_details. The state is global to the bfd_handle.
			ResolvedTrace::source_locs_t results;
			while (previous_result.found) {
				find_sym_result result;
				result.found = bfd_find_inliner_info(fobj->handle.get(), &result.filename, &result.funcname, &result.line);

				if (result.found) {
					ResolvedTrace::SourceLoc src_loc;
					src_loc.line = result.line;
					if (result.filename) {
						src_loc.filename = result.filename;
					}
					if (result.funcname) {
						src_loc.function = Demangle(result.funcname);
					}
					results.push_back(src_loc);
				}
				previous_result = result;
			}
			return results;
		}

		bool cstrings_eq(const char* a, const char* b) {
			if (a == nullptr || b == nullptr) {
				return false;
			}
			return strcmp(a, b) == 0;
		}
	};
#endif // BACKWARD_HAS_BFD

#if defined(BACKWARD_HAS_DW)

	class TraceResolver : public TraceResolverLinuxBase {
	public:
		TraceResolver() : _dwfl_handle_initialized(false) {}

		ResolvedTrace Resolve(ResolvedTrace trace) override {
			using namespace Implementation;

			Dwarf_Addr trace_addr = reinterpret_cast<Dwarf_Addr>(trace.Address);

			if (!_dwfl_handle_initialized) {
				// initialize dwfl...
				_dwfl_cb.reset(new Dwfl_Callbacks);
				_dwfl_cb->find_elf = &dwfl_linux_proc_find_elf;
				_dwfl_cb->find_debuginfo = &dwfl_standard_find_debuginfo;
				_dwfl_cb->debuginfo_path = nullptr;

				_dwfl_handle.reset(dwfl_begin(_dwfl_cb.get()));
				_dwfl_handle_initialized = true;

				if (!_dwfl_handle) {
					return trace;
				}

				// ...from the current process.
				dwfl_report_begin(_dwfl_handle.get());
				int r = dwfl_linux_proc_report(_dwfl_handle.get(), getpid());
				dwfl_report_end(_dwfl_handle.get(), nullptr, nullptr);
				if (r < 0) {
					return trace;
				}
			}

			if (!_dwfl_handle) {
				return trace;
			}

			// find the module (binary object) that contains the trace's address. This is not using any debug information,
			// but the addresses ranges of all the currently loaded binary object.
			Dwfl_Module* mod = dwfl_addrmodule(_dwfl_handle.get(), trace_addr);
			if (mod != nullptr) {
				// now that we found it, lets get the name of it, this will be the full path to the running binary
				// or one of the loaded library.
				const char* module_name = dwfl_module_info(mod, 0, 0, 0, 0, 0, 0, 0);
				if (module_name != nullptr) {
					trace.ObjectFilename = module_name;
				}
				// We also look after the name of the symbol, equal or before this address. This is found by walking
				// the symtab. We should get the symbol corresponding to the function (mangled) containing the address.
				// If the code corresponding to the address was inlined, this is the name of the out-most inliner function.
				const char* sym_name = dwfl_module_addrname(mod, trace_addr);
				if (sym_name != nullptr) {
					trace.ObjectFunction = Demangle(sym_name);
				}
			}

			// now let's get serious, and find out the source location (file and
			// line number) of the address.

			// This function will look in .debug_aranges for the address and map it to the location of the compilation
			// unit DIE in .debug_info and return it.
			Dwarf_Addr mod_bias = 0;
			Dwarf_Die* cudie = dwfl_module_addrdie(mod, trace_addr, &mod_bias);
			trace.ObjectBaseAddress = (void*)mod_bias;

#	if 1
			if (cudie == nullptr) {
				// Sadly clang does not generate the section .debug_aranges, thus dwfl_module_addrdie will fail early.
				// Clang doesn't either set the lowpc/highpc/range info for every compilation unit.
				//
				// So in order to save the world:
				// for every compilation unit, we will iterate over every single DIEs. Normally functions should have
				// a lowpc/highpc/range, which we will use to infer the compilation unit.

				// Note that this is probably badly inefficient.
				while ((cudie = dwfl_module_nextcu(mod, cudie, &mod_bias))) {
					Dwarf_Die die_mem;
					Dwarf_Die* fundie = find_fundie_by_pc(cudie, trace_addr - mod_bias, &die_mem);
					if (fundie != nullptr) {
						break;
					}
				}
			}
#	endif

//#	define BACKWARD_I_DO_NOT_RECOMMEND_TO_ENABLE_THIS_HORRIBLE_PIECE_OF_CODE
#	if defined(BACKWARD_I_DO_NOT_RECOMMEND_TO_ENABLE_THIS_HORRIBLE_PIECE_OF_CODE)
			if (!cudie) {
				// If it's still not enough, lets dive deeper in the shit, and try to save the world again: for every
				// compilation unit, we will load the corresponding .debug_line section, and see if we can find our address in it.

				Dwarf_Addr cfi_bias;
				Dwarf_CFI* cfi_cache = dwfl_module_eh_cfi(mod, &cfi_bias);

				Dwarf_Addr bias;
				while ((cudie = dwfl_module_nextcu(mod, cudie, &bias))) {
					if (dwarf_getsrc_die(cudie, trace_addr - bias)) {

						// ...but if we get a match, it might be a false positive because our (address - bias) might
						// as well be valid in a different compilation unit. So we throw our last card on the table
						// and lookup for the address into the .eh_frame section.

						handle<Dwarf_Frame*> frame;
						dwarf_cfi_addrframe(cfi_cache, trace_addr - cfi_bias, &frame);
						if (frame) {
							break;
						}
					}
				}
			}
#	endif

			if (!cudie) {
				return trace; // This time we lost the game :/
			}

			// Now that we have a compilation unit DIE, this function will be able to load the corresponding section
			// in .debug_line (if not already loaded) and hopefully find the source location mapped to our address.
			Dwarf_Line* srcloc = dwarf_getsrc_die(cudie, trace_addr - mod_bias);

			if (srcloc) {
				const char* srcfile = dwarf_linesrc(srcloc, 0, 0);
				if (srcfile != nullptr) {
					trace.Source.Filename = srcfile;
				}
				std::int32_t line = 0, col = 0;
				dwarf_lineno(srcloc, &line);
				dwarf_linecol(srcloc, &col);
				trace.Source.Line = line;
				trace.Source.Column = col;
			}

			deep_first_search_by_pc(cudie, trace_addr - mod_bias, inliners_search_cb(trace));
			if (trace.Source.Function.empty()) {
				// Fallback
				trace.Source.Function = trace.ObjectFunction;
			}

			return trace;
		}

	private:
		typedef Implementation::Handle<Dwfl*, Implementation::Deleter<void, Dwfl*, &dwfl_end>> dwfl_handle_t;
		Implementation::Handle<Dwfl_Callbacks*, Implementation::DefaultDelete<Dwfl_Callbacks*>> _dwfl_cb;
		dwfl_handle_t _dwfl_handle;
		bool _dwfl_handle_initialized;

		struct inliners_search_cb {
			void operator()(Dwarf_Die* die) {
				switch (dwarf_tag(die)) {
					case DW_TAG_subprogram: {
						const char* name;
						if ((name = dwarf_diename(die))) {
							trace.Source.Function = name;
							trace.Source.Function += "()";
						}
						break;
					}
					case DW_TAG_inlined_subroutine: {
						ResolvedTrace::SourceLoc sloc;
						Dwarf_Attribute attr_mem;

						const char* name;
						if ((name = dwarf_diename(die))) {
							sloc.Function = name;
							sloc.Function += "()";
						}
						if ((name = die_call_file(die))) {
							sloc.Filename = name;
						}

						Dwarf_Word line = 0, col = 0;
						dwarf_formudata(dwarf_attr(die, DW_AT_call_line, &attr_mem), &line);
						dwarf_formudata(dwarf_attr(die, DW_AT_call_column, &attr_mem), &col);
						sloc.Line = static_cast<std::int32_t>(line);
						sloc.Column = static_cast<std::int32_t>(col);

						trace.Inliners.push_back(sloc);
						break;
					}
				};
			}
			ResolvedTrace& trace;
			inliners_search_cb(ResolvedTrace& t) : trace(t) {}
		};

		static bool die_has_pc(Dwarf_Die* die, Dwarf_Addr pc) {
			Dwarf_Addr low, high;

			// Continuous range
			if (dwarf_hasattr(die, DW_AT_low_pc) && dwarf_hasattr(die, DW_AT_high_pc)) {
				if (dwarf_lowpc(die, &low) != 0) {
					return false;
				}
				if (dwarf_highpc(die, &high) != 0) {
					Dwarf_Attribute attr_mem;
					Dwarf_Attribute* attr = dwarf_attr(die, DW_AT_high_pc, &attr_mem);
					Dwarf_Word value;
					if (dwarf_formudata(attr, &value) != 0) {
						return false;
					}
					high = low + value;
				}
				return pc >= low && pc < high;
			}

			// Non-continuous range.
			Dwarf_Addr base;
			ptrdiff_t offset = 0;
			while ((offset = dwarf_ranges(die, offset, &base, &low, &high)) > 0) {
				if (pc >= low && pc < high) {
					return true;
				}
			}
			return false;
		}

		static Dwarf_Die* find_fundie_by_pc(Dwarf_Die* parent_die, Dwarf_Addr pc, Dwarf_Die* result) {
			if (dwarf_child(parent_die, result) != 0) {
				return 0;
			}

			Dwarf_Die* die = result;
			do {
				switch (dwarf_tag(die)) {
					case DW_TAG_subprogram:
					case DW_TAG_inlined_subroutine:
						if (die_has_pc(die, pc)) {
							return result;
						}
				};
				bool declaration = false;
				Dwarf_Attribute attr_mem;
				dwarf_formflag(dwarf_attr(die, DW_AT_declaration, &attr_mem), &declaration);
				if (!declaration) {
					// Let's be curious and look deeper in the tree, function are not necessarily at the first level,
					// but might be nested inside a namespace, structure etc.
					Dwarf_Die die_mem;
					Dwarf_Die* indie = find_fundie_by_pc(die, pc, &die_mem);
					if (indie) {
						*result = die_mem;
						return result;
					}
				}
			} while (dwarf_siblingof(die, result) == 0);
			return 0;
		}

		template <typename CB>
		static bool deep_first_search_by_pc(Dwarf_Die* parent_die, Dwarf_Addr pc, CB cb) {
			Dwarf_Die die_mem;
			if (dwarf_child(parent_die, &die_mem) != 0) {
				return false;
			}

			bool branch_has_pc = false;
			Dwarf_Die* die = &die_mem;
			do {
				bool declaration = false;
				Dwarf_Attribute attr_mem;
				dwarf_formflag(dwarf_attr(die, DW_AT_declaration, &attr_mem), &declaration);
				if (!declaration) {
					// Let's be curious and look deeper in the tree, function are not necessarily at the first level,
					// but might be nested inside a namespace, structure, a function, an inlined function etc.
					branch_has_pc = deep_first_search_by_pc(die, pc, cb);
				}
				if (!branch_has_pc) {
					branch_has_pc = die_has_pc(die, pc);
				}
				if (branch_has_pc) {
					cb(die);
				}
			} while (dwarf_siblingof(die, &die_mem) == 0);
			return branch_has_pc;
		}

		static const char* die_call_file(Dwarf_Die* die) {
			Dwarf_Attribute attr_mem;
			Dwarf_Word file_idx = 0;

			dwarf_formudata(dwarf_attr(die, DW_AT_call_file, &attr_mem), &file_idx);

			if (file_idx == 0) {
				return nullptr;
			}

			Dwarf_Die die_mem;
			Dwarf_Die* cudie = dwarf_diecu(die, &die_mem, nullptr, nullptr);
			if (!cudie) {
				return nullptr;
			}

			Dwarf_Files* files = nullptr;
			std::size_t nfiles;
			dwarf_getsrcfiles(cudie, &files, &nfiles);
			if (!files) {
				return nullptr;
			}

			return dwarf_filesrc(files, file_idx, nullptr, nullptr);
		}
	};
#endif // BACKWARD_HAS_DW

#if defined(BACKWARD_HAS_DWARF)

	class TraceResolver : public TraceResolverLinuxBase {
	public:
		TraceResolver() : _dwarf_loaded(false) {}

		ResolvedTrace Resolve(ResolvedTrace trace) override {
			// trace.addr is a virtual address in memory pointing to some code.
			// Let's try to find from which loaded object it comes from.
			// The loaded object can be yourself btw.

			Dl_info symbol_info;
			std::int32_t dladdr_result = 0;
#	if defined(__GLIBC__)
			link_map* link_map;
			// We request the link map so we can get information about offsets
			dladdr_result = dladdr1(trace.Address, &symbol_info, reinterpret_cast<void**>(&link_map), RTLD_DL_LINKMAP);
#	else
			// Android doesn't have dladdr1. Don't use the linker map.
			dladdr_result = dladdr(trace.Address, &symbol_info);
#	endif
			if (!dladdr_result) {
				return trace; // dat broken trace...
			}

			// Now we get in symbol_info:
			// .dli_fname:
			//      pathname of the shared object that contains the address.
			// .dli_fbase:
			//      where the object is loaded in memory.
			// .dli_sname:
			//      the name of the nearest symbol to trace.addr, we expect a
			//      function name.
			// .dli_saddr:
			//      the exact address corresponding to .dli_sname.
			//
			// And in link_map:
			// .l_addr:
			//      difference between the address in the ELF file and the address
			//      in memory
			// l_name:
			//      absolute pathname where the object was found

			if (symbol_info.dli_sname) {
				trace.ObjectFunction = Demangle(symbol_info.dli_sname);
			}

			if (!symbol_info.dli_fname) {
				return trace;
			}

			trace.ObjectFilename = resolve_exec_path(symbol_info);
			dwarf_fileobject& fobj = load_object_with_dwarf(symbol_info.dli_fname);
			if (!fobj.dwarf_handle) {
				return trace; // sad, we couldn't load the object :(
			}

#	if defined(__GLIBC__)
			// Convert the address to a module relative one by looking at the module's loading address in the link map
			Dwarf_Addr address = reinterpret_cast<uintptr_t>(trace.Address) - reinterpret_cast<uintptr_t>(link_map->l_addr);
			trace.ObjectBaseAddress = (void*)link_map->l_addr;
#	else
			Dwarf_Addr address = reinterpret_cast<uintptr_t>(trace.Address);
#	endif

			if (trace.ObjectFunction.empty()) {
				symbol_cache_t::iterator it = fobj.symbol_cache.lower_bound(address);

				if (it != fobj.symbol_cache.end()) {
					if (it->first != address) {
						if (it != fobj.symbol_cache.begin()) {
							--it;
						}
					}
					trace.object_function = Demangle(it->second.c_str());
				}
			}

			// Get the Compilation Unit DIE for the address
			Dwarf_Die die = find_die(fobj, address);
			if (!die) {
				return trace; // this time we lost the game :/
			}

			// libdwarf doesn't give us direct access to its objects, it always
			// allocates a copy for the caller. We keep that copy alive in a cache
			// and we deallocate it later when it's no longer required.
			die_cache_entry& die_object = get_die_cache(fobj, die);
			if (die_object.isEmpty()) {
				return trace; // We have no line section for this DIE
			}

			die_linemap_t::iterator it = die_object.line_section.lower_bound(address);

			if (it != die_object.line_section.end()) {
				if (it->first != address) {
					if (it == die_object.line_section.begin()) {
						// If we are on the first item of the line section
						// but the address does not match it means that
						// the address is below the range of the DIE. Give up.
						return trace;
					} else {
						--it;
					}
				}
			} else {
				return trace; // We didn't find the address.
			}

			// Get the Dwarf_Line that the address points to and call libdwarf
			// to get source file, line and column info.
			Dwarf_Line line = die_object.line_buffer[it->second];
			Dwarf_Error error = DW_DLE_NE;

			char* filename;
			if (dwarf_linesrc(line, &filename, &error) == DW_DLV_OK) {
				trace.Source.Filename = std::string(filename);
				dwarf_dealloc(fobj.dwarf_handle.get(), filename, DW_DLA_STRING);
			}

			Dwarf_Unsigned number = 0;
			if (dwarf_lineno(line, &number, &error) == DW_DLV_OK) {
				trace.Source.Line = number;
			} else {
				trace.Source.Line = 0;
			}

			if (dwarf_lineoff_b(line, &number, &error) == DW_DLV_OK) {
				trace.Source.Column = number;
			} else {
				trace.Source.Column = 0;
			}

			std::vector<std::string> namespace_stack;
			deep_first_search_by_pc(fobj, die, address, namespace_stack, inliners_search_cb(trace, fobj, die));

			dwarf_dealloc(fobj.dwarf_handle.get(), die, DW_DLA_DIE);

			return trace;
		}

		static std::int32_t close_dwarf(Dwarf_Debug dwarf) {
			return dwarf_finish(dwarf, NULL);
		}

	private:
		bool _dwarf_loaded;

		typedef Implementation::Handle<std::int32_t, Implementation::Deleter<std::int32_t, std::int32_t, &::close>> dwarf_file_t;
		typedef Implementation::Handle<Elf*, Implementation::Deleter<std::int32_t, Elf*, &elf_end>> dwarf_elf_t;
		typedef Implementation::Handle<Dwarf_Debug, Implementation::Deleter<std::int32_t, Dwarf_Debug, &close_dwarf>> dwarf_handle_t;
		typedef std::map<Dwarf_Addr, std::int32_t> die_linemap_t;
		typedef std::map<Dwarf_Off, Dwarf_Off> die_specmap_t;

		struct die_cache_entry {
			die_specmap_t spec_section;
			die_linemap_t line_section;
			Dwarf_Line* line_buffer;
			Dwarf_Signed line_count;
			Dwarf_Line_Context line_context;

			inline bool isEmpty() {
				return line_buffer == NULL || line_count == 0 || line_context == NULL || line_section.empty();
			}

			die_cache_entry() : line_buffer(0), line_count(0), line_context(0) {}

			~die_cache_entry() {
				if (line_context) {
					dwarf_srclines_dealloc_b(line_context);
				}
			}
		};

		typedef std::map<Dwarf_Off, die_cache_entry> die_cache_t;

		typedef std::map<uintptr_t, std::string> symbol_cache_t;

		struct dwarf_fileobject {
			dwarf_file_t file_handle;
			dwarf_elf_t elf_handle;
			dwarf_handle_t dwarf_handle;
			symbol_cache_t symbol_cache;

			// Die cache
			die_cache_t die_cache;
			die_cache_entry* current_cu;
		};

		typedef Implementation::Hashtable<std::string, dwarf_fileobject>::type fobj_dwarf_map_t;
		fobj_dwarf_map_t _fobj_dwarf_map;

		static bool cstrings_eq(const char* a, const char* b) {
			if (a == nullptr || b == nullptr) {
				return false;
			}
			return strcmp(a, b) == 0;
		}

		dwarf_fileobject& load_object_with_dwarf(const std::string& filename_object) {
			if (!_dwarf_loaded) {
				// Set the ELF library operating version, if that fails there's nothing we can do
				_dwarf_loaded = elf_version(EV_CURRENT) != EV_NONE;
			}

			fobj_dwarf_map_t::iterator it = _fobj_dwarf_map.find(filename_object);
			if (it != _fobj_dwarf_map.end()) {
				return it->second;
			}

			// This new object is empty for now
			dwarf_fileobject& r = _fobj_dwarf_map[filename_object];

			dwarf_file_t file_handle;
			file_handle.reset(open(filename_object.c_str(), O_RDONLY));
			if (file_handle.get() < 0) {
				return r;
			}

			// Try to get an ELF handle. We need to read the ELF sections, because we want to see if there
			// is a .gnu_debuglink section that points to a split debug file
			dwarf_elf_t elf_handle;
			elf_handle.reset(elf_begin(file_handle.get(), ELF_C_READ, NULL));
			if (!elf_handle) {
				return r;
			}

			const char* e_ident = elf_getident(elf_handle.get(), 0);
			if (e_ident == nullptr) {
				return r;
			}

			// Get the number of sections
			// We use the new APIs as elf_getshnum is deprecated
			std::size_t shdrnum = 0;
			if (elf_getshdrnum(elf_handle.get(), &shdrnum) == -1) {
				return r;
			}

			// Get the index to the string section
			std::size_t shdrstrndx = 0;
			if (elf_getshdrstrndx(elf_handle.get(), &shdrstrndx) == -1) {
				return r;
			}

			std::string debuglink;
			// Iterate through the ELF sections to try to get a gnu_debuglink note and also to cache the symbol table.
			// We go the preprocessor way to avoid having to create templated classes or using gelf (which might throw
			// a compiler error if 64 bit is not supported)
#define ELF_GET_DATA(ARCH)																	\
	Elf_Scn *elf_section = 0;																\
	Elf_Data *elf_data = 0;																	\
	Elf##ARCH##_Shdr *section_header = 0;													\
	Elf_Scn *symbol_section = 0;															\
	std::size_t symbol_count = 0;															\
	std::size_t symbol_strings = 0;															\
	Elf##ARCH##_Sym *symbol = 0;															\
	const char *section_name = 0;															\
																							\
	while ((elf_section = elf_nextscn(elf_handle.get(), elf_section)) != NULL) {			\
		section_header = elf##ARCH##_getshdr(elf_section);									\
		if (section_header == NULL) {														\
			return r;																		\
		}																					\
																							\
		if ((section_name = elf_strptr(elf_handle.get(), shdrstrndx,						\
									   section_header->sh_name)) == NULL) {					\
			return r;																		\
		}																					\
																							\
		if (cstrings_eq(section_name, ".gnu_debuglink")) {									\
			elf_data = elf_getdata(elf_section, NULL);										\
			if (elf_data && elf_data->d_size > 0) {											\
				debuglink =																	\
					std::string(reinterpret_cast<const char *>(elf_data->d_buf));			\
			}																				\
		}																					\
																							\
		switch (section_header->sh_type) {													\
			case SHT_SYMTAB:																\
				symbol_section = elf_section;												\
				symbol_count = section_header->sh_size / section_header->sh_entsize;		\
				symbol_strings = section_header->sh_link;									\
				break;																		\
																							\
			/* We use .dynsyms as a last resort, we prefer .symtab */						\
			case SHT_DYNSYM:																\
				if (!symbol_section) {														\
					symbol_section = elf_section;											\
					symbol_count = section_header->sh_size / section_header->sh_entsize;	\
					symbol_strings = section_header->sh_link;								\
				}																			\
				break;																		\
		}																					\
	}																						\
																							\
	if (symbol_section && symbol_count && symbol_strings) {									\
		elf_data = elf_getdata(symbol_section, NULL);										\
		symbol = reinterpret_cast<Elf##ARCH##_Sym *>(elf_data->d_buf);						\
		for (std::size_t i = 0; i < symbol_count; ++i) {									\
			std::int32_t type = ELF##ARCH##_ST_TYPE(symbol->st_info);						\
			if (type == STT_FUNC && symbol->st_value > 0) {									\
				r.symbol_cache[symbol->st_value] = std::string(								\
				elf_strptr(elf_handle.get(), symbol_strings, symbol->st_name));				\
			}																				\
			++symbol;																		\
		}																					\
	}

			if (e_ident[EI_CLASS] == ELFCLASS32) {
				ELF_GET_DATA(32)
			} else if (e_ident[EI_CLASS] == ELFCLASS64) {
					// libelf might have been built without 64 bit support
#	if __LIBELF64
					ELF_GET_DATA(64)
#	endif
				}

				if (!debuglink.empty()) {
					// We have a debuglink section! Open an elf instance on that file instead. If we can't open
					// the file, then return the elf handle we had already opened.
					dwarf_file_t debuglink_file;
					debuglink_file.reset(open(debuglink.c_str(), O_RDONLY));
					if (debuglink_file.get() > 0) {
						dwarf_elf_t debuglink_elf;
						debuglink_elf.reset(elf_begin(debuglink_file.get(), ELF_C_READ, NULL));

						// If we have a valid elf handle, return the new elf handle and file handle and discard the original ones
						if (debuglink_elf) {
							elf_handle = std::move(debuglink_elf);
							file_handle = std::move(debuglink_file);
						}
					}
				}

				// Ok, we have a valid ELF handle, let's try to get debug symbols
				Dwarf_Debug dwarf_debug;
				Dwarf_Error error = DW_DLE_NE;
				dwarf_handle_t dwarf_handle;

				std::int32_t dwarf_result = dwarf_elf_init(elf_handle.get(), DW_DLC_READ, NULL, NULL, &dwarf_debug, &error);

				// We don't do any special handling for DW_DLV_NO_ENTRY specially.
				// If we get an error, or the file doesn't have debug information we just return.
				if (dwarf_result != DW_DLV_OK) {
					return r;
				}

				dwarf_handle.reset(dwarf_debug);

				r.file_handle = std::move(file_handle);
				r.elf_handle = std::move(elf_handle);
				r.dwarf_handle = std::move(dwarf_handle);

				return r;
		}

		die_cache_entry& get_die_cache(dwarf_fileobject& fobj, Dwarf_Die die) {
			Dwarf_Error error = DW_DLE_NE;

			// Get the die offset, we use it as the cache key
			Dwarf_Off die_offset;
			if (dwarf_dieoffset(die, &die_offset, &error) != DW_DLV_OK) {
				die_offset = 0;
			}

			die_cache_t::iterator it = fobj.die_cache.find(die_offset);

			if (it != fobj.die_cache.end()) {
				fobj.current_cu = &it->second;
				return it->second;
			}

			die_cache_entry& de = fobj.die_cache[die_offset];
			fobj.current_cu = &de;

			Dwarf_Addr line_addr;
			Dwarf_Small table_count;

			// The addresses in the line section are not fully sorted (they might be sorted by block of code belonging
			// to the same file), which makes it necessary to do so before searching is possible.
			//
			// As libdwarf allocates a copy of everything, let's get the contents of the line section and keep it around.
			// We also create a map of program counter to line table indices so we can search by address and get the
			// line buffer index.
			//
			// To make things more difficult, the same address can span more than one line, so we need to keep the index
			// pointing to the first line by using insert instead of the map's [ operator.

			// Get the line context for the DIE
			if (dwarf_srclines_b(die, 0, &table_count, &de.line_context, &error) == DW_DLV_OK) {
				// Get the source lines for this line context, to be deallocated later
				if (dwarf_srclines_from_linecontext(de.line_context, &de.line_buffer, &de.line_count, &error) == DW_DLV_OK) {
					// Add all the addresses to our map
					for (std::int32_t i = 0; i < de.line_count; i++) {
						if (dwarf_lineaddr(de.line_buffer[i], &line_addr, &error) != DW_DLV_OK) {
							line_addr = 0;
						}
						de.line_section.insert(std::pair<Dwarf_Addr, std::int32_t>(line_addr, i));
					}
				}
			}

			// For each CU, cache the function DIEs that contain the DW_AT_specification attribute. When building
			// with -g3 the function DIEs are separated in declaration and specification, with the declaration containing
			// only the name and parameters and the specification the low/high pc and other compiler attributes.
			//
			// We cache those specifications so we don't skip over the declarations, because they have no pc, and we
			// can do namespace resolution for DWARF function names.
			Dwarf_Debug dwarf = fobj.dwarf_handle.get();
			Dwarf_Die current_die = 0;
			if (dwarf_child(die, &current_die, &error) == DW_DLV_OK) {
				while (true) {
					Dwarf_Die sibling_die = 0;

					Dwarf_Half tag_value;
					dwarf_tag(current_die, &tag_value, &error);

					if (tag_value == DW_TAG_subprogram ||
						tag_value == DW_TAG_inlined_subroutine) {

						Dwarf_Bool has_attr = 0;
						if (dwarf_hasattr(current_die, DW_AT_specification, &has_attr, &error) == DW_DLV_OK) {
							if (has_attr) {
								Dwarf_Attribute attr_mem;
								if (dwarf_attr(current_die, DW_AT_specification, &attr_mem, &error) == DW_DLV_OK) {
									Dwarf_Off spec_offset = 0;
									if (dwarf_formref(attr_mem, &spec_offset, &error) == DW_DLV_OK) {
										Dwarf_Off spec_die_offset;
										if (dwarf_dieoffset(current_die, &spec_die_offset, &error) == DW_DLV_OK) {
											de.spec_section[spec_offset] = spec_die_offset;
										}
									}
								}
								dwarf_dealloc(dwarf, attr_mem, DW_DLA_ATTR);
							}
						}
					}

					std::int32_t result = dwarf_siblingof(dwarf, current_die, &sibling_die, &error);
					if (result == DW_DLV_ERROR) {
						break;
					} else if (result == DW_DLV_NO_ENTRY) {
						break;
					}

					if (current_die != die) {
						dwarf_dealloc(dwarf, current_die, DW_DLA_DIE);
						current_die = 0;
					}

					current_die = sibling_die;
				}
			}
			return de;
		}

		static Dwarf_Die get_referenced_die(Dwarf_Debug dwarf, Dwarf_Die die, Dwarf_Half attr, bool global) {
			Dwarf_Error error = DW_DLE_NE;
			Dwarf_Attribute attr_mem;

			Dwarf_Die found_die = NULL;
			if (dwarf_attr(die, attr, &attr_mem, &error) == DW_DLV_OK) {
				Dwarf_Off offset;
				std::int32_t result = 0;
				if (global) {
					result = dwarf_global_formref(attr_mem, &offset, &error);
				} else {
					result = dwarf_formref(attr_mem, &offset, &error);
				}

				if (result == DW_DLV_OK) {
					if (dwarf_offdie(dwarf, offset, &found_die, &error) != DW_DLV_OK) {
						found_die = NULL;
					}
				}
				dwarf_dealloc(dwarf, attr_mem, DW_DLA_ATTR);
			}
			return found_die;
		}

		static std::string get_referenced_die_name(Dwarf_Debug dwarf, Dwarf_Die die, Dwarf_Half attr, bool global) {
			Dwarf_Error error = DW_DLE_NE;
			std::string value;

			Dwarf_Die found_die = get_referenced_die(dwarf, die, attr, global);
			if (found_die) {
				char* name;
				if (dwarf_diename(found_die, &name, &error) == DW_DLV_OK) {
					if (name) {
						value = std::string(name);
					}
					dwarf_dealloc(dwarf, name, DW_DLA_STRING);
				}
				dwarf_dealloc(dwarf, found_die, DW_DLA_DIE);
			}

			return value;
		}

		// Returns a spec DIE linked to the passed one. The caller should deallocate the DIE
		static Dwarf_Die get_spec_die(dwarf_fileobject& fobj, Dwarf_Die die) {
			Dwarf_Debug dwarf = fobj.dwarf_handle.get();
			Dwarf_Error error = DW_DLE_NE;
			Dwarf_Off die_offset;
			if (fobj.current_cu && dwarf_die_CU_offset(die, &die_offset, &error) == DW_DLV_OK) {
				die_specmap_t::iterator it = fobj.current_cu->spec_section.find(die_offset);

				// If we have a DIE that completes the current one, check if that one has the pc we are looking for
				if (it != fobj.current_cu->spec_section.end()) {
					Dwarf_Die spec_die = 0;
					if (dwarf_offdie(dwarf, it->second, &spec_die, &error) == DW_DLV_OK) {
						return spec_die;
					}
				}
			}

			// Maybe we have an abstract origin DIE with the function information?
			return get_referenced_die(fobj.dwarf_handle.get(), die, DW_AT_abstract_origin, true);
		}

		static bool die_has_pc(dwarf_fileobject& fobj, Dwarf_Die die, Dwarf_Addr pc) {
			Dwarf_Addr low_pc = 0, high_pc = 0;
			Dwarf_Half high_pc_form = 0;
			Dwarf_Form_Class return_class;
			Dwarf_Error error = DW_DLE_NE;
			Dwarf_Debug dwarf = fobj.dwarf_handle.get();
			bool has_lowpc = false;
			bool has_highpc = false;
			bool has_ranges = false;

			if (dwarf_lowpc(die, &low_pc, &error) == DW_DLV_OK) {
				// If we have a low_pc check if there is a high pc.
				// If we don't have a high pc this might mean we have a base address for the ranges list or just an address.
				has_lowpc = true;

				if (dwarf_highpc_b(die, &high_pc, &high_pc_form, &return_class, &error) ==
					DW_DLV_OK) {
					// We do have a high pc. In DWARF 4+ this is an offset from the low pc, but in earlier versions it's an absolute address.

					has_highpc = true;
					// In DWARF 2/3 this would be a DW_FORM_CLASS_ADDRESS
					if (return_class == DW_FORM_CLASS_CONSTANT) {
						high_pc = low_pc + high_pc;
					}

					// We have low and high pc, check if our address
					// is in that range
					return pc >= low_pc && pc < high_pc;
				}
			} else {
				// Reset the low_pc, in case dwarf_lowpc failing set it to some
				// undefined value.
				low_pc = 0;
			}

			// Check if DW_AT_ranges is present and search for the PC in the returned ranges list. We always add
			// the low_pc, as it not set it will be 0, in case we had a DW_AT_low_pc and DW_AT_ranges pair
			bool result = false;

			Dwarf_Attribute attr;
			if (dwarf_attr(die, DW_AT_ranges, &attr, &error) == DW_DLV_OK) {

				Dwarf_Off offset;
				if (dwarf_global_formref(attr, &offset, &error) == DW_DLV_OK) {
					Dwarf_Ranges* ranges;
					Dwarf_Signed ranges_count = 0;
					Dwarf_Unsigned byte_count = 0;

					if (dwarf_get_ranges_a(dwarf, offset, die, &ranges, &ranges_count, &byte_count, &error) == DW_DLV_OK) {
						has_ranges = ranges_count != 0;
						for (std::int32_t i = 0; i < ranges_count; i++) {
							if (ranges[i].dwr_addr1 != 0 &&
								pc >= ranges[i].dwr_addr1 + low_pc &&
								pc < ranges[i].dwr_addr2 + low_pc) {
								result = true;
								break;
							}
						}
						dwarf_ranges_dealloc(dwarf, ranges, ranges_count);
					}
				}
			}

			// Last attempt. We might have a single address set as low_pc.
			if (!result && low_pc != 0 && pc == low_pc) {
				result = true;
			}

			// If we don't have lowpc, highpc and ranges maybe this DIE is a declaration that relies on
			// a DW_AT_specification DIE that happens later. Use the specification cache we filled when we loaded this CU.
			if (!result && (!has_lowpc && !has_highpc && !has_ranges)) {
				Dwarf_Die spec_die = get_spec_die(fobj, die);
				if (spec_die) {
					result = die_has_pc(fobj, spec_die, pc);
					dwarf_dealloc(dwarf, spec_die, DW_DLA_DIE);
				}
			}

			return result;
		}

		static void get_type(Dwarf_Debug dwarf, Dwarf_Die die, std::string& type) {
			Dwarf_Error error = DW_DLE_NE;

			Dwarf_Die child = 0;
			if (dwarf_child(die, &child, &error) == DW_DLV_OK) {
				get_type(dwarf, child, type);
			}

			if (child) {
				type.insert(0, "::");
				dwarf_dealloc(dwarf, child, DW_DLA_DIE);
			}

			char* name;
			if (dwarf_diename(die, &name, &error) == DW_DLV_OK) {
				type.insert(0, std::string(name));
				dwarf_dealloc(dwarf, name, DW_DLA_STRING);
			} else {
				type.insert(0, "<unknown>");
			}
		}

		static std::string get_type_by_signature(Dwarf_Debug dwarf, Dwarf_Die die) {
			Dwarf_Error error = DW_DLE_NE;

			Dwarf_Sig8 signature;
			Dwarf_Bool has_attr = 0;
			if (dwarf_hasattr(die, DW_AT_signature, &has_attr, &error) == DW_DLV_OK) {
				if (has_attr) {
					Dwarf_Attribute attr_mem;
					if (dwarf_attr(die, DW_AT_signature, &attr_mem, &error) == DW_DLV_OK) {
						if (dwarf_formsig8(attr_mem, &signature, &error) != DW_DLV_OK) {
							return std::string("<no type signature>");
						}
					}
					dwarf_dealloc(dwarf, attr_mem, DW_DLA_ATTR);
				}
			}

			Dwarf_Unsigned next_cu_header;
			Dwarf_Sig8 tu_signature;
			std::string result;
			bool found = false;

			while (dwarf_next_cu_header_d(dwarf, 0, 0, 0, 0, 0, 0, 0, &tu_signature, 0, &next_cu_header, 0, &error) == DW_DLV_OK) {
				if (strncmp(signature.signature, tu_signature.signature, 8) == 0) {
					Dwarf_Die type_cu_die = 0;
					if (dwarf_siblingof_b(dwarf, 0, 0, &type_cu_die, &error) == DW_DLV_OK) {
						Dwarf_Die child_die = 0;
						if (dwarf_child(type_cu_die, &child_die, &error) == DW_DLV_OK) {
							get_type(dwarf, child_die, result);
							found = !result.empty();
							dwarf_dealloc(dwarf, child_die, DW_DLA_DIE);
						}
						dwarf_dealloc(dwarf, type_cu_die, DW_DLA_DIE);
					}
				}
			}

			if (found) {
				while (dwarf_next_cu_header_d(dwarf, 0, 0, 0, 0, 0, 0, 0, 0, 0, &next_cu_header, 0, &error) == DW_DLV_OK) {
					// Reset the cu header state. Unfortunately, libdwarf's next_cu_header API keeps its own iterator
					// per Dwarf_Debug that can't be reset. We need to keep fetching elements until the end.
				}
			} else {
				// If we couldn't resolve the type just print out the signature
				std::ostringstream string_stream;
				string_stream << "<0x" << std::hex << std::setfill('0');
				for (std::int32_t i = 0; i < 8; ++i) {
					string_stream << std::setw(2) << std::hex << (std::int32_t)(unsigned char)(signature.signature[i]);
				}
				string_stream << ">";
				result = string_stream.str();
			}
			return result;
		}

		struct type_context_t {
			bool is_const;
			bool is_typedef;
			bool has_type;
			bool has_name;
			std::string text;

			type_context_t()
				: is_const(false), is_typedef(false), has_type(false), has_name(false) {
			}
		};

		// Types are resolved from right to left: we get the variable name first and then all specifiers (like const
		// or pointer) in a chain of DW_AT_type DIEs. Call this function recursively until we get a complete type string.
		static void set_parameter_string(dwarf_fileobject& fobj, Dwarf_Die die, type_context_t& context) {
			char* name;
			Dwarf_Error error = DW_DLE_NE;

			// typedefs contain also the base type, so we skip it and only print the typedef name
			if (!context.is_typedef) {
				if (dwarf_diename(die, &name, &error) == DW_DLV_OK) {
					if (!context.text.empty()) {
						context.text.insert(0, " ");
					}
					context.text.insert(0, std::string(name));
					dwarf_dealloc(fobj.dwarf_handle.get(), name, DW_DLA_STRING);
				}
			} else {
				context.is_typedef = false;
				context.has_type = true;
				if (context.is_const) {
					context.text.insert(0, "const ");
					context.is_const = false;
				}
			}

			bool next_type_is_const = false;
			bool is_keyword = true;

			Dwarf_Half tag = 0;
			Dwarf_Bool has_attr = 0;
			if (dwarf_tag(die, &tag, &error) == DW_DLV_OK) {
				switch (tag) {
					case DW_TAG_structure_type:
					case DW_TAG_union_type:
					case DW_TAG_class_type:
					case DW_TAG_enumeration_type:
						context.has_type = true;
						if (dwarf_hasattr(die, DW_AT_signature, &has_attr, &error) ==
							DW_DLV_OK) {
							// If we have a signature it means the type is defined in .debug_types, so we need to load
							// the DIE pointed at by the signature and resolve it
							if (has_attr) {
								std::string type = get_type_by_signature(fobj.dwarf_handle.get(), die);
								if (context.is_const) {
									type.insert(0, "const ");
								}
								if (!context.text.empty()) {
									context.text.insert(0, " ");
								}
								context.text.insert(0, type);
							}

							// Treat enums like typedefs, and skip printing its base type
							context.is_typedef = (tag == DW_TAG_enumeration_type);
						}
						break;
					case DW_TAG_const_type:
						next_type_is_const = true;
						break;
					case DW_TAG_pointer_type:
						context.text.insert(0, "*");
						break;
					case DW_TAG_reference_type:
						context.text.insert(0, "&");
						break;
					case DW_TAG_restrict_type:
						context.text.insert(0, "restrict ");
						break;
					case DW_TAG_rvalue_reference_type:
						context.text.insert(0, "&&");
						break;
					case DW_TAG_volatile_type:
						context.text.insert(0, "volatile ");
						break;
					case DW_TAG_typedef:
						// Propagate the const-ness to the next type
						// as typedefs are linked to its base type
						next_type_is_const = context.is_const;
						context.is_typedef = true;
						context.has_type = true;
						break;
					case DW_TAG_base_type:
						context.has_type = true;
						break;
					case DW_TAG_formal_parameter:
						context.has_name = true;
						break;
					default:
						is_keyword = false;
						break;
				}
			}

			if (!is_keyword && context.is_const) {
				context.text.insert(0, "const ");
			}

			context.is_const = next_type_is_const;

			Dwarf_Die ref = get_referenced_die(fobj.dwarf_handle.get(), die, DW_AT_type, true);
			if (ref) {
				set_parameter_string(fobj, ref, context);
				dwarf_dealloc(fobj.dwarf_handle.get(), ref, DW_DLA_DIE);
			}

			if (!context.has_type && context.has_name) {
				context.text.insert(0, "void ");
				context.has_type = true;
			}
		}

		// Resolve the function return type and parameters
		static void set_function_parameters(std::string& function_name, std::vector<std::string>& ns, dwarf_fileobject& fobj, Dwarf_Die die) {
			Dwarf_Debug dwarf = fobj.dwarf_handle.get();
			Dwarf_Error error = DW_DLE_NE;
			Dwarf_Die current_die = 0;
			std::string parameters;
			bool has_spec = true;
			// Check if we have a spec DIE. If we do we use it as it contains
			// more information, like parameter names.
			Dwarf_Die spec_die = get_spec_die(fobj, die);
			if (!spec_die) {
				has_spec = false;
				spec_die = die;
			}

			std::vector<std::string>::const_iterator it = ns.begin();
			std::string ns_name;
			for (it = ns.begin(); it < ns.end(); ++it) {
				ns_name.append(*it).append("::");
			}

			if (!ns_name.empty()) {
				function_name.insert(0, ns_name);
			}

			// See if we have a function return type. It can be either on the
			// current die or in its spec one (usually true for inlined functions)
			std::string return_type = get_referenced_die_name(dwarf, die, DW_AT_type, true);
			if (return_type.empty()) {
				return_type = get_referenced_die_name(dwarf, spec_die, DW_AT_type, true);
			}
			if (!return_type.empty()) {
				return_type.append(" ");
				function_name.insert(0, return_type);
			}

			if (dwarf_child(spec_die, &current_die, &error) == DW_DLV_OK) {
				while (true) {
					Dwarf_Die sibling_die = 0;

					Dwarf_Half tag_value;
					dwarf_tag(current_die, &tag_value, &error);

					if (tag_value == DW_TAG_formal_parameter) {
						// Ignore artificial (ie, compiler generated) parameters
						bool is_artificial = false;
						Dwarf_Attribute attr_mem;
						if (dwarf_attr(current_die, DW_AT_artificial, &attr_mem, &error) == DW_DLV_OK) {
							Dwarf_Bool flag = 0;
							if (dwarf_formflag(attr_mem, &flag, &error) == DW_DLV_OK) {
								is_artificial = flag != 0;
							}
							dwarf_dealloc(dwarf, attr_mem, DW_DLA_ATTR);
						}

						if (!is_artificial) {
							type_context_t context;
							set_parameter_string(fobj, current_die, context);

							if (parameters.empty()) {
								parameters.append("(");
							} else {
								parameters.append(", ");
							}
							parameters.append(context.text);
						}
					}

					std::int32_t result = dwarf_siblingof(dwarf, current_die, &sibling_die, &error);
					if (result == DW_DLV_ERROR) {
						break;
					} else if (result == DW_DLV_NO_ENTRY) {
						break;
					}

					if (current_die != die) {
						dwarf_dealloc(dwarf, current_die, DW_DLA_DIE);
						current_die = 0;
					}

					current_die = sibling_die;
				}
			}
			if (parameters.empty()) {
				parameters = "(";
			}
			parameters.append(")");

			// If we got a spec DIE we need to deallocate it
			if (has_spec) {
				dwarf_dealloc(dwarf, spec_die, DW_DLA_DIE);
			}
			function_name.append(parameters);
		}

		struct inliners_search_cb {
			void operator()(Dwarf_Die die, std::vector<std::string>& ns) {
				Dwarf_Error error = DW_DLE_NE;
				Dwarf_Half tag_value;
				Dwarf_Attribute attr_mem;
				Dwarf_Debug dwarf = fobj.dwarf_handle.get();

				dwarf_tag(die, &tag_value, &error);

				switch (tag_value) {
					char* name;
					case DW_TAG_subprogram:
						if (!trace.Source.Function.empty())
							break;
						if (dwarf_diename(die, &name, &error) == DW_DLV_OK) {
							trace.Source.Function = std::string(name);
							dwarf_dealloc(dwarf, name, DW_DLA_STRING);
						} else {
							// We don't have a function name in this DIE. Check if there is a referenced non-defining declaration.
							trace.Source.Function = get_referenced_die_name(dwarf, die, DW_AT_abstract_origin, true);
							if (trace.Source.Function.empty()) {
								trace.Source.Function = get_referenced_die_name(dwarf, die, DW_AT_specification, true);
							}
						}

						// Append the function parameters, if available
						set_function_parameters(trace.Source.Function, ns, fobj, die);

						// If the object function name is empty, it's possible that there is no dynamic symbol table
						// (maybe the executable was stripped or not built with -rdynamic). See if we have a DWARF
						// linkage name to use instead. We try both linkage_name and MIPS_linkage_name because
						// the MIPS tag was the unofficial one until it was adopted in DWARF4. Old gcc versions
						// generate MIPS_linkage_name
						if (trace.ObjectFunction.empty()) {
							Implementation::Demangler demangler;

							if (dwarf_attr(die, DW_AT_linkage_name, &attr_mem, &error) != DW_DLV_OK) {
								if (dwarf_attr(die, DW_AT_MIPS_linkage_name, &attr_mem, &error) != DW_DLV_OK) {
									break;
								}
							}

							char* linkage;
							if (dwarf_formstring(attr_mem, &linkage, &error) == DW_DLV_OK) {
								trace.ObjectFunction = demangler.Demangle(linkage);
								dwarf_dealloc(dwarf, linkage, DW_DLA_STRING);
							}
							dwarf_dealloc(dwarf, attr_mem, DW_DLA_ATTR);
						}
						break;

					case DW_TAG_inlined_subroutine:
						ResolvedTrace::SourceLoc sloc;

						if (dwarf_diename(die, &name, &error) == DW_DLV_OK) {
							sloc.Function = std::string(name);
							dwarf_dealloc(dwarf, name, DW_DLA_STRING);
						} else {
							// We don't have a name for this inlined DIE, it could be that there is an abstract origin instead.
							// Get the DW_AT_abstract_origin value, which is a reference to the source DIE and try to get its name.
							sloc.Function = get_referenced_die_name(dwarf, die, DW_AT_abstract_origin, true);
						}

						set_function_parameters(sloc.Function, ns, fobj, die);

						std::string file = die_call_file(dwarf, die, cu_die);
						if (!file.empty()) {
							sloc.Filename = file;
						}
						Dwarf_Unsigned number = 0;
						if (dwarf_attr(die, DW_AT_call_line, &attr_mem, &error) == DW_DLV_OK) {
							if (dwarf_formudata(attr_mem, &number, &error) == DW_DLV_OK) {
								sloc.Line = number;
							}
							dwarf_dealloc(dwarf, attr_mem, DW_DLA_ATTR);
						}

						if (dwarf_attr(die, DW_AT_call_column, &attr_mem, &error) == DW_DLV_OK) {
							if (dwarf_formudata(attr_mem, &number, &error) == DW_DLV_OK) {
								sloc.Column = number;
							}
							dwarf_dealloc(dwarf, attr_mem, DW_DLA_ATTR);
						}

						trace.Inliners.push_back(sloc);
						break;
				};
			}
			ResolvedTrace& trace;
			dwarf_fileobject& fobj;
			Dwarf_Die cu_die;
			inliners_search_cb(ResolvedTrace& t, dwarf_fileobject& f, Dwarf_Die c)
				: trace(t), fobj(f), cu_die(c) {}
		};

		static Dwarf_Die find_fundie_by_pc(dwarf_fileobject& fobj, Dwarf_Die parent_die, Dwarf_Addr pc, Dwarf_Die result) {
			Dwarf_Die current_die = 0;
			Dwarf_Error error = DW_DLE_NE;
			Dwarf_Debug dwarf = fobj.dwarf_handle.get();

			if (dwarf_child(parent_die, &current_die, &error) != DW_DLV_OK) {
				return NULL;
			}

			while (true) {
				Dwarf_Die sibling_die = 0;
				Dwarf_Half tag_value;
				dwarf_tag(current_die, &tag_value, &error);

				switch (tag_value) {
					case DW_TAG_subprogram:
					case DW_TAG_inlined_subroutine:
						if (die_has_pc(fobj, current_die, pc)) {
							return current_die;
						}
				};
				bool declaration = false;
				Dwarf_Attribute attr_mem;
				if (dwarf_attr(current_die, DW_AT_declaration, &attr_mem, &error) == DW_DLV_OK) {
					Dwarf_Bool flag = 0;
					if (dwarf_formflag(attr_mem, &flag, &error) == DW_DLV_OK) {
						declaration = flag != 0;
					}
					dwarf_dealloc(dwarf, attr_mem, DW_DLA_ATTR);
				}

				if (!declaration) {
					// Let's be curious and look deeper in the tree, functions are not necessarily at the first level,
					// but might be nested inside a namespace, structure, a function, an inlined function etc.
					Dwarf_Die die_mem = 0;
					Dwarf_Die indie = find_fundie_by_pc(fobj, current_die, pc, die_mem);
					if (indie) {
						result = die_mem;
						return result;
					}
				}

				std::int32_t res = dwarf_siblingof(dwarf, current_die, &sibling_die, &error);
				if (res == DW_DLV_ERROR) {
					return NULL;
				} else if (res == DW_DLV_NO_ENTRY) {
					break;
				}

				if (current_die != parent_die) {
					dwarf_dealloc(dwarf, current_die, DW_DLA_DIE);
					current_die = 0;
				}

				current_die = sibling_die;
			}
			return NULL;
		}

		template<typename CB>
		static bool deep_first_search_by_pc(dwarf_fileobject& fobj, Dwarf_Die parent_die, Dwarf_Addr pc, std::vector<std::string>& ns, CB cb) {
			Dwarf_Die current_die = 0;
			Dwarf_Debug dwarf = fobj.dwarf_handle.get();
			Dwarf_Error error = DW_DLE_NE;

			if (dwarf_child(parent_die, &current_die, &error) != DW_DLV_OK) {
				return false;
			}

			bool branch_has_pc = false;
			bool has_namespace = false;
			while (true) {
				Dwarf_Die sibling_die = 0;

				Dwarf_Half tag;
				if (dwarf_tag(current_die, &tag, &error) == DW_DLV_OK) {
					if (tag == DW_TAG_namespace || tag == DW_TAG_class_type) {
						char* ns_name = NULL;
						if (dwarf_diename(current_die, &ns_name, &error) == DW_DLV_OK) {
							if (ns_name) {
								ns.push_back(std::string(ns_name));
							} else {
								ns.push_back("<unknown>");
							}
							dwarf_dealloc(dwarf, ns_name, DW_DLA_STRING);
						} else {
							ns.push_back("<unknown>");
						}
						has_namespace = true;
					}
				}

				bool declaration = false;
				Dwarf_Attribute attr_mem;
				if (tag != DW_TAG_class_type &&
					dwarf_attr(current_die, DW_AT_declaration, &attr_mem, &error) ==
						DW_DLV_OK) {
					Dwarf_Bool flag = 0;
					if (dwarf_formflag(attr_mem, &flag, &error) == DW_DLV_OK) {
						declaration = flag != 0;
					}
					dwarf_dealloc(dwarf, attr_mem, DW_DLA_ATTR);
				}

				if (!declaration) {
					// Let's be curious and look deeper in the tree, function are not necessarily at the first level,
					// but might be nested inside a namespace, structure, a function, an inlined function etc.
					branch_has_pc = deep_first_search_by_pc(fobj, current_die, pc, ns, cb);
				}

				if (!branch_has_pc) {
					branch_has_pc = die_has_pc(fobj, current_die, pc);
				}

				if (branch_has_pc) {
					cb(current_die, ns);
				}

				std::int32_t result = dwarf_siblingof(dwarf, current_die, &sibling_die, &error);
				if (result == DW_DLV_ERROR) {
					return false;
				} else if (result == DW_DLV_NO_ENTRY) {
					break;
				}

				if (current_die != parent_die) {
					dwarf_dealloc(dwarf, current_die, DW_DLA_DIE);
					current_die = 0;
				}

				if (has_namespace) {
					has_namespace = false;
					ns.pop_back();
				}
				current_die = sibling_die;
			}

			if (has_namespace) {
				ns.pop_back();
			}
			return branch_has_pc;
		}

		static std::string die_call_file(Dwarf_Debug dwarf, Dwarf_Die die, Dwarf_Die cu_die) {
			Dwarf_Attribute attr_mem;
			Dwarf_Error error = DW_DLE_NE;
			Dwarf_Unsigned file_index;

			std::string file;

			if (dwarf_attr(die, DW_AT_call_file, &attr_mem, &error) == DW_DLV_OK) {
				if (dwarf_formudata(attr_mem, &file_index, &error) != DW_DLV_OK) {
					file_index = 0;
				}
				dwarf_dealloc(dwarf, attr_mem, DW_DLA_ATTR);

				if (file_index == 0) {
					return file;
				}

				char** srcfiles = 0;
				Dwarf_Signed file_count = 0;
				if (dwarf_srcfiles(cu_die, &srcfiles, &file_count, &error) == DW_DLV_OK) {
					if (file_count > 0 && file_index <= static_cast<Dwarf_Unsigned>(file_count)) {
						file = std::string(srcfiles[file_index - 1]);
					}

					// Deallocate all strings!
					for (std::int32_t i = 0; i < file_count; ++i) {
						dwarf_dealloc(dwarf, srcfiles[i], DW_DLA_STRING);
					}
					dwarf_dealloc(dwarf, srcfiles, DW_DLA_LIST);
				}
			}
			return file;
		}

		Dwarf_Die find_die(dwarf_fileobject& fobj, Dwarf_Addr addr) {
			// Let's get to work! First see if we have a debug_aranges section, so we can speed up the search.

			Dwarf_Debug dwarf = fobj.dwarf_handle.get();
			Dwarf_Error error = DW_DLE_NE;
			Dwarf_Arange* aranges;
			Dwarf_Signed arange_count;

			Dwarf_Die returnDie;
			bool found = false;
			if (dwarf_get_aranges(dwarf, &aranges, &arange_count, &error) != DW_DLV_OK) {
				aranges = NULL;
			}

			if (aranges) {
				// We have aranges. Get the one where our address is.
				Dwarf_Arange arange;
				if (dwarf_get_arange(aranges, arange_count, addr, &arange, &error) == DW_DLV_OK) {

					// We found our address. Get the compilation-unit DIE offset represented by the given address range.
					Dwarf_Off cu_die_offset;
					if (dwarf_get_cu_die_offset(arange, &cu_die_offset, &error) == DW_DLV_OK) {
						// Get the DIE at the offset returned by the aranges search. We set is_info to 1 to specify
						// that the offset is from the .debug_info section (and not .debug_types)
						std::int32_t dwarf_result = dwarf_offdie_b(dwarf, cu_die_offset, 1, &returnDie, &error);
						found = (dwarf_result == DW_DLV_OK);
					}
					dwarf_dealloc(dwarf, arange, DW_DLA_ARANGE);
				}
			}

			if (found) {
				return returnDie; // The caller is responsible for freeing the die
			}

			// The search for aranges failed. Try to find our address by scanning all compilation units.
			Dwarf_Unsigned next_cu_header;
			Dwarf_Half tag = 0;
			returnDie = 0;

			while (!found && dwarf_next_cu_header_d(dwarf, 1, 0, 0, 0, 0, 0, 0, 0, 0, &next_cu_header, 0, &error) == DW_DLV_OK) {
				if (returnDie) {
					dwarf_dealloc(dwarf, returnDie, DW_DLA_DIE);
				}
				if (dwarf_siblingof(dwarf, 0, &returnDie, &error) == DW_DLV_OK) {
					if ((dwarf_tag(returnDie, &tag, &error) == DW_DLV_OK) &&
						tag == DW_TAG_compile_unit) {
						if (die_has_pc(fobj, returnDie, addr)) {
							found = true;
						}
					}
				}
			}

			if (found) {
				while (dwarf_next_cu_header_d(dwarf, 1, 0, 0, 0, 0, 0, 0, 0, 0, &next_cu_header, 0, &error) == DW_DLV_OK) {
					// Reset the cu header state. Libdwarf's next_cu_header API keeps its own iterator per Dwarf_Debug
					// that can't be reset. We need to keep fetching elements until the end.
				}
			}

			if (found) {
				return returnDie;
			}

			// We couldn't find any compilation units with ranges or a high/low pc. Try again by looking at all DIEs in all compilation units.
			Dwarf_Die cudie;
			while (dwarf_next_cu_header_d(dwarf, 1, 0, 0, 0, 0, 0, 0, 0, 0, &next_cu_header, 0, &error) == DW_DLV_OK) {
				if (dwarf_siblingof(dwarf, 0, &cudie, &error) == DW_DLV_OK) {
					Dwarf_Die die_mem = 0;
					Dwarf_Die resultDie = find_fundie_by_pc(fobj, cudie, addr, die_mem);
					if (resultDie) {
						found = true;
						break;
					}
				}
			}

			if (found) {
				while (dwarf_next_cu_header_d(dwarf, 1, 0, 0, 0, 0, 0, 0, 0, 0, &next_cu_header, 0, &error) == DW_DLV_OK) {
					// Reset the cu header state. Libdwarf's next_cu_header API keeps its own iterator per Dwarf_Debug
					// that can't be reset. We need to keep fetching elements until the end.
				}
			}

			if (found) {
				return cudie;
			}

			return NULL;
		}
	};
#endif // BACKWARD_HAS_DWARF

#endif // BACKWARD_TARGET_LINUX

#if defined(BACKWARD_TARGET_APPLE)

	class TraceResolver : public TraceResolverBase {
	public:
		void LoadAddresses(void* const* addresses, std::int32_t addressCount) override {
			if (addressCount == 0) {
				return;
			}
			_symbols.reset(backtrace_symbols(addresses, addressCount));
		}

		ResolvedTrace Resolve(ResolvedTrace trace) override {
			// parse:
			// <n>  <file>  <addr>  <mangled-name> + <offset>
			char* filename = _symbols[trace.Index];

			// skip "<n>  "
			while (*filename && *filename != ' ')
				filename++;
			while (*filename == ' ')
				filename++;

			// find start of <mangled-name> from end (<file> may contain a space)
			char* p = filename + std::strlen(filename) - 1;
			// skip to start of " + <offset>"
			while (p > filename && *p != ' ')
				p--;
			while (p > filename && *p == ' ')
				p--;
			while (p > filename && *p != ' ')
				p--;
			while (p > filename && *p == ' ')
				p--;
			char* funcname_end = p + 1;

			// skip to start of "<manged-name>"
			while (p > filename && *p != ' ')
				p--;
			char* funcname = p + 1;

			// skip to start of "  <addr>  "
			while (p > filename && *p == ' ')
				p--;
			while (p > filename && *p != ' ')
				p--;
			while (p > filename && *p == ' ')
				p--;

			// skip "<file>", handling the case where it contains a
			char* filename_end = p + 1;
			if (p == filename) {
				// something went wrong, give up
				filename_end = filename + std::strlen(filename);
				funcname = filename_end;
			}
			trace.ObjectFilename.assign(filename, filename_end); // ok even if filename_end is the ending \0 (then we assign entire string)

			if (*funcname != '\0') { // if it's not end of string
				*funcname_end = '\0';

				trace.ObjectFunction = this->Demangle(funcname);
				trace.ObjectFunction += " ";
				trace.ObjectFunction += (funcname_end + 1);
				trace.Source.Function = trace.ObjectFunction; // we cannot do better.
			}
			return trace;
		}

	private:
		Implementation::Handle<char**> _symbols;
	};

#endif // BACKWARD_TARGET_APPLE

#if defined(BACKWARD_TARGET_WINDOWS)

	// Load all symbol info
	// Based on: https://stackoverflow.com/questions/6205981/windows-c-stack-trace-from-a-running-app/28276227#28276227

	struct ModuleData {
		std::string image_name;
		std::string module_name;
		void* base_address;
		DWORD load_size;
	};

	class GetModuleInfo {
		HANDLE process;
		static const std::int32_t BufferSize = 4096;

	public:
		GetModuleInfo(HANDLE h) : process(h) {}

		ModuleData operator()(HMODULE module) {
			using namespace std::string_view_literals;

			ModuleData ret;
			char temp[BufferSize];
			MODULEINFO mi;

			::GetModuleInformation(process, module, &mi, sizeof(mi));
			ret.base_address = mi.lpBaseOfDll;
			ret.load_size = mi.SizeOfImage;

			::GetModuleFileNameExA(process, module, temp, sizeof(temp));
			ret.image_name = temp;
			::GetModuleBaseNameA(process, module, temp, sizeof(temp));
			ret.module_name = temp;
			if (ret.module_name == "KERNEL32.DLL"sv) {
				ret.module_name = "kernel32.dll"sv;	// This library is usually returned with upper-case name
			}
			::SymLoadModule64(process, 0, &ret.image_name[0], &ret.module_name[0], (DWORD64)ret.base_address, ret.load_size);
			return ret;
		}
	};

	class TraceResolver : public TraceResolverBase {
	public:
		TraceResolver() {
			HANDLE process = ::GetCurrentProcess();

			DWORD cbNeeded;
			std::vector<HMODULE> module_handles(1);
			::SymInitialize(process, NULL, FALSE);
			DWORD symOptions = ::SymGetOptions();
			symOptions |= SYMOPT_LOAD_LINES | SYMOPT_UNDNAME;
			::SymSetOptions(symOptions);
			::EnumProcessModules(process, &module_handles[0],
				static_cast<DWORD>(module_handles.size() * sizeof(HMODULE)), &cbNeeded);
			module_handles.resize(cbNeeded / sizeof(HMODULE));
			::EnumProcessModules(process, &module_handles[0],
				static_cast<DWORD>(module_handles.size() * sizeof(HMODULE)), &cbNeeded);
			std::transform(module_handles.begin(), module_handles.end(),
				std::back_inserter(_modules), GetModuleInfo(process));

			void* base = _modules[0].base_address;
			IMAGE_NT_HEADERS* h = ::ImageNtHeader(base);
			_imageType = h->FileHeader.Machine;
		}

		~TraceResolver() {
			::SymCleanup(::GetCurrentProcess());
		}

		static const std::int32_t MaxSymbolLength = 255;
		struct symbol_t {
			SYMBOL_INFO sym;
			char buffer[MaxSymbolLength];
		} sym;

		DWORD64 displacement;

		ResolvedTrace Resolve(ResolvedTrace t) override {
			HANDLE process = ::GetCurrentProcess();

			std::memset(&sym, 0, sizeof(sym));
			sym.sym.SizeOfStruct = sizeof(SYMBOL_INFO);
			sym.sym.MaxNameLen = MaxSymbolLength;

			char name[256];
			if (::SymFromAddr(process, (ULONG64)t.Address, &displacement, &sym.sym)) {
				::UnDecorateSymbolName(sym.sym.Name, (PSTR)name, 256, UNDNAME_COMPLETE);
				strcat_s(name, "()");
			} else {
				name[0] = '\0';
			}

			DWORD offset = 0;
			IMAGEHLP_LINEW64 lineW = { sizeof(IMAGEHLP_LINEW64) };
			if (::SymGetLineFromAddrW64(process, (ULONG64)t.Address, &offset, &lineW)) {
				t.Source.Filename = Death::Utf8::FromUtf16(lineW.FileName);
				t.Source.Line = lineW.LineNumber;
				t.Source.Column = offset;
			}

			t.Source.Function = name;
			t.ObjectFunction = name;

			for (auto& m : _modules) {
				if ((std::uintptr_t)m.base_address <= (std::uintptr_t)t.Address && (std::uintptr_t)t.Address < (std::uintptr_t)m.base_address + m.load_size) {
					t.ObjectFilename = m.module_name;
					t.ObjectBaseAddress = m.base_address;
					break;
				}
			}

			return t;
		}

		DWORD GetMachineType() const {
			return _imageType;
		}

	private:
		DWORD _imageType;
		std::vector<ModuleData> _modules;
	};

#endif

	/** @brief Represents a file with source code */
	class SourceFile {
	public:
#ifndef DOXYGEN_GENERATING_OUTPUT
		typedef std::vector<std::pair<std::int32_t, std::string>> lines_t;
#endif

		SourceFile() {}

		SourceFile(const std::string& path) {
			// If BACKWARD_CXX_SOURCE_PREFIXES is set then assume it contains a colon-separated list of path prefixes.
			// Try prepending each to the given path until a valid file is found.
			const std::vector<std::string>& prefixes = GetPathsFromEnvVariable();
			if (!prefixes.empty()) {
				std::size_t lastOffset = std::string::npos;
				while (true) {
					std::size_t offset = path.find_last_of("/\\", lastOffset);
					if (offset == std::string::npos) {
						break;
					}

					for (std::size_t i = 0; i < prefixes.size(); i++) {
						const std::string& prefix = prefixes[i];
						std::string newPath = prefix;
						if (prefix[prefix.size() - 1] != '/' && prefix[prefix.size() - 1] != '\\') {
#if defined(BACKWARD_TARGET_WINDOWS)
							newPath += '\\';
#else
							newPath += '/';
#endif
						}
						newPath.append(path, offset + 1);

						_file.reset(new std::ifstream(newPath.c_str()));
						if (IsOpen()) {
							return;
						}
					}

					if (offset == 0) {
						break;
					}

					lastOffset = offset - 1;
				}

				for (std::size_t i = 0; i < prefixes.size(); ++i) {
					// Double slashes (//) should not be a problem.
					std::string newPath = prefixes[i];
#if defined(BACKWARD_TARGET_WINDOWS)
					newPath += '\\';
#else
					newPath += '/';
#endif
					newPath += path;

					_file.reset(new std::ifstream(newPath.c_str()));
					if (IsOpen()) {
						return;
					}
				}
			}
			
			// If no valid file found then fallback to opening the path as-is.
			_file.reset(new std::ifstream(path.c_str()));
		}

		SourceFile(const SourceFile&) = delete;
		SourceFile& operator=(const SourceFile&) = delete;

		/** @brief Returns `true` if the file can be read */
		bool IsOpen() const {
			return _file->is_open();
		}

		/** @brief Returns lines from the file within the specified range */
		lines_t& GetLines(std::int32_t lineStart, std::int32_t lineCount, lines_t& lines) {
			// This function make uses of the dumbest algo ever:
			//	1) seek(0)
			//	2) read lines one by one and discard until line_start
			//	3) read line one by one until line_start + line_count
			//
			// If you are getting snippets many time from the same file, it is
			// somewhat a waste of CPU, feel free to benchmark and propose a
			// better solution ;)

			_file->clear();
			_file->seekg(0);
			std::string line;
			std::int32_t lineIdx;

			for (lineIdx = 1; lineIdx < lineStart; ++lineIdx) {
				std::getline(*_file, line);
				if (!*_file) {
					return lines;
				}
			}

			// think of it like a lambda in C++98 ;)
			// but look, I will reuse it two times!
			// What a good boy am I.
			struct isspace {
				bool operator()(char c) {
					return std::isspace(c);
				}
			};

			bool started = false;
			for (; lineIdx < lineStart + lineCount; ++lineIdx) {
				std::getline(*_file, line);
				if (!*_file) {
					return lines;
				}
				if (!started) {
					if (std::find_if(line.begin(), line.end(), not_isspace()) == line.end()) {
						continue;
					}
					started = true;
				}
				lines.push_back(make_pair(lineIdx, line));
			}

			lines.erase(std::find_if(lines.rbegin(), lines.rend(), not_isempty()).base(), lines.end());
			return lines;
		}

		/** @overload */
		lines_t GetLines(std::int32_t lineStart, std::int32_t lineCount) {
			lines_t lines;
			return GetLines(lineStart, lineCount, lines);
		}

		void swap(SourceFile& b) {
			_file.swap(b._file);
		}

		SourceFile(SourceFile&& from) noexcept : _file(nullptr) {
			swap(from);
		}
		SourceFile& operator=(SourceFile&& from) noexcept {
			swap(from);
			return *this;
		}

		// Allow adding to paths gotten from BACKWARD_CXX_SOURCE_PREFIXES after loading the
		// library; this can be useful when the library is loaded when the locations are unknown
		// Warning: Because this edits the static paths variable, it is *not* thread-safe
		static void AddPathToEnvVariable(const std::string& toAdd) {
			GetMutablePathsFromEnvVariable().push_back(toAdd);
		}

	private:
#ifndef DOXYGEN_GENERATING_OUTPUT
		// Doxygen 1.12.0 outputs also private structs/unions even if it shouldn't
		// There is no find_if_not in C++98, lets do something crappy to workaround
		struct not_isspace {
			bool operator()(char c) {
				return !std::isspace(c);
			}
		};
		// And define this one here because C++98 is not happy with local defined struct passed to template functions
		struct not_isempty {
			bool operator()(const lines_t::value_type& p) {
				return !(std::find_if(p.second.begin(), p.second.end(), not_isspace()) == p.second.end());
			}
		};
#endif

		Implementation::Handle<std::ifstream*, Implementation::DefaultDelete<std::ifstream*>> _file;

		static std::vector<std::string> GetPathsFromEnvVariableImpl() {
			std::vector<std::string> paths;
#if defined(BACKWARD_TARGET_WINDOWS)
			char* prefixesStr; std::size_t length;
			if (_dupenv_s(&prefixesStr, &length, "BACKWARD_CXX_SOURCE_PREFIXES") == 0 && prefixesStr != nullptr) {
				if (prefixesStr[0] != '\0') {
					paths = Implementation::SplitSourcePrefixes(prefixesStr);
				}
				std::free(prefixesStr);
			}
#else
			const char* prefixesStr = std::getenv("BACKWARD_CXX_SOURCE_PREFIXES");
			if (prefixesStr != nullptr && prefixesStr[0] != '\0') {
				paths = Implementation::SplitSourcePrefixes(prefixesStr);
			}
#endif
			return paths;
		}

		static std::vector<std::string>& GetMutablePathsFromEnvVariable() {
			static volatile std::vector<std::string> paths = GetPathsFromEnvVariableImpl();
			return const_cast<std::vector<std::string>&>(paths);
		}

		static const std::vector<std::string>& GetPathsFromEnvVariable() {
			return GetMutablePathsFromEnvVariable();
		}
	};

	/** @brief Helps to create snippets from source code */
	class SnippetFactory {
	public:
#ifndef DOXYGEN_GENERATING_OUTPUT
		typedef SourceFile::lines_t lines_t;
#endif

		/** @brief Creates a snippet from a given file and line number */
		lines_t GetSnippet(const std::string& filename, std::int32_t lineStart, std::int32_t contextSize) {
			SourceFile& srcFile = GetSourceFile(filename);
			std::int32_t start = lineStart - (contextSize / 2);
			return srcFile.GetLines(start, contextSize);
		}

		/** @brief Creates a combined snippet from given files and line numbers */
		lines_t GetCombinedSnippet(const std::string& filenameA, std::int32_t lineA, const std::string& filenameB, std::int32_t lineB, std::int32_t contextSize) {
			SourceFile& srcFileA = GetSourceFile(filenameA);
			SourceFile& srcFileB = GetSourceFile(filenameB);

			lines_t lines = srcFileA.GetLines(lineA - (contextSize / 4), contextSize / 2);
			srcFileB.GetLines(lineB - (contextSize / 4), contextSize / 2, lines);
			return lines;
		}

		/** @brief Creates a coalesced snippet from a given file and line numbers */
		lines_t GetCoalescedSnippet(const std::string& filename, std::int32_t lineA, std::int32_t lineB, std::int32_t contextSize) {
			SourceFile& srcFile = GetSourceFile(filename);

			std::int32_t a = std::min(lineA, lineB);
			std::int32_t b = std::max(lineA, lineB);

			if ((b - a) < (contextSize / 3)) {
				return srcFile.GetLines((a + b - contextSize + 1) / 2, contextSize);
			}

			lines_t lines = srcFile.GetLines(a - contextSize / 4, contextSize / 2);
			srcFile.GetLines(b - contextSize / 4, contextSize / 2, lines);
			return lines;
		}

	private:
		typedef Implementation::Hashtable<std::string, SourceFile>::type SourceFiles;
		SourceFiles _srcFiles;

		SourceFile& GetSourceFile(const std::string& filename) {
			SourceFiles::iterator it = _srcFiles.find(filename);
			if (it != _srcFiles.end()) {
				return it->second;
			}
			SourceFile& newSrcFile = _srcFiles[filename];
			newSrcFile = SourceFile(filename);
			return newSrcFile;
		}
	};

	/** @brief Feature flags for @ref ExceptionHandling */
	enum class Flags {
		None = 0,
		/** @brief Write exception info to `stdout` */
		UseStdError = 0x01,
		/** @brief Colorize using virtual terminal sequences */
		Colorized = 0x02,
		/** @brief Include code snippets */
		IncludeSnippet = 0x04,
		/** @brief Create memory dump */
		CreateMemoryDump = 0x08
	};

	DEATH_ENUM_FLAGS(Flags);

	namespace Implementation
	{
		class StreambufWrapper : public std::streambuf {
		public:
			StreambufWrapper(IO::Stream* sink) : _sink(sink) {}

			StreambufWrapper(const StreambufWrapper&) = delete;
			StreambufWrapper& operator=(const StreambufWrapper&) = delete;

			int_type underflow() override {
				return traits_type::eof();
			}
			int_type overflow(int_type ch) override {
				if (traits_type::not_eof(ch) && _sink->Write(&ch, sizeof(ch) > 0)) {
					return ch;
				}
				return traits_type::eof();
			}

			std::streamsize xsputn(const char_type* s, std::streamsize count) override {
				return static_cast<std::streamsize>(_sink->Write(s, sizeof(*s) * static_cast<std::int64_t>(count)));
			}

		private:
			IO::Stream* _sink;
		};

#	if defined(BACKWARD_TARGET_LINUX) || defined(BACKWARD_TARGET_WINDOWS)

		enum class Color {
			BrightGreen = 92, Yellow = 33, BrightYellow = 93, Green = 32, Purple = 35, Reset = 0, Bold = 1, Dark = 2
		};

		class Colorize {
		public:
			Colorize(std::ostream& os) : _os(os), _enabled(false), _reset(false) {
			}

			void SetEnabled(bool enable) {
				_enabled = enable;
			}

			void SetColor(Color code) {
				if (!_enabled) {
					return;
				}

				// Assume that the terminal can handle basic colors
				_os << "\033[" << static_cast<std::int32_t>(code) << "m";
				_reset = (code != Color::Reset);
			}

			~Colorize() {
				if (_reset) {
					SetColor(Color::Reset);
				}
			}

		private:
			std::ostream& _os;
			bool _enabled;
			bool _reset;
		};

#	else

		enum class Color {
			BrightGreen = 0, Yellow = 0, BrightYellow = 0, Green = 0, Purple = 0, Reset = 0, Bold = 0, Dark = 0
		};

		class Colorize {
		public:
			Colorize(std::ostream&) {
			}
			void SetEnabled(bool) {
			}
			void SetColor(Color) {
			}
		};

#	endif

		using PathComponents = std::vector<std::string>;

		class PathTrie {
		public:
			explicit PathTrie(std::string _root) : _root(std::move(_root)) {
			};

			void Insert(const PathComponents& path) {
				Insert(path, (std::int32_t)path.size() - 2);
			}

			PathComponents Disambiguate(const PathComponents& path) const {
				using namespace std::string_view_literals;

				PathComponents result;
				const PathTrie* current = this;
				result.push_back(current->_root);
				std::int32_t count = (std::int32_t)(path.size() - 2);
				for (std::int32_t i = count; i >= 1; i--) {
					if (current->_downstreamBranches == 1 && i < count - 2) {	// Include at least 2 subdirectories
						break;
					}
					const std::string& component = path[i];
					if (component == "Sources"sv) {	// "Sources" directory is usually root for all source files
						result.emplace_back("…"sv);
						break;
					}

					current = current->_edges.at(component).get();
					result.push_back(current->_root);
				}
				std::reverse(result.begin(), result.end());
				return result;
			}

		private:
			size_t _downstreamBranches = 1;
			std::string _root;
			std::unordered_map<std::string, std::unique_ptr<PathTrie>> _edges;

			void Insert(const PathComponents& path, std::int32_t i) {
				if (i < 0) {
					return;
				}
				if (!_edges.count(path[i])) {
					if (!_edges.empty()) {
						_downstreamBranches++; // This is to deal with making leaves have count 1
					}
					_edges.insert({ path[i], std::make_unique<PathTrie>(path[i]) });
				}
				_downstreamBranches -= _edges.at(path[i])->_downstreamBranches;
				_edges.at(path[i])->Insert(path, i - 1);
				_downstreamBranches += _edges.at(path[i])->_downstreamBranches;
			}
		};
	}

	/** @brief Exception and stack trace printer */
	class Printer {
	public:
		Flags FeatureFlags;
		bool Address;
		bool Object;
		std::int32_t InlinerContextSize;
		std::int32_t TraceContextSize;

		Printer()
			: FeatureFlags(Flags::None), Address(false), Object(false), InlinerContextSize(5), TraceContextSize(7) {}

		/** @brief Prints the specified stack trace to a stream */
		template<typename ST>
		void Print(ST& st, IO::Stream* s, std::uint32_t exceptionCode = 0) {
			Implementation::StreambufWrapper obuf(s);
			std::ostream os(&obuf);
			Implementation::Colorize colorize(os);
			colorize.SetEnabled((FeatureFlags & Flags::Colorized) == Flags::Colorized);
			PrintStacktrace(st, os, exceptionCode, colorize);
		}

		/** @overload */
		template<typename ST>
		void Print(ST& st, std::ostream& os, std::uint32_t exceptionCode = 0) {
			Implementation::Colorize colorize(os);
			colorize.SetEnabled((FeatureFlags & Flags::Colorized) == Flags::Colorized);
			PrintStacktrace(st, os, exceptionCode, colorize);
		}

		/** @brief Prints standard prologue of the text log file */
		void PrintFilePrologue(IO::Stream* s) {
			auto p = Containers::DateTime::UtcNow().Partitioned();

			char buffer[64];
			std::int32_t length = (std::int32_t)formatInto(buffer, "{:.2}:{:.2}:{:.2}.{:.3} [F] ",
				p.Hour, p.Minute, p.Second, p.Millisecond);
			if (length > 0) {
				s->Write(buffer, length);
			}
		}

		/** @brief Returns stack trace resolver */
		TraceResolver const& GetResolver() const {
			return _resolver;
		}

	private:
		TraceResolver _resolver;
		SnippetFactory _snippets;

		static std::vector<std::string_view> Split(std::string_view s, std::string_view delims) {
			std::vector<std::string_view> vec;
			std::size_t oldPos = 0;
			std::size_t pos = 0;
			while ((pos = s.find_first_of(delims, oldPos)) != std::string::npos) {
				vec.emplace_back(s.substr(oldPos, pos - oldPos));
				oldPos = pos + 1;
			}
			vec.emplace_back(s.substr(oldPos));
			return vec;
		}

		template<typename C>
		static std::string Join(const C& container, const std::string_view delim) {
			auto iter = std::begin(container);
			auto end = std::end(container);
			std::string str;
			if (std::distance(iter, end) > 0) {
				str += *iter;
				while (++iter != end) {
					str += delim;
					str += *iter;
				}
			}
			return str;
		}

		static Implementation::PathComponents ParsePath(std::string_view path) {
			using namespace std::string_view_literals;
			
			Implementation::PathComponents parts;
			for (auto part : Split(path, "/\\"sv)) {
				if (parts.empty()) {
					parts.emplace_back(part);
				} else {
					if (part.empty()) {
						// No-op
					} else if (part == ".") {
						// No-op
					} else if (part == "..") {
						// Cases where we have unresolvable ..'s, e.g. ./../../demo.exe
						if (parts.back() == "." || parts.back() == "..") {
							parts.emplace_back(part);
						} else {
							parts.pop_back();
						}
					} else {
						parts.emplace_back(part);
					}
				}
			}
			return parts;
		}

		static void AddPath(const std::string& path, std::unordered_map<std::string, Implementation::PathComponents>& parsedPaths, std::unordered_map<std::string, Implementation::PathTrie>& tries) {
			if (!path.empty() && !parsedPaths.count(path)) {
				auto parsedPath = ParsePath(path);
				auto& fileName = parsedPath.back();
				parsedPaths.insert({ path, parsedPath });
				if (tries.count(fileName) == 0) {
					tries.insert({ fileName, Implementation::PathTrie(fileName) });
				}
				tries.at(fileName).Insert(parsedPath);
			}
		}

		template<typename ST>
		void PrintStacktrace(ST& st, std::ostream& os, std::uint32_t exceptionCode, Implementation::Colorize& colorize) {
			using namespace std::string_view_literals;

			PrintHeader(os, st.GetThreadId(), exceptionCode, colorize);
			_resolver.LoadStacktrace(st);

			std::unordered_map<std::string, Implementation::PathComponents> parsedPaths;
			std::unordered_map<std::string, Implementation::PathTrie> tries;

#	if defined(BACKWARD_TARGET_WINDOWS) || defined(BACKWARD_TARGET_LINUX)
			bool failed = false;
#	endif
			std::vector<ResolvedTrace> resolvedTrace(st.size());
			for (std::size_t traceIdx = 0; traceIdx < st.size(); ++traceIdx) {
				const auto& trace = resolvedTrace[traceIdx] = _resolver.Resolve(st[traceIdx]);
				
				// Collect all used paths
				AddPath(trace.ObjectFilename, parsedPaths, tries);
				AddPath(trace.Source.Filename, parsedPaths, tries);

				for (std::size_t inlinerIdx = trace.Inliners.size(); inlinerIdx > 0; --inlinerIdx) {
					const ResolvedTrace::SourceLoc& inlinerLoc = trace.Inliners[inlinerIdx - 1];
					AddPath(inlinerLoc.Filename, parsedPaths, tries);
				}

#	if defined(BACKWARD_TARGET_WINDOWS) || defined(BACKWARD_TARGET_LINUX)
				if (trace.ObjectFunction.empty()) {
					failed = true;
				}
#	endif
			}

			// Finalize paths
			std::unordered_map<std::string, std::string> pathMap;
			for (auto& [raw, parsedPath] : parsedPaths) {
				std::string newPath = Join(tries.at(parsedPath.back()).Disambiguate(parsedPath),
#	if defined(BACKWARD_TARGET_WINDOWS)
					"\\"sv
#	else
					"/"sv
#	endif
				);
				pathMap.insert({ raw, newPath });
			}

			for (std::size_t traceIdx = 0; traceIdx < st.size(); ++traceIdx) {
				PrintTrace(os, resolvedTrace[traceIdx], colorize, pathMap);
			}

#	if defined(BACKWARD_TARGET_WINDOWS) || defined(BACKWARD_TARGET_LINUX)
			if (failed) {
				colorize.SetColor(Implementation::Color::BrightYellow);
				os << "Make sure corresponding .pdb files are accessible to show full stack trace. ";
			}
#	endif
#	if defined(BACKWARD_TARGET_WINDOWS)
			colorize.SetColor(Implementation::Color::Yellow);
			os << "Memory dump file has been saved to ";
			colorize.SetColor(Implementation::Color::BrightGreen);
			os << "\"CrashDumps\"";
			colorize.SetColor(Implementation::Color::Yellow);
			os << " directory.\n";
			colorize.SetColor(Implementation::Color::Reset);
#	endif
		}

		//template<typename IT>
		//void PrintStacktrace(IT begin, IT end, std::ostream& os, std::size_t thread_id, std::uint32_t exceptionCode, Implementation::Colorize& colorize) {
		//	PrintHeader(os, thread_id, exceptionCode, colorize);
		//	for (; begin != end; ++begin) {
		//		PrintTrace(os, *begin, colorize);
		//	}
		//}

		void PrintHeader(std::ostream& os, std::size_t threadId, std::uint32_t exceptionCode, Implementation::Colorize& colorize) {
			colorize.SetColor(Implementation::Color::Bold);
			os << "The application exited unexpectedly";
			colorize.SetColor(Implementation::Color::Reset);
			if (threadId != 0) {
				os << " in thread " << threadId;
			}
			if (exceptionCode != 0) {
#	if defined(BACKWARD_TARGET_WINDOWS)
				os << " due to exception 0x" << std::hex << std::uppercase << std::setw(8) << std::setfill('0')
					<< std::uint64_t(exceptionCode) << std::dec << std::setw(0) << std::setfill(' ');

				switch (exceptionCode) {
					case EXCEPTION_ACCESS_VIOLATION: os << " (EXCEPTION_ACCESS_VIOLATION)"; break;
					case EXCEPTION_ARRAY_BOUNDS_EXCEEDED: os << " (EXCEPTION_ARRAY_BOUNDS_EXCEEDED)"; break;
					case EXCEPTION_BREAKPOINT: os << " (EXCEPTION_BREAKPOINT)"; break;
					case EXCEPTION_DATATYPE_MISALIGNMENT: os << " (EXCEPTION_DATATYPE_MISALIGNMENT)"; break;
					case EXCEPTION_FLT_DENORMAL_OPERAND: os << " (EXCEPTION_FLT_DENORMAL_OPERAND)"; break;
					case EXCEPTION_FLT_DIVIDE_BY_ZERO: os << " (EXCEPTION_FLT_DIVIDE_BY_ZERO)"; break;
					case EXCEPTION_FLT_INEXACT_RESULT: os << " (EXCEPTION_FLT_INEXACT_RESULT)"; break;
					case EXCEPTION_FLT_INVALID_OPERATION: os << " (EXCEPTION_FLT_INVALID_OPERATION)"; break;
					case EXCEPTION_FLT_OVERFLOW: os << " (EXCEPTION_FLT_OVERFLOW)"; break;
					case EXCEPTION_FLT_STACK_CHECK: os << " (EXCEPTION_FLT_STACK_CHECK)"; break;
					case EXCEPTION_FLT_UNDERFLOW: os << " (EXCEPTION_FLT_UNDERFLOW)"; break;
					case EXCEPTION_ILLEGAL_INSTRUCTION: os << " (EXCEPTION_ILLEGAL_INSTRUCTION)"; break;
					case EXCEPTION_IN_PAGE_ERROR: os << " (EXCEPTION_IN_PAGE_ERROR)"; break;
					case EXCEPTION_INT_DIVIDE_BY_ZERO: os << " (EXCEPTION_INT_DIVIDE_BY_ZERO)"; break;
					case EXCEPTION_INT_OVERFLOW: os << " (EXCEPTION_INT_OVERFLOW)"; break;
					case EXCEPTION_INVALID_DISPOSITION: os << " (EXCEPTION_INVALID_DISPOSITION)"; break;
					case EXCEPTION_NONCONTINUABLE_EXCEPTION: os << " (EXCEPTION_NONCONTINUABLE_EXCEPTION)"; break;
					case EXCEPTION_PRIV_INSTRUCTION: os << " (EXCEPTION_PRIV_INSTRUCTION)"; break;
					case EXCEPTION_SINGLE_STEP: os << " (EXCEPTION_SINGLE_STEP)"; break;
					case EXCEPTION_STACK_OVERFLOW: os << " (EXCEPTION_STACK_OVERFLOW)"; break;
				}
#	else
				os << " due to signal " << exceptionCode;
#		if defined(BACKWARD_TARGET_LINUX) && defined(__GLIBC__) && __GLIBC__*100 + __GLIBC_MINOR__ >= 232
				const char* signalName = sigabbrev_np(int(exceptionCode));
				if (signalName != nullptr) {
					os << " (SIG" << signalName << ")";
				}
#		endif
#	endif
			}
			os << " with following stack trace:\n";
		}

		void PrintTrace(std::ostream& os, const ResolvedTrace& trace, Implementation::Colorize& colorize, std::unordered_map<std::string, std::string>& pathMap) {
			if ((std::uintptr_t)trace.Address == UINTPTR_MAX) {
				// Skip usually the last frame on Linux
				return;
			}

			os << "#" << std::left << std::setw(2) << (trace.Index + 1) << std::right;
			bool alreadyIndented = true;

			if (!trace.Source.Filename.size() || Object) {
				if (!trace.ObjectFilename.empty()) {
					os << "   Library ";
					colorize.SetColor(Implementation::Color::BrightGreen);
					os << "\"";
					auto path = pathMap.find(trace.ObjectFilename);
					if (path != pathMap.end()) {
						os << path->second;
					} else {
						os << trace.ObjectFilename;
					}
					if (trace.ObjectBaseAddress != nullptr) {
						colorize.SetColor(Implementation::Color::Green);
						os << "!0x" << std::hex << std::uppercase << std::setw(8) << std::setfill('0')
							<< ((char*)trace.Address - (char*)trace.ObjectBaseAddress) << std::dec << std::setfill(' ');
						colorize.SetColor(Implementation::Color::BrightGreen);
					}
					os << "\"";
				} else {
					os << "   Source ";
					colorize.SetColor(Implementation::Color::BrightGreen);
					os << "\"<unknown>\"";
				}

				colorize.SetColor(Implementation::Color::Reset);
				if (!trace.ObjectFunction.empty()) {
					os << ", in ";
					colorize.SetColor(Implementation::Color::Bold);
					os << trace.ObjectFunction;
					colorize.SetColor(Implementation::Color::Reset);
				}
				os << " [0x" << std::hex << std::uppercase << std::setw(8) << std::setfill('0')
					<< std::uint64_t(trace.Address) << std::dec << std::setfill(' ') << "]\n";
				alreadyIndented = false;
			}

			for (std::size_t inlinerIdx = trace.Inliners.size(); inlinerIdx > 0; --inlinerIdx) {
				if (!alreadyIndented) {
					os << "   ";
				}
				const ResolvedTrace::SourceLoc& inlinerLoc = trace.Inliners[inlinerIdx - 1];
				PrintSourceLocation(os, colorize, pathMap, " │ ", inlinerLoc);
				if ((FeatureFlags & Flags::IncludeSnippet) == Flags::IncludeSnippet) {
					PrintSnippet(os, "    │ ", inlinerLoc, colorize, Implementation::Color::Purple, InlinerContextSize);
				}
				alreadyIndented = false;
			}

			if (trace.Source.Filename.size()) {
				if (!alreadyIndented) {
					os << "   ";
				}
				PrintSourceLocation(os, colorize, pathMap, "   ", trace.Source, trace.Address);
				if ((FeatureFlags & Flags::IncludeSnippet) == Flags::IncludeSnippet) {
					PrintSnippet(os, "      ", trace.Source, colorize, Implementation::Color::Yellow, TraceContextSize);
				}
			}
		}

		void PrintSnippet(std::ostream& os, const char* indent, const ResolvedTrace::SourceLoc& sourceLoc,
						  Implementation::Colorize& colorize, Implementation::Color colorCode, std::int32_t contextSize) {
			typedef SnippetFactory::lines_t lines_t;

			lines_t lines = _snippets.GetSnippet(sourceLoc.Filename, sourceLoc.Line, contextSize);
			for (lines_t::const_iterator it = lines.begin(); it != lines.end(); ++it) {
				if (it->first == sourceLoc.Line) {
					colorize.SetColor(colorCode);
					os << indent << ">";
				} else {
					colorize.SetColor(Implementation::Color::Dark);
					os << indent << " ";
				}
				os << std::setw(6) << it->first << ": " << it->second << "\n";
				colorize.SetColor(Implementation::Color::Reset);
			}
		}

		void PrintSourceLocation(std::ostream& os, Implementation::Colorize& colorize, std::unordered_map<std::string, std::string>& pathMap,
								 const char* indent, const ResolvedTrace::SourceLoc& sourceLoc, void* addr = nullptr) {
			os << indent << "Source ";
			colorize.SetColor(Implementation::Color::BrightGreen);
			os << "\"";
			auto path = pathMap.find(sourceLoc.Filename);
			if (path != pathMap.end()) {
				os << path->second;
			} else {
				os << sourceLoc.Filename;
			}
			colorize.SetColor(Implementation::Color::Green);
			os << ":" << std::setw(0) << sourceLoc.Line;
			colorize.SetColor(Implementation::Color::BrightGreen);
			os << "\"";
			colorize.SetColor(Implementation::Color::Reset);
			if (!sourceLoc.Function.empty()) {
				os << ", in ";
				colorize.SetColor(Implementation::Color::Bold);
				os << sourceLoc.Function;
				colorize.SetColor(Implementation::Color::Reset);
			}
			if (Address && addr != nullptr) {
				os << " [0x" << std::hex << std::uppercase << std::setw(8) << std::setfill('0')
					<< std::uint64_t(addr) << std::dec << std::setfill(' ') << "]";
			}
			os << "\n";
		}
	};

#if defined(BACKWARD_TARGET_LINUX) || defined(BACKWARD_TARGET_APPLE) || defined(DOXYGEN_GENERATING_OUTPUT)

	/** @brief Unhandled exception handling */
	class ExceptionHandling {
	public:
		/** @brief Destination stream for printing exception information */
		IO::Stream* Destination;
		/** @brief Feature flags */
		Flags FeatureFlags;

		ExceptionHandling(Flags flags = Flags::None)
			: Destination(nullptr), FeatureFlags(flags), _loaded(false) {
			auto& current = GetSingleton();
			if (current != nullptr) {
				return;
			}
			current = this;

			bool success = true;

			constexpr std::size_t StackSize = 1024 * 1024 * 8;
			_stackContent.reset(static_cast<char*>(std::malloc(StackSize)));
			if (_stackContent) {
				stack_t ss;
				ss.ss_sp = _stackContent.get();
				ss.ss_size = StackSize;
				ss.ss_flags = 0;
				if (sigaltstack(&ss, nullptr) < 0) {
					success = false;
				}
			} else {
				success = false;
			}

			for (std::int32_t i = 0; i < PosixSignalsCount; ++i) {
				struct sigaction action;
				std::memset(&action, 0, sizeof action);
				action.sa_flags = static_cast<std::int32_t>(SA_SIGINFO | SA_ONSTACK | SA_NODEFER | SA_RESETHAND);
				sigfillset(&action.sa_mask);
				sigdelset(&action.sa_mask, PosixSignals[i]);
#	if defined(DEATH_TARGET_CLANG)
#		pragma clang diagnostic push
#		pragma clang diagnostic ignored "-Wdisabled-macro-expansion"
#	endif
				action.sa_sigaction = &SignalHandler;
#	if defined(DEATH_TARGET_CLANG)
#		pragma clang diagnostic pop
#	endif

				int r = sigaction(PosixSignals[i], &action, nullptr);
				if (r < 0) {
					success = false;
				}
			}

			_loaded = success;
		}

		/** @brief Returns `true` if the exception handling is successfully initialized */
		bool IsLoaded() const {
			return _loaded;
		}

		/** @brief Handles a given Unix signal */
		void HandleSignal(int sig, siginfo_t* info, void* _ctx) {
			ucontext_t* uctx = static_cast<ucontext_t*>(_ctx);

			StackTrace st;
			void* errorAddr = nullptr;
#	if defined(REG_RIP)		// 64-bit x86
			errorAddr = reinterpret_cast<void*>(uctx->uc_mcontext.gregs[REG_RIP]);
#	elif defined(REG_EIP)	// 32-bit x86
			errorAddr = reinterpret_cast<void*>(uctx->uc_mcontext.gregs[REG_EIP]);
#	elif defined(__arm__)
			errorAddr = reinterpret_cast<void*>(uctx->uc_mcontext.arm_pc);
#	elif defined(__aarch64__)
#		if defined(DEATH_TARGET_APPLE)
			errorAddr = reinterpret_cast<void*>(uctx->uc_mcontext->__ss.__pc);
#		else
			errorAddr = reinterpret_cast<void*>(uctx->uc_mcontext.pc);
#		endif
#	elif defined(__mips__)
			errorAddr = reinterpret_cast<void*>(
				reinterpret_cast<struct sigcontext*>(&uctx->uc_mcontext)->sc_pc);
#	elif defined(DEATH_TARGET_APPLE) && defined(DEATH_TARGET_POWERPC)
			errorAddr = reinterpret_cast<void*>(uctx->uc_mcontext->__ss.__srr0);
#	elif defined(DEATH_TARGET_POWERPC)
			errorAddr = reinterpret_cast<void*>(uctx->uc_mcontext.regs->nip);
#	elif defined(DEATH_TARGET_RISCV)
			errorAddr = reinterpret_cast<void*>(uctx->uc_mcontext.__gregs[REG_PC]);
#	elif defined(__s390x__)
			errorAddr = reinterpret_cast<void*>(uctx->uc_mcontext.psw.addr);
#	elif defined(DEATH_TARGET_APPLE) && defined(DEATH_TARGET_X86)
			errorAddr = reinterpret_cast<void*>(uctx->uc_mcontext->__ss.__rip);
#	elif defined(DEATH_TARGET_APPLE)
			errorAddr = reinterpret_cast<void*>(uctx->uc_mcontext->__ss.__eip);
#	elif defined(__loongarch__)
			errorAddr = reinterpret_cast<void*>(uctx->uc_mcontext.__pc);
#	else
#		warning "Unsupported CPU architecture"
#	endif
			if (errorAddr != nullptr) {
				st.LoadFrom(errorAddr, 32, reinterpret_cast<void*>(uctx), info->si_addr);
			} else {
				st.LoadHere(32, reinterpret_cast<void*>(uctx), info->si_addr);
			}

			bool shouldWriteToStdErr = (FeatureFlags & Flags::UseStdError) == Flags::UseStdError;

			IO::Stream* dest = Destination;
			bool shouldWriteToDest = (dest != nullptr);

			if (!shouldWriteToStdErr && !shouldWriteToDest) {
				return;
			}

			Printer printer;
			printer.Address = true;

			if (shouldWriteToStdErr) {
				printer.FeatureFlags = FeatureFlags;
				printer.Print(st, std::cerr, std::uint32_t(info->si_signo));
			}

			if (shouldWriteToDest) {
				printer.FeatureFlags = FeatureFlags & ~Flags::Colorized;
				printer.PrintFilePrologue(dest);
				printer.Print(st, dest, std::uint32_t(info->si_signo));
				dest->Flush();
			}
		}

	private:
		static constexpr std::int32_t ExceptionExitCode = 0xDEADBEEF;

		static constexpr std::int32_t PosixSignals[] = {
			// Signals for which the default action is "Core".
			SIGABRT,	// Abort signal from abort(3)
			SIGBUS,		// Bus error (bad memory access)
			SIGFPE,		// Floating point exception
			SIGILL,		// Illegal Instruction
			//SIGIOT,	// IOT trap. A synonym for SIGABRT
			//SIGQUIT,	// Quit from keyboard
			SIGSEGV,	// Invalid memory reference
			SIGSYS,		// Bad argument to routine (SVr4)
			SIGTRAP,	// Trace/breakpoint trap
			//SIGXCPU,	// CPU time limit exceeded (4.2BSD)
			//SIGXFSZ,	// File size limit exceeded (4.2BSD)
#	if defined(BACKWARD_TARGET_APPLE)
			SIGEMT,		// Emulation instruction executed
#	endif
		};

		static constexpr std::int32_t PosixSignalsCount = sizeof(PosixSignals) / sizeof(PosixSignals[0]);

		static ExceptionHandling*& GetSingleton() {
			static ExceptionHandling* current = nullptr;
			return current;
		}

		Implementation::Handle<char*> _stackContent;
		bool _loaded;

#	if defined(__GNUC__)
		__attribute__((noreturn))
#	endif
		static void SignalHandler(int sig, siginfo_t* info, void* _ctx) {
			auto* current = GetSingleton();
			current->HandleSignal(sig, info, _ctx);

			// Try to forward the signal
			raise(info->si_signo);

			// Terminate the process immediately
			_exit(ExceptionExitCode);
		}
	};

#endif // BACKWARD_TARGET_LINUX || BACKWARD_TARGET_APPLE || DOXYGEN_GENERATING_OUTPUT

#if defined(BACKWARD_TARGET_WINDOWS)

	class ExceptionHandling {
	public:
		IO::Stream* Destination;
		Flags FeatureFlags;

		ExceptionHandling(Flags flags = Flags::None)
			: Destination(nullptr), FeatureFlags(flags), _crashedThread(NULL),
				_status(HandlerStatus::Running), _skipFrames(0) {
			auto& current = GetSingleton();
			if (current != nullptr) {
				return;
			}
			current = this;

			constexpr std::int32_t ExceptionHandlerThreadStackSize = 64 * 1024;
			_reporterThread = ::CreateThread(NULL, ExceptionHandlerThreadStackSize,
				OnExceptionHandlerThread, this, 0, nullptr);
			if (_reporterThread == NULL) {
				return;
			}

			EnableCrashingOnCrashes();

			_prevExceptionFilter = ::SetUnhandledExceptionFilter(CrashHandler);

			::signal(SIGABRT, SignalHandler);
//#if !defined(_Build_By_LTL)
//			// This function is not supported on VC-LTL 4.1.3
//			_set_abort_behavior(0, _WRITE_ABORT_MSG | _CALL_REPORTFAULT);
//#endif

			std::set_terminate(Terminator);
#	if !defined(BACKWARD_ATLEAST_CXX17)
			std::set_unexpected(Terminator);
#	endif
			_set_purecall_handler(Terminator);
#	if _MSC_VER >= 1400
			_set_invalid_parameter_handler(InvalidParameterHandler);
#	endif
		}

		~ExceptionHandling() {
			{
				std::unique_lock<std::mutex> lk(_lock);
				_status = HandlerStatus::NormalExit;
			}

			_cv.notify_one();

			if (_reporterThread != NULL) {
				if (::WaitForSingleObject(_reporterThread, 5000) != WAIT_OBJECT_0) {
					::TerminateThread(_reporterThread, 1);
				}
				::CloseHandle(_reporterThread);
				_reporterThread = NULL;
			}
		}

		bool IsLoaded() const {
			return true;
		}

	private:
		struct AppMemory {
			ULONG64 Ptr;
			ULONG Length;

			bool operator==(const struct AppMemory& other) const {
				return (Ptr == other.Ptr);
			}

			bool operator==(const void* other) const {
				return (Ptr == reinterpret_cast<ULONG64>(other));
			}
		};

		//typedef Containers::SmallVector<AppMemory> AppMemoryList;

		typedef struct {
			const AppMemory* Begin;
			const AppMemory* End;
		} MinidumpCallbackContext;

		enum class MinidumpRawInfoValidity : std::uint32_t {
			None = 0,
			ValidDumpThreadId = 0x01,
			ValidRequestingThreadId = 0x02
		};

		DEATH_PRIVATE_ENUM_FLAGS(MinidumpRawInfoValidity);

		struct MinidumpRawInfo {
			MinidumpRawInfoValidity Validity;
			std::uint32_t DumpThreadId;
			std::uint32_t RequestingThreadId;
		};

		enum class HandlerStatus {
			Running, Crashed, NormalExit, Ending
		};

		static constexpr std::int32_t ExceptionExitCode = 0xDEADBEEF;
		static constexpr std::uint32_t MinidumpRawInfoStream = 0x47670001;

		static constexpr std::int32_t SignalSkipFrames =
#	if defined(DEATH_TARGET_CLANG)
			// With clang, RtlCaptureContext also captures the stack frame of the current function Below that,
			// there are 3 internal Windows functions
			4
#	else
			// With MSVC cl, RtlCaptureContext misses the stack frame of the current function The first entries
			// during StackWalk are the 3 internal Windows functions
			3
#	endif
			;

		static ExceptionHandling*& GetSingleton() {
			static ExceptionHandling* current = nullptr;
			return current;
		}

		HANDLE _reporterThread;
		LPTOP_LEVEL_EXCEPTION_FILTER _prevExceptionFilter;
		ExceptionContext _context;
		std::mutex _lock;
		std::condition_variable _cv;
		HANDLE _crashedThread;
		HandlerStatus _status;
		std::int32_t _skipFrames;

		static DWORD WINAPI OnExceptionHandlerThread(void* lpParameter) {
			// We handle crashes in a utility thread: backward structures and some Windows functions called here
			// need stack space, which we do not have when we encounter a stack overflow.
			// To support reporting stack traces during a stack overflow, we create a utility thread at startup,
			// which waits until a crash happens or the program exits normally.

			auto* _this = static_cast<ExceptionHandling*>(lpParameter);

			{
				std::unique_lock<std::mutex> lk(_this->_lock);
				_this->_cv.wait(lk, [_this]() { return _this->_status != HandlerStatus::Running; });
			}

			if (_this->_status == HandlerStatus::Crashed) {
				// For some reason this must be called first, otherwise the dump is not linked to sources correctly
				auto* current = GetSingleton();
				if ((current->FeatureFlags & Flags::CreateMemoryDump) == Flags::CreateMemoryDump) {
					WriteMinidumpWithException(::GetThreadId(_this->_crashedThread), &_this->_context);
				}
				current->HandleStacktrace();
			}

			{
				std::unique_lock<std::mutex> lk(_this->_lock);
				_this->_status = HandlerStatus::Ending;
			}
			_this->_cv.notify_one();
			return 0;
		}

		static inline void Terminator() {
			GetSingleton()->CrashHandler(SignalSkipFrames);
			::exit(ExceptionExitCode);
		}

		static inline void SignalHandler(int) {
			GetSingleton()->CrashHandler(SignalSkipFrames);
			::exit(ExceptionExitCode);
		}

#	if _MSC_VER >= 1400
		static inline void __cdecl InvalidParameterHandler(const wchar_t* expression, const wchar_t* function, const wchar_t* file, std::uint32_t line, std::uintptr_t reserved) {
			GetSingleton()->CrashHandler(SignalSkipFrames);
			::exit(ExceptionExitCode);
		}
#	endif

		DEATH_NEVER_INLINE static LONG WINAPI CrashHandler(EXCEPTION_POINTERS* info) {
			auto* _this = GetSingleton();
			DWORD code = info->ExceptionRecord->ExceptionCode;

			// Pass-through MSVC exceptions
			if (code == 0xE06D7363 && _this->_prevExceptionFilter != nullptr) {
				return _this->_prevExceptionFilter(info);
			}

			// The exception info supplies a trace from exactly where the issue was, no need to skip records
			bool isDebugException = (code == EXCEPTION_BREAKPOINT || code == EXCEPTION_SINGLE_STEP ||
									 code == DBG_PRINTEXCEPTION_C || code == DBG_PRINTEXCEPTION_WIDE_C);
			if (!isDebugException) {
				_this->CrashHandler(0, info);
			}
			return EXCEPTION_CONTINUE_SEARCH;
		}

		DEATH_NEVER_INLINE void CrashHandler(std::int32_t skip, EXCEPTION_POINTERS* info = nullptr) {
			auto* _this = GetSingleton();
			auto& context = _this->_context;

			{
				std::unique_lock<std::mutex> lk(_this->_lock);
				if (_this->_status >= HandlerStatus::Crashed) {
					// Crash handler was already called, wait until it finishes
					while (_this->_status == HandlerStatus::Crashed) {
						::Sleep(10);
					}
					return;
				}
			}

			if (info == nullptr) {
#	if (defined(_M_IX86) || defined(__i386__)) && defined(DEATH_TARGET_MSVC)
				// RtlCaptureContext() doesn't work on i386
				CONTEXT localCtx;
				std::memset(&localCtx, 0, sizeof(CONTEXT));
				localCtx.ContextFlags = CONTEXT_CONTROL;
				__asm {
				label:
					mov[localCtx.Ebp], ebp;
					mov[localCtx.Esp], esp;
					mov eax, [label];
					mov[localCtx.Eip], eax;
				}
				std::memcpy(&(context.Context), &localCtx, sizeof(CONTEXT));
#	else
				::RtlCaptureContext(&(context.Context));
#	endif
				std::memset(&(context.ExceptionRecord), 0, sizeof(EXCEPTION_RECORD));
			} else {
				std::memcpy(&(context.Context), info->ContextRecord, sizeof(CONTEXT));
				std::memcpy(&(context.ExceptionRecord), info->ExceptionRecord, sizeof(EXCEPTION_RECORD));
			}
			::DuplicateHandle(::GetCurrentProcess(), ::GetCurrentThread(),
								::GetCurrentProcess(), &_this->_crashedThread,
								0, FALSE, DUPLICATE_SAME_ACCESS);

			_this->_skipFrames = skip;

			{
				std::unique_lock<std::mutex> lk(_this->_lock);
				if (_this->_status >= HandlerStatus::Crashed) {
					// Crash handler was already called, wait until it finishes
					while (_this->_status == HandlerStatus::Crashed) {
						::Sleep(10);
					}
					return;
				}
				_this->_status = HandlerStatus::Crashed;
			}

			_this->_cv.notify_one();

			{
				std::unique_lock<std::mutex> lk(_this->_lock);
				_this->_cv.wait(lk, [_this]() { return _this->_status != HandlerStatus::Crashed; });
			}
		}

		void HandleStacktrace() {
			// Printer creates the TraceResolver, which can supply us a machine type for stack walking. Without this,
			// StackTrace can only guess using some macros. StackTrace also requires that the PDBs are already loaded,
			// which is done in the constructor of TraceResolver.

			HANDLE hStdError = ::GetStdHandle(STD_ERROR_HANDLE);
			bool shouldWriteToStdErr = ((FeatureFlags & Flags::UseStdError) == Flags::UseStdError && ::GetFileType(hStdError) != FILE_TYPE_UNKNOWN);

			IO::Stream* dest = Destination;
			bool shouldWriteToDest = (dest != nullptr);

			if (!shouldWriteToStdErr && !shouldWriteToDest) {
				return;
			}

			std::int32_t skipFrames = _skipFrames;

			Printer printer;
			printer.Address = true;

			StackTrace st;
			st.SetMachineType(printer.GetResolver().GetMachineType());
			st.SetThreadHandle(_crashedThread);
			st.LoadHere(32 + skipFrames, &_context);
			st.SetSkipFrames(skipFrames);

			if (shouldWriteToStdErr) {
				printer.FeatureFlags = FeatureFlags;
				printer.Print(st, std::cerr, _context.ExceptionRecord.ExceptionCode);
			}

			if (shouldWriteToDest) {
				printer.FeatureFlags = FeatureFlags & ~Flags::Colorized;
				printer.PrintFilePrologue(dest);
				printer.Print(st, dest, _context.ExceptionRecord.ExceptionCode);
				dest->Flush();
			}
		}

		static void EnableCrashingOnCrashes() {
			// Disable swallowing of exceptions in mainly 32-bit apps, these functions are not present in the newer version of Windows anymore
			using _GetPolicyDelegate = BOOL (WINAPI*)(LPDWORD lpFlags);
			using _SetPolicyDelegate = BOOL (WINAPI*)(DWORD dwFlags);
			constexpr DWORD EXCEPTION_SWALLOWING = 0x1;

			DWORD dwFlags;
			HMODULE kernel32 = ::GetModuleHandle(L"kernel32.dll");
			_GetPolicyDelegate pGetPolicy = (_GetPolicyDelegate)::GetProcAddress(kernel32, "GetProcessUserModeExceptionPolicy");
			_SetPolicyDelegate pSetPolicy = (_SetPolicyDelegate)::GetProcAddress(kernel32, "SetProcessUserModeExceptionPolicy");
			if (pGetPolicy != nullptr && pSetPolicy != nullptr && pGetPolicy(&dwFlags)) {
				pSetPolicy(dwFlags & ~EXCEPTION_SWALLOWING);
			}
		}

		static bool WriteMinidumpWithException(std::size_t requestingThreadId, ExceptionContext* ctx) {
			wchar_t processPath[MAX_PATH];
			if (!::GetModuleFileNameW(NULL, processPath, DWORD(Containers::arraySize(processPath)))) {
				return false;
			}

			bool success = false;
			std::size_t processPathLength = wcslen(processPath) - 1;
			bool dotFound = false;
			while (processPathLength >= 0) {
				wchar_t c = processPath[processPathLength];
				if (c == '/' || c == '\\') {
					processPath[processPathLength] = L'\0';
					processPathLength++;
					break;
				}
				if (c == '.' && !dotFound) {
					dotFound = true;
					processPath[processPathLength] = L'\0';
				}
				processPathLength--;
			}

			SYSTEMTIME lt;
			::GetLocalTime(&lt);

			wchar_t minidumpPath[MAX_PATH];
			std::int32_t pathPrefixLength = swprintf_s(minidumpPath, L"%s\\CrashDumps\\", processPathLength > 0 ? processPath : L".");
			::CreateDirectory(minidumpPath, NULL);
			TryEnableFileCompression(minidumpPath);
			swprintf_s(minidumpPath + pathPrefixLength, Containers::arraySize(minidumpPath) - pathPrefixLength,
				L"%s (%02i-%02i-%02i-%02i-%02i-%02i).dmp", &processPath[processPathLength], lt.wYear % 100,
				lt.wMonth, lt.wDay, lt.wHour, lt.wMinute, lt.wSecond);

			HANDLE dumpFile = ::CreateFile(minidumpPath, GENERIC_WRITE, 0, NULL, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, NULL);
			if (dumpFile != INVALID_HANDLE_VALUE) {
				HANDLE process = ::GetCurrentProcess();

				EXCEPTION_POINTERS exceptionPointers;
				exceptionPointers.ContextRecord = &ctx->Context;
				exceptionPointers.ExceptionRecord = &ctx->ExceptionRecord;

				MINIDUMP_EXCEPTION_INFORMATION exceptInfo;
				exceptInfo.ThreadId = (DWORD)requestingThreadId;
				exceptInfo.ExceptionPointers = &exceptionPointers;
				exceptInfo.ClientPointers = FALSE;

				MINIDUMP_USER_STREAM userStreamArray[2];
				MINIDUMP_USER_STREAM_INFORMATION userStreams;
				userStreams.UserStreamCount = 0;
				userStreams.UserStreamArray = userStreamArray;

				// Add an MDRawBreakpadInfo stream to the minidump, to provide additional information about the exception handler.
				// The information will help to determine which threads are relevant.
				MinidumpRawInfo rawInfo;
				rawInfo.Validity = MinidumpRawInfoValidity::ValidDumpThreadId | MinidumpRawInfoValidity::ValidRequestingThreadId;
				rawInfo.DumpThreadId = ::GetCurrentThreadId();
				rawInfo.RequestingThreadId = (DWORD)requestingThreadId;

				std::int32_t index = userStreams.UserStreamCount;
				userStreamArray[index].Type = MinidumpRawInfoStream;
				userStreamArray[index].BufferSize = sizeof(rawInfo);
				userStreamArray[index].Buffer = &rawInfo;
				userStreams.UserStreamCount++;

				/*if (assertion != nullptr) {
					std::int32_t index = userStreams.UserStreamCount;
					userStreamArray[index].Type = MinidumpAssertionInfoStream;
					userStreamArray[index].BufferSize = sizeof(MinidumpRawAssertionInfo);
					userStreamArray[index].Buffer = assertion;
					userStreams.UserStreamCount++;
				}*/

				// Older versions of DbgHelp.dll don't correctly put the memory around the faulting instruction pointer into the minidump.
				// This callback will ensure that it gets included.
				AppMemory exceptionThreadMemory = {};
				if (ctx != nullptr) {
					// Find a memory region of 256 bytes centered on the faulting instruction pointer
					const ULONG64 instructionPointer =
#if defined(_M_X64) || defined(__x86_64__)
						ctx->Context.Rip;
#elif defined(_M_ARM) || (_M_ARM64) || defined(_M_ARM64EC)
						ctx->Context.Pc;
#elif defined(_M_IA64)
						ctx->Context.StIIP;
#else
						ctx->Context.Eip;
#endif

					MEMORY_BASIC_INFORMATION info;
					if (::VirtualQueryEx(process, reinterpret_cast<LPCVOID>(instructionPointer), &info, sizeof(MEMORY_BASIC_INFORMATION)) != 0 && info.State == MEM_COMMIT) {
						// Attempt to get 128 bytes before and after the instruction pointer, but settle for whatever's available up to the boundaries of the memory region
						constexpr ULONG64 IPMemorySize = 256;
						ULONG64 base = std::max(reinterpret_cast<ULONG64>(info.BaseAddress), instructionPointer - (IPMemorySize / 2));
						ULONG64 endOfRange = std::min(instructionPointer + (IPMemorySize / 2), reinterpret_cast<ULONG64>(info.BaseAddress) + info.RegionSize);
						ULONG size = static_cast<ULONG>(endOfRange - base);

						exceptionThreadMemory.Ptr = base;
						exceptionThreadMemory.Length = size;
					}
				}

				MinidumpCallbackContext context;
				context.Begin = &exceptionThreadMemory;
				context.End = (context.Begin + 1);	// Only one item for now

				// Skip the reserved element if there was no instruction memory
				if (context.Begin->Ptr == NULL) {
					context.Begin++;
				}

				MINIDUMP_CALLBACK_INFORMATION callback;
				callback.CallbackRoutine = MinidumpWriteDumpCallback;
				callback.CallbackParam = reinterpret_cast<void*>(&context);

				constexpr MINIDUMP_TYPE MinidumpType = (MINIDUMP_TYPE)(MiniDumpScanMemory | MiniDumpWithHandleData | MiniDumpWithThreadInfo | MiniDumpWithIndirectlyReferencedMemory);
				success = ::MiniDumpWriteDump(process, ::GetProcessId(process), dumpFile, MinidumpType,
					ctx != nullptr ? &exceptInfo : NULL, &userStreams, &callback);

				::CloseHandle(dumpFile);
			}

			return success;
		}

		static BOOL CALLBACK MinidumpWriteDumpCallback(PVOID context, const PMINIDUMP_CALLBACK_INPUT callbackInput, PMINIDUMP_CALLBACK_OUTPUT callbackOutput) {
			switch (callbackInput->CallbackType) {
				case MemoryCallback: {
					MinidumpCallbackContext* callbackContext = reinterpret_cast<MinidumpCallbackContext*>(context);
					if (callbackContext->Begin == callbackContext->End) {
						return FALSE;
					}

					// Include the specified memory region
					callbackOutput->MemoryBase = callbackContext->Begin->Ptr;
					callbackOutput->MemorySize = callbackContext->Begin->Length;
					callbackContext->Begin++;
					return TRUE;
				}

				// Include all modules
				case IncludeModuleCallback:
				case ModuleCallback:
					return TRUE;

				// Include all threads
				case IncludeThreadCallback:
				case ThreadCallback:
				case ThreadExCallback:
					return TRUE;

				// Stop receiving cancel callbacks
				case CancelCallback:
					callbackOutput->CheckCancel = FALSE;
					callbackOutput->Cancel = FALSE;
					return TRUE;

				default:
					return FALSE;
			}
		}

		static bool TryEnableFileCompression(const wchar_t* path) {
			HANDLE hFile = ::CreateFile(path, GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
							NULL, OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS, NULL);
			if (hFile == INVALID_HANDLE_VALUE) {
				return false;
			}

			DWORD dwBytesReturned;
			USHORT compressionState = COMPRESSION_FORMAT_DEFAULT;
			BOOL success = ::DeviceIoControl(hFile, FSCTL_SET_COMPRESSION, &compressionState, sizeof(compressionState),
								NULL, 0, &dwBytesReturned, NULL);
			::CloseHandle(hFile);
			return success;
		}
	};

#endif // BACKWARD_TARGET_WINDOWS

}} // namespace Death::Backward

#endif