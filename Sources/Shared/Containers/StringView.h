// Copyright © 2007, 2008, 2009, 2010, 2011, 2012, 2013, 2014, 2015, 2016,
//             2017, 2018, 2019, 2020, 2021, 2022, 2023, 2024
//           Vladimír Vondruš <mosra@centrum.cz> and contributors
// Copyright © 2020-2024 Dan R.
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

#pragma once

#include "Containers.h"
#include "../Asserts.h"
#include "../Base/Move.h"

#include <initializer_list>
#include <type_traits>

namespace Death { namespace Containers {
//###==##====#=====--==~--~=~- --- -- -  -  -   -

	namespace Implementation
	{
		template<class, class> struct StringViewConverter;
	}

	/** @brief String view flags */
	enum class StringViewFlags : std::size_t
	{
		/**
		 * The referenced string is global, i.e., with an unlimited lifetime. A
		 * string view with this flag set doesn't need to have a copy allocated in
		 * order to ensure it stays in scope.
		 */
		Global = std::size_t{1} << (sizeof(std::size_t) * 8 - 1),

		/**
		 * The referenced string is null-terminated. A string view with this flag
		 * set doesn't need to have a null-terminated copy allocated in order to
		 * pass to an API that expects only null-terminated strings.
		 * @see @ref Containers-BasicStringView-usage-c-string-conversion
		 */
		 NullTerminated = std::size_t{1} << (sizeof(std::size_t) * 8 - 2)
	};

	DEATH_ENUM_FLAGS(StringViewFlags);

	namespace Implementation
	{
		enum : std::size_t {
			StringViewSizeMask = std::size_t(StringViewFlags::NullTerminated) | std::size_t(StringViewFlags::Global)
		};
	}

	/**
		@brief Base for string views

		@m_keywords{StringView MutableStringView}

		A lighter alternative to C++17 @ref std::string_view that has also a mutable
		variant and additional optimizations for reducing unnecessary copies and
		allocations. An owning version of this container is a @ref String.

		@section Containers-BasicStringView-usage Usage

		The class is meant to be used through either the @ref StringView or
		@ref MutableStringView typedefs. It's implicitly convertible from C string
		literals, but the recommended way is using the @link Literals::operator""_s() @endlink
		literal.

		While both expressions are *mostly* equivalent, the literal is
		@cpp constexpr @ce so you can use it in a compile-time context (and on the
		other hand, the implicit conversion uses @ref std::strlen() which has some
		runtime impact). The main difference is however that the literal will annotate
		the view as @ref StringViewFlags::Global "global" and
		@ref StringViewFlags::NullTerminated "null-terminated", which can help avoid
		copies and allocations when lifetime of the data needs to be extended or when
		dealing with APIs that expect null-terminated strings. Additionally, the
		literal will also preserve zero bytes inside the string, while implicit
		conversion from a C string won't.

		C string literals are implicitly immutable, in order to create a mutable one
		you need to assign the literal to a @cpp char[] @ce (instead of
		@cpp const char* @ce) and then create a @ref MutableStringView in a second
		step.

		This class is implicitly convertible from and to @ref ArrayView, however note
		that the conversion will not preserve the global / null-terminated annotations.

		@attention In order to allow the above-mentioned optimizations, on 32-bit
			systems the size is limited to 1 GB. That should be more than enough for
			real-world strings (as opposed to arbitrary binary data), if you need more
			please use an @ref ArrayView instead.

		@subsection Containers-BasicStringView-usage-slicing String view slicing

		The string view class inherits the slicing APIs of @ref ArrayView ---
		@ref slice(), @ref sliceSize(), @ref prefix(), @ref suffix(),
		@ref exceptPrefix() and @ref exceptSuffix() --- and in addition it provides
		string-specific utilities. These are are all derived from the slicing APIs,
		which means they also return sub-views of the original string:

		<ul>
		<li>@ref split() and @ref splitWithoutEmptyParts() split the view on given set
		of delimiter characters</li>
		<li>@ref join() and @ref joinWithoutEmptyParts() is an inverse of the
		above</li>
		<li>@ref partition() is similar to @ref split(), but always returning three
		elements with a clearly defined behavior, which can make certain code more
		robust while reducing the amount of possible error states</li>
		<li>@ref trimmed() (and its variants @ref trimmedPrefix() /
		@ref trimmedSuffix()), commonly used to remove leading and trailing
		whitespace</li>
		<li>@ref exceptPrefix(StringView) const / @ref exceptSuffix(StringView) const
		checks that a view starts (or ends) with given string and then removes it.</li>
		</ul>

		@subsection Containers-BasicStringView-usage-c-string-conversion Converting StringView instances to null-terminated C strings

		If possible when interacting with 3rd party APIs, passing a string together
		with the size information is always preferable to passing just a plain
		@cpp const char* @ce. Apart from saving an unnecessary @ref std::strlen() call
		it can avoid unbounded memory reads in security-critical scenarios.

		Unlike a @ref String, string views can point to any slice of a larger string
		and thus can't guarantee null termination. Because of this and because even a
		view with @ref StringViewFlags::NullTerminated can still contain a @cpp '\0' @ce
		anywhere in the middle, there's no implicit conversion to @cpp const char* @ce
		provided, and the pointer returned by @ref data() should only be used together
		with @ref size().

		The quickest safe way to get a null-terminated string out of a @ref StringView
		is to convert the view to a @ref String and then use @ref String::data().
		However, such operation will unconditionally make a copy of the string, which
		is unnecessary work if the view was null-terminated already. To avoid that,
		there's @ref String::nullTerminatedView(), which will make a copy only if the
		view is not already null-terminated, directly referencing the view with a no-op
		deleter otherwise.

		Similarly as described in @ref Containers-String-usage-c-string-conversion,
		pointers to data in SSO instances will get invalidated when the instance is
		moved. With @ref String::nullTerminatedGlobalView(AllocatedInitT, StringView)
		the null-terminated copy will be always allocated.

		@section Containers-BasicStringView-array-views Conversion to array views

		String views are implicitly convertible to @ref ArrayView as described in the
		following table. This also extends to other container types constructibe from
		@ref ArrayView.

		String view type                | ↭ | Array view type
		------------------------------- | - | ---------------------
		@ref StringView                 | → | @ref ArrayView "ArrayView<const char>"
		@ref MutableStringView          | → | @ref ArrayView "ArrayView<const char>"
		@ref MutableStringView          | → | @ref ArrayView "ArrayView<char>"

		@section Containers-BasicStringView-stl STL compatibility

		Instances of @ref StringView and @ref BasicStringView are *implicitly*
		convertible from and to @ref std::string if you include
		@ref Containers/StringStl.h. The conversion is provided in a separate
		header to avoid unconditional @cpp #include <string> @ce, which significantly
		affects compile times.

		Creating a @ref std::string instance always involves a data copy,
		while going the other way always creates a non-owning reference without
		allocations or copies. @ref StringView / @ref MutableStringView created from a
		@ref std::string always have @ref StringViewFlags::NullTerminated set, but the
		usual conditions regarding views apply --- if the original string is modified,
		view pointer, size or the null termination property may not be valid anymore.

		On compilers that support C++17 and @ref std::string_view, implicit conversion
		from and to it is provided in @ref Containers/StringStlView.h. For
		similar reasons, it's a dedicated header to avoid unconditional
		@cpp #include <string_view> @ce, but this one is even significantly heavier
		than the @ref string "<string>" include on certain implementations, so it's
		separate from a @ref std::string as well.

		The @ref std::string_view type doesn't have any mutable counterpart, so there's
		no possibility to create a @ref MutableStringView out of it. Because
		@ref std::string_view doesn't preserve any information about the string origin,
		neither @ref StringViewFlags::NullTerminated nor @ref StringViewFlags::Global is
		set in a @ref StringView converted from it.
	*/
	template<class T> class BasicStringView
	{
	public:
		/**
		 * @brief Default constructor
		 *
		 * A default-constructed instance has @ref StringViewFlags::Global set.
		 */
#ifdef DOXYGEN_GENERATING_OUTPUT
		constexpr /*implicit*/ BasicStringView(std::nullptr_t = nullptr) noexcept;
#else
		/* To avoid ambiguity in certain cases of passing 0 to overloads that take either a StringView or std::size_t */
		template<class U, typename std::enable_if<std::is_same<std::nullptr_t, U>::value, int>::type = 0> constexpr /*implicit*/ BasicStringView(U) noexcept : _data{}, _sizePlusFlags{std::size_t(StringViewFlags::Global)} {}

		constexpr /*implicit*/ BasicStringView() noexcept : _data{}, _sizePlusFlags{std::size_t(StringViewFlags::Global)} {}
#endif

		/**
		 * @brief Construct from a C string of known size
		 * @param data      C string
		 * @param size      Size of the C string, excluding the null terminator
		 * @param flags     Flags describing additional string properties
		 *
		 * If @ref StringViewFlags::Global is set, the data pointer is assumed
		 * to never go out of scope, which can avoid copies and allocations in
		 * code using the instance. If @ref StringViewFlags::NullTerminated is
		 * set, it's expected that `data` is not @cpp nullptr @ce and
		 * @cpp data[size] == '\0' @ce. That can avoid copies and allocations
		 * in code that passes such string to APIs that expect null-terminated
		 * strings (such as @ref std::fopen()).
		 *
		 * If you're unsure about data origin, the safe bet is to keep flags at
		 * their default. On the other hand, C string literals are always
		 * global and null-terminated --- for those, the recommended way is to
		 * use the @link operator""_s() @endlink literal instead.
		 */
		constexpr /*implicit*/ BasicStringView(T* data, std::size_t size, StringViewFlags flags = {}) noexcept : _data{data}, _sizePlusFlags{
			// This ends up being called from BasicStringView(T*, Flags), so basically on every implicit conversion
			// from a C string, thus the release build perf aspect wins over safety
			(size | (std::size_t(flags) & Implementation::StringViewSizeMask))} {}

		/**
		 * @brief Construct from a @ref String
		 *
		 * The resulting view has @ref StringViewFlags::NullTerminated set
		 * always, and @ref StringViewFlags::Global if the string was originally
		 * created from a global null-terminated view with
		 * @ref String::nullTerminatedView() or @ref String::nullTerminatedGlobalView().
		 */
		/*implicit*/ BasicStringView(String& data) noexcept;

		/**
		 * @brief Construct from a const @ref String
		 *
		 * Enabled only if the view is not mutable. The resulting view has
		 * @ref StringViewFlags::NullTerminated set always, and
		 * @ref StringViewFlags::Global if the string was created from a global
		 * null-terminated view with @ref String::nullTerminatedView() or
		 * @ref String::nullTerminatedGlobalView().
		 */
		template<class U = T
#ifndef DOXYGEN_GENERATING_OUTPUT
			/* typename std::enable_if<std::is_const<U>::value, int>::type = 0
			   cannot be used because GCC and Clang then have different mangling
			   for the deinlined specialization in StringView.cpp, which means
			   the library built with GCC cannot be used with Clang and vice versa. */
			, class = typename std::enable_if<std::is_const<U>::value>::type
#endif
		> /*implicit*/ BasicStringView(const String& data) noexcept;

		/**
		 * @brief Construct from an @ref ArrayView
		 *
		 * The resulting view has the same size as @p data, by default no
		 * null-termination is assumed.
		 */
#ifdef DOXYGEN_GENERATING_OUTPUT
		/*implicit*/ BasicStringView(ArrayView<T> data, StringViewFlags flags = {}) noexcept;
#else
		/* This has to accept any type and then delegate to a private constructor instead
		   of directly taking ArrayView<T>, due to how overload resolution works in copy
		   initialization as opposed to a direct constructor/function call. If it would take
		   ArrayView<T> directly, `Array<char> -> ArrayView<const char> -> StringView`
		   wouldn't work because it's one custom conversion sequence more than allowed
		   in a copy initialization, and to make that work, this class would have to
		   replicate all ArrayView constructors including conversion from Array etc.,
		   which isn't feasible.
		   It's also explicitly disallowing T[] arguments (which are implicitly convertible
		   to an ArrayView), because those should be picking the T* overload and rely on strlen(),
		   consistently with how C string literals work; and disallowing construction from
		   a StringView because it'd get preferred over the implicit copy constructor. */
		template<class U, class = typename std::enable_if<!std::is_array<typename std::remove_reference<U&&>::type>::value && !std::is_same<typename std::decay<U&&>::type, BasicStringView<T>>::value && !std::is_same<typename std::decay<U&&>::type, std::nullptr_t>::value, decltype(ArrayView<T>{std::declval<U&&>()})>::type> constexpr /*implicit*/ BasicStringView(U&& data, StringViewFlags flags = {}) noexcept : BasicStringView{flags, ArrayView<T>(data)} {}
#endif

		/** @brief Construct a @ref StringView from a @ref MutableStringView */
		template<class U
#ifndef DOXYGEN_GENERATING_OUTPUT
			, typename std::enable_if<std::is_same<const U, T>::value, int>::type = 0
#endif
		> constexpr /*implicit*/ BasicStringView(BasicStringView<U> mutable_) noexcept : _data{mutable_._data}, _sizePlusFlags{mutable_._sizePlusFlags} {}

		/**
		 * @brief Construct from a null-terminated C string
		 *
		 * Contrary to the behavior of @ref std::string, @p data is allowed to
		 * be @cpp nullptr @ce --- in that case an empty view is constructed.
		 *
		 * Calls @ref BasicStringView(T*, std::size_t, StringViewFlags) with
		 * @p size set to @ref std::strlen() of @p data if @p data is not
		 * @cpp nullptr @ce. If @p data is @cpp nullptr @ce, @p size is set to
		 * @cpp 0 @ce. In addition to @p extraFlags, if @p data is not
		 * @cpp nullptr @ce, @ref StringViewFlags::NullTerminated is set,
		 * otherwise @ref StringViewFlags::Global is set.
		 *
		 * The @ref BasicStringView(std::nullptr_t) overload (which is a
		 * default constructor) is additionally @cpp constexpr @ce.
		 */
#ifdef DOXYGEN_GENERATING_OUTPUT
		/*implicit*/ BasicStringView(T* data, StringViewFlags extraFlags = {}) noexcept;
#else
		template<class U, typename std::enable_if<std::is_pointer<U>::value && std::is_convertible<const U&, T*>::value, int>::type = 0> /*implicit*/ BasicStringView(U data, StringViewFlags extraFlags = {}) noexcept : BasicStringView{data, extraFlags, nullptr} {}
#endif

		/** @brief Construct a view on an external type / from an external representation */
		/* There's no restriction that would disallow creating StringView from
		   e.g. std::string<T>&& because that would break uses like `consume(foo());`,
		   where `consume()` expects a view but `foo()` returns a std::vector. Besides
		   that, to simplify the implementation, there's no const-adding conversion.
		   Instead, the implementer is supposed to add an ArrayViewConverter variant for that. */
		template<class U, class = decltype(Implementation::StringViewConverter<T, typename std::decay<U&&>::type>::from(std::declval<U&&>()))> constexpr /*implicit*/ BasicStringView(U&& other) noexcept : BasicStringView{Implementation::StringViewConverter<T, typename std::decay<U&&>::type>::from(Death::forward<U>(other))} {}

		/** @brief Convert the view to external representation */
		/* To simplify the implementation, there's no const-adding conversion. Instead, the implementer is supposed to add an StringViewConverter variant for that. */
		template<class U, class = decltype(Implementation::StringViewConverter<T, U>::to(std::declval<BasicStringView<T>>()))> constexpr /*implicit*/ operator U() const {
			return Implementation::StringViewConverter<T, U>::to(*this);
		}

		/**
		 * @brief Whether the string is non-empty and non-null
		 *
		 * Returns @cpp true @ce if the string is non-empty *and* the pointer
		 * is not @cpp nullptr @ce, @cpp false @ce otherwise. If you rely on
		 * just one of these conditions, use @ref empty() and @ref data()
		 * instead.
		 */
		constexpr explicit operator bool() const {
			return _data && (_sizePlusFlags & ~Implementation::StringViewSizeMask);
		}

		/** @brief Flags */
		constexpr StringViewFlags flags() const {
			return StringViewFlags(_sizePlusFlags & Implementation::StringViewSizeMask);
		}

		/**
		 * @brief String data
		 *
		 * The pointer is not guaranteed to be null-terminated, use
		 * @ref flags() and @ref StringViewFlags::NullTerminated to check for
		 * the presence of a null terminator.
		 */
		constexpr T* data() const { return _data; }

		/**
		 * @brief String size
		 *
		 * Excludes the null terminator.
		 */
		constexpr std::size_t size() const {
			return (_sizePlusFlags & ~Implementation::StringViewSizeMask);
		}

		/**
		 * @brief Whether the string is empty
		 */
		constexpr bool empty() const {
			return !(_sizePlusFlags & ~Implementation::StringViewSizeMask);
		}

		/**
		 * @brief Pointer to the first byte
		 */
		constexpr T* begin() const { return _data; }
		/** @overload */
		constexpr T* cbegin() const { return _data; }

		/**
		 * @brief Pointer to (one item after) the last byte
		 */
		constexpr T* end() const { return _data + (_sizePlusFlags & ~Implementation::StringViewSizeMask); }
		/** @overload */
		constexpr T* cend() const { return _data + (_sizePlusFlags & ~Implementation::StringViewSizeMask); }

		/**
		 * @brief First byte
		 *
		 * Expects there is at least one byte.
		 */
		constexpr T& front() const;

		/**
		 * @brief Last byte
		 *
		 * Expects there is at least one byte.
		 */
		constexpr T& back() const;

		/** @brief Element access */
		constexpr T& operator[](std::size_t i) const;

		/**
		 * @brief View slice
		 *
		 * Both arguments are expected to be in range. Propagates the
		 * @ref StringViewFlags::Global flag and if @p end points to (one item
		 * after) the end of the original null-terminated string, the result
		 * has @ref StringViewFlags::NullTerminated also.
		 */
		constexpr BasicStringView<T> slice(T* begin, T* end) const;

		/** @overload */
		constexpr BasicStringView<T> slice(std::size_t begin, std::size_t end) const;

		/**
		 * @brief View slice of given size
		 *
		 * Equivalent to @cpp data.slice(begin, begin + size) @ce.
		 */
#ifdef DOXYGEN_GENERATING_OUTPUT
		constexpr BasicStringView<T> sliceSize(T* begin, std::size_t size) const;
#else
		template<class U, typename std::enable_if<std::is_convertible<U, T*>::value && !std::is_convertible<U, std::size_t>::value, int>::type = 0> constexpr BasicStringView<T> sliceSize(U begin, std::size_t size) const {
			return slice(begin, begin + size);
		}
#endif

		/** @overload */
		constexpr BasicStringView<T> sliceSize(std::size_t begin, std::size_t size) const {
			return slice(begin, begin + size);
		}

		/**
		 * @brief View prefix until a pointer
		 *
		 * Equivalent to @cpp string.slice(string.begin(), end) @ce. If @p end
		 * is @cpp nullptr @ce, returns zero-sized @cpp nullptr @ce view.
		 */
#ifdef DOXYGEN_GENERATING_OUTPUT
		constexpr BasicStringView<T> prefix(T* end) const;
#else
		template<class U, typename std::enable_if<std::is_convertible<U, T*>::value && !std::is_convertible<U, std::size_t>::value, int>::type = 0> constexpr BasicStringView<T> prefix(U end) const {
			return static_cast<T*>(end) ? slice(_data, end) : BasicStringView<T>{};
		}
#endif

		/**
		 * @brief View suffix after a pointer
		 *
		 * Equivalent to @cpp string.slice(begin, string.end()) @ce. If
		 * @p begin is @cpp nullptr @ce and the original view isn't, returns a
		 * zero-sized @cpp nullptr @ce view.
		 */
		constexpr BasicStringView<T> suffix(T* begin) const {
			return _data && !begin ? BasicStringView<T>{} : slice(begin, _data + (_sizePlusFlags & ~Implementation::StringViewSizeMask));
		}

		/**
		 * @brief View on the first @p size bytes
		 *
		 * Equivalent to @cpp string.slice(0, size) @ce.
		 */
		constexpr BasicStringView<T> prefix(std::size_t size) const {
			return slice(0, size);
		}

		// Here will be suffix(std::size_t size), view on the last size bytes, once the deprecated suffix(std::size_t begin)
		// is gone and enough time passes to not cause silent breakages in existing code.

		/**
		* @brief View except the first @p size bytes
		*
		* Equivalent to @cpp string.slice(size, string.size()) @ce.
		*/
		constexpr BasicStringView<T> exceptPrefix(std::size_t size) const {
			return slice(size, _sizePlusFlags & ~Implementation::StringViewSizeMask);
		}

		/**
		 * @brief View except the last @p size bytes
		 *
		 * Equivalent to @cpp string.slice(0, string.size() - size) @ce.
		 */
		constexpr BasicStringView<T> exceptSuffix(std::size_t size) const {
			return slice(0, (_sizePlusFlags & ~Implementation::StringViewSizeMask) - size);
		}

		/**
		 * @brief Split on given character
		 *
		 * If @p delimiter is not found, returns a single-item array containing
		 * the full input string. If the string is empty, returns an empty
		 * array. The function uses @ref slice() internally, meaning it
		 * propagates the @ref flags() as appropriate.
		 */
		Array<BasicStringView<T>> split(char delimiter) const;

		/**
		 * @brief Split on given substring
		 *
		 * If @p delimiter is not found, returns a single-item array containing
		 * the full input string. If the string is empty, returns an empty
		 * array. The function uses @ref slice() internally, meaning it
		 * propagates the @ref flags() as appropriate.
		 *
		 * Note that this function looks for the whole delimiter. If you want
		 * to split on any character from a set, use
		 * @ref splitOnAnyWithoutEmptyParts() instead.
		 */
		Array<BasicStringView<T>> split(StringView delimiter) const;

		/**
		 * @brief Split on given character, removing empty parts
		 *
		 * If @p delimiter is not found, returns a single-item array containing
		 * the full input string. If the string is empty or consists just of
		 * @p delimiter characters, returns an empty array. The function uses
		 * @ref slice() internally, meaning it propagates the @ref flags() as
		 * appropriate.
		 *
		 * If you have just a single delimiter character,
		 * @ref split(char) const is more efficient. If you need to split on a
		 * multi-character delimiter, use @ref split(StringView) const instead.
		 */
		Array<BasicStringView<T>> splitWithoutEmptyParts(char delimiter) const;

		/**
		 * @brief Split on any character from given set, removing empty parts
		 *
		 * If no characters from @p delimiters are found, returns a single-item
		 * array containing the full input string. If the string is empty or
		 * consists just of characters from @p delimiters, returns an empty
		 * array. The function uses @ref slice() internally, meaning it
		 * propagates the @ref flags() as appropriate.
		 */
		Array<BasicStringView<T>> splitOnAnyWithoutEmptyParts(StringView delimiters) const;

		/**
		 * @brief Split on whitespace, removing empty parts
		 *
		 * Equivalent to calling @ref splitOnAnyWithoutEmptyParts(StringView) const
		 * with @cpp " \t\f\v\r\n" @ce passed to @p delimiters.
		 */
		Array<BasicStringView<T>> splitOnWhitespaceWithoutEmptyParts() const;

		/**
		 * @brief Partition on a character
		 *
		 * Equivalent to Python's @m_class{m-doc-external} [str.partition()](https://docs.python.org/3/library/stdtypes.html#str.partition).
		 * Splits @p string at the first occurrence of @p separator. First
		 * returned value is the part before the separator, second the
		 * separator, third a part after the separator. If the separator is not
		 * found, returns the input string followed by two empty strings.
		 *
		 * The function uses @ref slice() internally, meaning it propagates the
		 * @ref flags() as appropriate. Additionally, the resulting views are
		 * @cpp nullptr @ce only if the input is @cpp nullptr @ce, otherwise
		 * the view always points to existing memory.
		 */
		StaticArray<3, BasicStringView<T>> partition(char separator) const;

		/**
		 * @brief Partition on a substring
		 *
		 * Like @ref partition(char) const, but looks for a whole substring
		 * instead of a single character.
		 */
		StaticArray<3, BasicStringView<T>> partition(StringView separator) const;

		/**
		 * @brief Join strings with this view as the delimiter
		 *
		 * Similar in usage to Python's @m_class{m-doc-external} [str.join()](https://docs.python.org/3/library/stdtypes.html#str.join)
		 */
		String join(ArrayView<const StringView> strings) const;

		/** @overload */
		String join(std::initializer_list<StringView> strings) const;

		/**
		 * @brief Join strings with this view as the delimiter, skipping empty parts
		 *
		 * Like @ref join(), but empty views in @p strings are skipped instead
		 * of causing multiple repeated delimiters in the output.
		 */
		String joinWithoutEmptyParts(ArrayView<const StringView> strings) const;

		/** @overload */
		String joinWithoutEmptyParts(std::initializer_list<StringView> strings) const;

		/**
		 * @brief Whether the string begins with given prefix
		 *
		 * For an empty string returns @cpp true @ce only if @p prefix is empty
		 * as well.
		 */
		bool hasPrefix(StringView prefix) const;
		/** @overload */
		bool hasPrefix(char prefix) const;

		/**
		 * @brief Whether the string ends with given suffix
		 *
		 * For an empty string returns @cpp true @ce only if @p suffix is empty
		 * as well.
		 */
		bool hasSuffix(StringView suffix) const;
		/** @overload */
		bool hasSuffix(char suffix) const;

		/**
		 * @brief View with given prefix stripped
		 *
		 * Expects that the string actually begins with given prefix. The
		 * function uses @ref slice() internally, meaning it propagates the
		 * @ref flags() as appropriate. Additionally, the resulting view is
		 * @cpp nullptr @ce only if the input is @cpp nullptr @ce, otherwise
		 * the view always points to existing memory.
		 */
		BasicStringView<T> exceptPrefix(StringView prefix) const;

		/**
		 * @brief Using char literals for prefix stripping is not allowed
		 *
		 * To avoid accidentally interpreting a @cpp char @ce literal as a size
		 * and calling @ref exceptPrefix(std::size_t) const instead, or vice
		 * versa, you have to always use a string literal to call this
		 * function.
		 */
#ifdef DOXYGEN_GENERATING_OUTPUT
		BasicStringView<T> exceptPrefix(char prefix) const = delete;
#else
		template<typename std::enable_if<std::is_same<typename std::decay<T>::type, char>::value, int>::type = 0> BasicStringView<T> exceptPrefix(T&& prefix) const = delete;
#endif

		/**
		 * @brief View with given suffix stripped
		 *
		 * Expects that the string actually ends with given suffix. The
		 * function uses @ref slice() internally, meaning it propagates the
		 * @ref flags() as appropriate. Additionally, the resulting view is
		 * @cpp nullptr @ce only if the input is @cpp nullptr @ce, otherwise
		 * the view always points to existing memory.
		 */
		BasicStringView<T> exceptSuffix(StringView suffix) const;

		/**
		 * @brief Using char literals for suffix stripping is not allowed
		 *
		 * To avoid accidentally interpreting a @cpp char @ce literal as a size
		 * and calling @ref exceptSuffix(std::size_t) const instead, or vice
		 * versa, you have to always use a string literal to call this
		 * function.
		 */
#ifdef DOXYGEN_GENERATING_OUTPUT
		BasicStringView<T> exceptSuffix(char suffix) const = delete;
#else
		template<typename std::enable_if<std::is_same<typename std::decay<T>::type, char>::value, int>::type = 0> BasicStringView<T> exceptSuffix(T&& suffix) const = delete;
#endif

		/**
		 * @brief View with given characters trimmed from prefix and suffix
		 *
		 * The function uses @ref slice() internally, meaning it propagates the
		 * @ref flags() as appropriate. Additionally, the resulting view is
		 * @cpp nullptr @ce only if the input is @cpp nullptr @ce, otherwise
		 * the view always points to existing memory.
		 */
		BasicStringView<T> trimmed(StringView characters) const {
			return trimmedPrefix(characters).trimmedSuffix(characters);
		}

		/**
		 * @brief View with whitespace trimmed from prefix and suffix
		 *
		 * Equivalent to calling @ref trimmed(StringView) const with
		 * @cpp " \t\f\v\r\n" @ce passed to @p characters.
		 */
		BasicStringView<T> trimmed() const;

		/**
		 * @brief View with given characters trimmed from prefix
		 *
		 * The function uses @ref slice() internally, meaning it propagates the
		 * @ref flags() as appropriate. Additionally, the resulting view is
		 * @cpp nullptr @ce only if the input is @cpp nullptr @ce, otherwise
		 * the view always points to existing memory.
		 */
		BasicStringView<T> trimmedPrefix(StringView characters) const;

		/**
		 * @brief View with whitespace trimmed from prefix
		 *
		 * Equivalent to calling @ref trimmedPrefix(StringView) const with
		 * @cpp " \t\f\v\r\n" @ce passed to @p characters.
		 */
		BasicStringView<T> trimmedPrefix() const;

		/**
		 * @brief View with given characters trimmed from suffix
		 *
		 * The function uses @ref slice() internally, meaning it propagates the
		 * @ref flags() as appropriate. Additionally, the resulting view is
		 * @cpp nullptr @ce only if the input is @cpp nullptr @ce, otherwise
		 * the view always points to existing memory.
		 */
		BasicStringView<T> trimmedSuffix(StringView characters) const;

		/**
		 * @brief View with whitespace trimmed from suffix
		 *
		 * Equivalent to calling @ref trimmedSuffix(StringView) const with
		 * @cpp " \t\f\v\r\n" @ce passed to @p characters.
		 */
		BasicStringView<T> trimmedSuffix() const;

		/**
		 * @brief Find a substring
		 *
		 * Returns a view pointing to the first found substring. If not found,
		 * an empty @cpp nullptr @ce view is returned. The function uses
		 * @ref slice() internally, meaning it propagates the @ref flags() as
		 * appropriate, except in case of a failure, where it always returns no
		 * @ref StringViewFlags.
		 *
		 * Note that the function operates with a @f$ \mathcal{O}(nm) @f$
		 * complexity and as such is meant mainly for one-time searches in
		 * non-performance-critical code. For repeated searches or searches of
		 * large substrings it's recommended to use the @ref std::search()
		 * algorithms, especially @ref std::boyer_moore_searcher and its
		 * variants. Those algorithms on the other hand have to perform certain
		 * preprocessing of the input and keep extra state and due to that
		 * overhead aren't generally suited for one-time searches. Consider
		 * using @ref find(char) const instead for single-byte substrings, see
		 * also @ref count(char) const for counting the number of occurences.
		 *
		 * This function is equivalent to calling @relativeref{std::string,find()}
		 * on a @ref std::string or a @ref std::string_view.
		 */
		/* Technically it would be enough to have just one overload with a default value
		   for the fail parameter. But then `find(foo, pointer)` would imply "find foo
		   after pointer", because that's what the second parameter does in most APIs.
		   On the other hand, naming this findOr() and documenting the custom failure handling
		   would add extra congitive load for people looking for find() and nothing else. */
		BasicStringView<T> find(StringView substring) const {
			return findOr(substring, nullptr);
		}

		/**
		 * @brief Find a character
		 *
		 * Faster than @ref find(StringView) const if the string has just one
		 * byte.
		 */
		/* Technically it would be enough to have just one overload with a default
		   value for the fail parameter, see above why it's not */
		BasicStringView<T> find(char character) const {
			return findOr(character, nullptr);
		}

		/**
		 * @brief Find a substring with a custom failure pointer
		 *
		 * Like @ref find(StringView) const, but returns an empty view pointing
		 * to the @p fail value instead of @cpp nullptr @ce, which is useful to
		 * avoid explicit handling of cases where the substring wasn't found.
		 *
		 * The @p fail value can be @cpp nullptr @ce or any other pointer, but
		 * commonly it's set to either @ref begin() or @ref end().
		 * 
		 * Consider using @ref findOr(char, T*) const for single-byte
		 * substrings.
		 */
		BasicStringView<T> findOr(StringView substring, T* fail) const;

		/**
		 * @brief Find a character with a custom failure pointer
		 *
		 * Faster than @ref findOr(StringView, T*) const if the string has just
		 * one byte.
		 */
		BasicStringView<T> findOr(char character, T* fail) const;

		/**
		 * @brief Find the last occurence of a substring
		 *
		 * Returns a view pointing to the last found substring. If not found,
		 * an empty @cpp nullptr @ce view is returned. The function uses
		 * @ref slice() internally, meaning it propagates the @ref flags() as
		 * appropriate, except in case of a failure, where it always returns no
		 * @ref StringViewFlags.
		 *
		 * Similarly as with @ref find(), note that the function operates with
		 * a @f$ \mathcal{O}(nm) @f$ complexity and as such is meant mainly for
		 * one-time searches in non-performance-critical code. See the
		 * documentation of @ref find() for further information and suggested
		 * alternatives. Consider using @ref findLast(char) const instead for
		 * single-byte substrings.
		 *
		 * This function is equivalent to calling @relativeref{std::string,rfind()}
		 * on a @ref std::string or a @ref std::string_view.
		 */
		/* Technically it would be enough to have just one overload with a default
		   value for the fail parameter, see above why it's not */
		BasicStringView<T> findLast(StringView substring) const {
			return findLastOr(substring, nullptr);
		}

		/**
		 * @brief Find the last occurence of a character
		 *
		 * Faster than @ref findLast(StringView) const if the string has just
		 * one byte.
		 */
		/* Technically it would be enough to have just one overload with a default
		   value for the fail parameter, see above why it's not */
		BasicStringView<T> findLast(char character) const {
			return findLastOr(character, nullptr);
		}

		/**
		 * @brief Find the last occurence a substring with a custom failure pointer
		 *
		 * Like @ref findLast(StringView) const, but returns an empty view
		 * pointing to the @p fail value instead of @cpp nullptr @ce, which is
		 * useful to avoid explicit handling of cases where the substring
		 * wasn't found. See @ref findOr() for an example use case.
		 */
		BasicStringView<T> findLastOr(StringView substring, T* fail) const;

		/**
		 * @brief Find the last occurence of a character with a custom failure pointer
		 *
		 * Faster than @ref findLastOr(StringView, T*) const if the string has
		 * just one byte.
		 */
		BasicStringView<T> findLastOr(char character, T* fail) const;

		/**
		 * @brief Whether the view contains a substring
		 *
		 * A slightly lighter variant of @ref find() useful when you only want
		 * to know if a substring was found or not. Consider using
		 * @ref contains(char) const for single-byte substrings, see also
		 * @ref count(char) const for counting the number of occurences.
		 */
		bool contains(StringView substring) const;

		/**
		 * @brief Whether the view contains a character
		 *
		 * Faster than @ref contains(StringView) const if the string has just
		 * one byte.
		 */
		bool contains(char character) const;

		/**
		 * @brief Find any character from given set
		 *
		 * Returns a view pointing to the first found character from the set.
		 * If no characters from @p characters are found, an empty
		 * @cpp nullptr @ce view is returned. The function uses @ref slice()
		 * internally, meaning it propagates the @ref flags() as appropriate,
		 * except in case of a failure, where it always returns no
		 * @ref StringViewFlags.
		 *
		 * This function is equivalent to calling @relativeref{std::string,find_first_of()}
		 * on a @ref std::string or a @ref std::string_view.
		 */
		BasicStringView<T> findAny(StringView characters) const {
			return findAnyOr(characters, nullptr);
		}

		/**
		 * @brief Find any character from given set with a custom failure pointer
		 *
		 * Like @ref findAny(StringView) const, but returns an empty view
		 * pointing to the @p fail value instead of @cpp nullptr @ce, which is
		 * useful to avoid explicit handling of cases where no character was
		 * found.
		 *
		 * The @p fail value can be @cpp nullptr @ce or any other pointer, but
		 * commonly it's set to either @ref begin() or @ref end().
		 */
		BasicStringView<T> findAnyOr(StringView characters, T* fail) const;

		/**
		 * @brief Find the last occurence of any character from given set
		 *
		 * Returns a view pointing to the last found character from the set.
		 * If no characters from @p characters are found, an empty
		 * @cpp nullptr @ce view is returned. The function uses
		 * @ref slice() internally, meaning it propagates the @ref flags() as
		 * appropriate, except in case of a failure, where it always returns no
		 * @ref StringViewFlags.
		 *
		 * This function is equivalent to calling @relativeref{std::string,find_last_of()}
		 * on a @ref std::string or a @ref std::string_view.
		 */
		BasicStringView<T> findLastAny(StringView characters) const {
			return findLastAnyOr(characters, nullptr);
		}

		/**
		 * @brief Find the last occurence of any character from given set with a custom failure pointer
		 *
		 * Like @ref findLastAny(StringView) const, but returns an empty view
		 * pointing to the @p fail value instead of @cpp nullptr @ce, which is
		 * useful to avoid explicit handling of cases where the substring
		 * wasn't found.
		 */
		BasicStringView<T> findLastAnyOr(StringView characters, T* fail) const;

		/**
		 * @brief Whether the view contains any character from given set
		 *
		 * A slightly lighter variant of @ref findAny() useful when you only
		 * want to know if a character was found or not.
		 */
		bool containsAny(StringView substring) const;

		/**
		 * @brief Count of occurences of given character
		 *
		 * If it's only needed to know whether a character is contained in a
		 * string at all, consider using @ref contains(char) const instead.
		 */
		std::size_t count(char character) const;

	private:
		/* Needed for mutable/immutable conversion */
		template<class> friend class BasicStringView;
		friend String;

		/* MSVC demands the export macro to be here as well */
		friend bool operator==(StringView, StringView);
		friend bool operator!=(StringView, StringView);
		friend bool operator<(StringView, StringView);
		friend bool operator<=(StringView, StringView);
		friend bool operator>=(StringView, StringView);
		friend bool operator>(StringView, StringView);
		friend String operator*(StringView, std::size_t);

		/* Called from BasicStringView(U&&, StringViewFlags), see its comment for details;
		   arguments in a flipped order to avoid accidental ambiguity. The ArrayView type
		   is a template to avoid having to include ArrayView.h. */
		template<class U, typename std::enable_if<std::is_same<T, U>::value, int>::type = 0> constexpr explicit BasicStringView(StringViewFlags flags, ArrayView<U> data) noexcept : BasicStringView{data.data(), data.size(), flags} {}

		/* Used by the char* constructor, delinlined because it calls into std::strlen() */
		explicit BasicStringView(T* data, StringViewFlags flags, std::nullptr_t) noexcept;

		/* Used by slice() to skip unneeded checks in the public constexpr constructor */
		constexpr explicit BasicStringView(T* data, std::size_t sizePlusFlags, std::nullptr_t) noexcept : _data{data}, _sizePlusFlags{sizePlusFlags} {}

		T* _data;
		std::size_t _sizePlusFlags;
	};

	/**
		@brief String view

		Immutable, use @ref MutableStringView for mutable access.
	*/
	typedef BasicStringView<const char> StringView;

	/**
		@brief Mutable string view
	*/
	typedef BasicStringView<char> MutableStringView;

	/** @brief String view equality comparison */
	bool operator==(StringView a, StringView b);

	/** @brief String view non-equality comparison */
	bool operator!=(StringView a, StringView b);

	/** @brief String view less-than comparison */
	bool operator<(StringView a, StringView b);

	/** @brief String view less-than-or-equal comparison */
	bool operator<=(StringView a, StringView b);

	/** @brief String view greater-than-or-equal comparison */
	bool operator>=(StringView a, StringView b);

	/** @brief String view greater-than comparison */
	bool operator>(StringView a, StringView b);

	/**
		@brief String multiplication

		Equivalent to string multiplication in Python, returns @p string repeated
		@p count times.
	*/
	String operator*(StringView string, std::size_t count);

	String operator*(std::size_t count, StringView string);

	namespace Literals
	{
		// According to https://wg21.link/CWG2521, space between "" and literal name is deprecated because _Uppercase
		// or _double names could be treated as reserved depending on whether the space was present or not,
		// and whitespace is not load-bearing in any other contexts. Clang 17+ adds an off-by-default warning for this;
		// GCC 4.8 however *requires* the space there, so until GCC 4.8 support is dropped, we suppress this warning
		// instead of removing the space. GCC 15 now has the same warning but it's enabled by default on -std=c++23.
		#if (defined(DEATH_TARGET_CLANG) && __clang_major__ >= 17) || (defined(DEATH_TARGET_GCC) && !defined(DEATH_TARGET_CLANG) && __GNUC__ >= 15)
		#	pragma GCC diagnostic push
		#	pragma GCC diagnostic ignored "-Wdeprecated-literal-operator"
		#endif

		/** @relatesalso Death::Containers::BasicStringView
			@brief String view literal

			The returned instance has both @ref StringViewFlags::Global and @ref StringViewFlags::NullTerminated set.
		*/
		constexpr StringView operator"" _s(const char* data, std::size_t size) {
			// Using plain bit ops instead of EnumSet to speed up debug builds
			return StringView{data, size, StringViewFlags(std::size_t(StringViewFlags::Global) | std::size_t(StringViewFlags::NullTerminated))};
		}

		#if (defined(DEATH_TARGET_CLANG) && __clang_major__ >= 17) || (defined(DEATH_TARGET_GCC) && !defined(DEATH_TARGET_CLANG) && __GNUC__ >= 15)
		#	pragma GCC diagnostic pop
		#endif
	}

	template<class T> constexpr T& BasicStringView<T>::operator[](const std::size_t i) const {
		return DEATH_DEBUG_CONSTEXPR_ASSERT(i < size() + ((flags() & StringViewFlags::NullTerminated) == StringViewFlags::NullTerminated ? 1 : 0),
					("Index {} out of range for {} {}", i, size(), ((flags() & StringViewFlags::NullTerminated) == StringViewFlags::NullTerminated ? "null-terminated bytes" : "bytes"))),
				_data[i];
	}

	template<class T> constexpr T& BasicStringView<T>::front() const {
		return DEATH_DEBUG_CONSTEXPR_ASSERT(size(), "View is empty"), _data[0];
	}

	template<class T> constexpr T& BasicStringView<T>::back() const {
		return DEATH_DEBUG_CONSTEXPR_ASSERT(size(), "View is empty"), _data[size() - 1];
	}

	template<class T> constexpr BasicStringView<T> BasicStringView<T>::slice(T* const begin, T* const end) const {
		return DEATH_DEBUG_CONSTEXPR_ASSERT(_data <= begin && begin <= end && end <= _data + (_sizePlusFlags & ~Implementation::StringViewSizeMask),
					("Slice [{}:{}] out of range for {} elements",
					 std::size_t(begin - _data), std::size_t(end - _data), (_sizePlusFlags & ~Implementation::StringViewSizeMask))),
				BasicStringView<T>{begin, std::size_t(end - begin) |
					// Propagate the global flag always
					(_sizePlusFlags & std::size_t(StringViewFlags::Global)) |
					// The null termination flag only if the original is null-terminated and end points to the original end
					((_sizePlusFlags & std::size_t(StringViewFlags::NullTerminated)) * (end == _data + (_sizePlusFlags & ~Implementation::StringViewSizeMask))),
					// Using an internal assert-less constructor, the public constructor asserts would be redundant
					nullptr};
	}

	template<class T> constexpr BasicStringView<T> BasicStringView<T>::slice(const std::size_t begin, const std::size_t end) const {
		return DEATH_DEBUG_CONSTEXPR_ASSERT(begin <= end && end <= (_sizePlusFlags & ~Implementation::StringViewSizeMask),
					("Slice [{}:{}] out of range for {} elements",
					 begin, end, (_sizePlusFlags & ~Implementation::StringViewSizeMask))),
				BasicStringView<T>{_data + begin, (end - begin) |
					// Propagate the global flag always
					(_sizePlusFlags & std::size_t(StringViewFlags::Global)) |
					// The null termination flag only if the original is null-terminated and end points to the original end
					((_sizePlusFlags & std::size_t(StringViewFlags::NullTerminated)) * (end == (_sizePlusFlags & ~Implementation::StringViewSizeMask))),
					// Using an internal assert-less constructor, the public constructor asserts would be redundant
					nullptr};
	}

	namespace Implementation
	{
		const char* stringFindString(const char* data, std::size_t size, const char* substring, std::size_t substringSize);
		const char* stringFindLastString(const char* data, std::size_t size, const char* substring, std::size_t substringSize);
		extern const char* DEATH_CPU_DISPATCHED_DECLARATION(stringFindCharacter)(const char* data, std::size_t size, char character);
		DEATH_CPU_DISPATCHER_DECLARATION(stringFindCharacter)
		const char* stringFindLastCharacter(const char* data, std::size_t size, char character);
		const char* stringFindAny(const char* data, std::size_t size, const char* characters, std::size_t characterCount);
		const char* stringFindLastAny(const char* data, std::size_t size, const char* characters, std::size_t characterCount);
		const char* stringFindNotAny(const char* data, std::size_t size, const char* characters, std::size_t characterCount);
		const char* stringFindLastNotAny(const char* data, std::size_t size, const char* characters, std::size_t characterCount);
		extern std::size_t DEATH_CPU_DISPATCHED_DECLARATION(stringCountCharacter)(const char* data, std::size_t size, char character);
		DEATH_CPU_DISPATCHER_DECLARATION(stringCountCharacter)
	}

	template<class T> inline BasicStringView<T> BasicStringView<T>::trimmedPrefix(const StringView characters) const {
		const std::size_t size = this->size();
		T* const found = const_cast<T*>(Implementation::stringFindNotAny(_data, size, characters._data, characters.size()));
		return suffix(found ? found : _data + size);
	}

	template<class T> inline BasicStringView<T> BasicStringView<T>::trimmedSuffix(const StringView characters) const {
		T* const found = const_cast<T*>(Implementation::stringFindLastNotAny(_data, size(), characters._data, characters.size()));
		return prefix(found ? found + 1 : _data);
	}

	template<class T> inline BasicStringView<T> BasicStringView<T>::findOr(const StringView substring, T* const fail) const {
		// Cache the getters to speed up debug builds
		const std::size_t substringSize = substring.size();
		if (const char* const found = Implementation::stringFindString(_data, size(), substring._data, substringSize))
			return slice(const_cast<T*>(found), const_cast<T*>(found + substringSize));

		// Using an internal assert-less constructor, the public constructor asserts would be redundant.
		// Since it's a zero-sized view, it doesn't really make sense to try to preserve any flags.
		return BasicStringView<T>{fail, 0 /* empty, no flags */, nullptr};
	}

	template<class T> inline BasicStringView<T> BasicStringView<T>::findOr(const char character, T* const fail) const {
		if (const char* const found = Implementation::stringFindCharacter(_data, size(), character))
			return slice(const_cast<T*>(found), const_cast<T*>(found + 1));

		// Using an internal assert-less constructor, the public constructor asserts would be redundant.
		// Since it's a zero-sized view, it doesn't really make sense to try to preserve any flags.
		return BasicStringView<T>{fail, 0 /* empty, no flags */, nullptr};
	}

	template<class T> inline BasicStringView<T> BasicStringView<T>::findLastOr(const StringView substring, T* const fail) const {
		// Cache the getters to speed up debug builds
		const std::size_t substringSize = substring.size();
		if (const char* const found = Implementation::stringFindLastString(_data, size(), substring._data, substringSize))
			return slice(const_cast<T*>(found), const_cast<T*>(found + substringSize));

		// Using an internal assert-less constructor, the public constructor asserts would be redundant.
		// Since it's a zero-sized view, it doesn't really make sense to try to preserve any flags.
		return BasicStringView<T>{fail, 0 /* empty, no flags */, nullptr};
	}

	template<class T> inline BasicStringView<T> BasicStringView<T>::findLastOr(const char character, T* const fail) const {
		if (const char* const found = Implementation::stringFindLastCharacter(_data, size(), character))
			return slice(const_cast<T*>(found), const_cast<T*>(found + 1));

		// Using an internal assert-less constructor, the public constructor asserts would be redundant.
		// Since it's a zero-sized view, it doesn't really make sense to try to preserve any flags.
		return BasicStringView<T>{fail, 0 /* empty, no flags */, nullptr};
	}

	template<class T> inline bool BasicStringView<T>::contains(const StringView substring) const {
		return Implementation::stringFindString(_data, size(), substring._data, substring.size());
	}

	template<class T> inline bool BasicStringView<T>::contains(const char character) const {
		return Implementation::stringFindCharacter(_data, size(), character);
	}

	template<class T> inline BasicStringView<T> BasicStringView<T>::findAnyOr(const StringView characters, T* const fail) const {
		if (const char* const found = Implementation::stringFindAny(_data, size(), characters._data, characters.size()))
			return slice(const_cast<T*>(found), const_cast<T*>(found + 1));

		// Using an internal assert-less constructor, the public constructor asserts would be redundant.
		// Since it's a zero-sized view, it doesn't really make sense to try to preserve any flags.
		return BasicStringView<T>{fail, 0 /* empty, no flags */, nullptr};
	}

	template<class T> inline BasicStringView<T> BasicStringView<T>::findLastAnyOr(const StringView characters, T* const fail) const {
		if (const char* const found = Implementation::stringFindLastAny(_data, size(), characters._data, characters.size()))
			return slice(const_cast<T*>(found), const_cast<T*>(found + 1));

		// Using an internal assert-less constructor, the public constructor asserts would be redundant.
		// Since it's a zero-sized view, it doesn't really make sense to try to preserve any flags.
		return BasicStringView<T>{fail, 0 /* empty, no flags */, nullptr};
	}

	template<class T> inline bool BasicStringView<T>::containsAny(const StringView characters) const {
		return Implementation::stringFindAny(_data, size(), characters._data, characters.size());
	}

	template<class T> inline std::size_t BasicStringView<T>::count(const char character) const {
		return Implementation::stringCountCharacter(_data, size(), character);
	}

	namespace Implementation
	{
		template<class> struct ErasedArrayViewConverter;

		// Strangely enough, if the from() functions don't accept T& but just T, it leads to an infinite template recursion depth
		template<> struct ArrayViewConverter<char, BasicStringView<char>> {
			static ArrayView<char> from(const BasicStringView<char>& other);
		};
		template<> struct ArrayViewConverter<const char, BasicStringView<char>> {
			static ArrayView<const char> from(const BasicStringView<char>& other);
		};
		template<> struct ArrayViewConverter<const char, BasicStringView<const char>> {
			static ArrayView<const char> from(const BasicStringView<const char>& other);
		};
		template<class T> struct ErasedArrayViewConverter<BasicStringView<T>> : ArrayViewConverter<T, BasicStringView<T>> {};
		template<class T> struct ErasedArrayViewConverter<const BasicStringView<T>> : ArrayViewConverter<T, BasicStringView<T>> {};
	}

}}