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

#include "Array.h"
#include "SequenceHelpers.h"

namespace Death { namespace Containers {
//###==##====#=====--==~--~=~- --- -- -  -  -   -

	namespace Implementation
	{
		template<std::size_t size_, class T, bool trivial> struct StaticArrayData;
		template<std::size_t size_, class T> struct StaticArrayData<size_, T, true> {
			// Here we additionally deal with types that have a NoInit constructor
			template<class U = T, typename std::enable_if<!std::is_constructible<U, NoInitT>::value, int>::type = 0> explicit StaticArrayData(NoInitT) {}
			template<class U = T, typename std::enable_if<std::is_constructible<U, NoInitT>::value, int>::type = 0> explicit StaticArrayData(NoInitT) : StaticArrayData{NoInit, typename GenerateSequence<size_>::Type{}} {}
			template<std::size_t ...sequence, class U = T, typename std::enable_if<std::is_constructible<U, NoInitT>::value, int>::type = 0> explicit StaticArrayData(NoInitT noInit, Sequence<sequence...>) : _data{T{(&noInit)[0 * sequence]}...} {}

			// Same as in StaticArrayData<size_, T, false>. The () instead of {} works around a featurebug in C++
			// where new T{} doesn't work for an explicit defaulted constructor.
			constexpr explicit StaticArrayData(ValueInitT) : _data() {}

			// Same as in StaticArrayData<size_, T, false>
			template<class ...Args> constexpr explicit StaticArrayData(InPlaceInitT, Args&&... args) : _data{forward<Args>(args)...} {}
			template<std::size_t ...sequence> constexpr explicit StaticArrayData(InPlaceInitT, Sequence<sequence...>, const T(&data)[sizeof...(sequence)]) : _data{data[sequence]...} {}

#ifndef DEATH_MSVC2017_COMPATIBILITY
			template<std::size_t ...sequence> constexpr explicit StaticArrayData(InPlaceInitT, Sequence<sequence...>, T(&& data)[sizeof...(sequence)]) : _data{Death::move(data[sequence])...} {}
#endif

			T _data[size_];
		};

		template<std::size_t size_, class T> struct StaticArrayData<size_, T, false> {
			// Compared to StaticArrayData<size_, T, true> it does the right thing by default
			explicit StaticArrayData(NoInitT) {}

			// Compared to StaticArrayData<size_, T, true> a default constructor has to be called on the union members.
			// If the default constructor is trivial, the StaticArrayData<size_, T, true> base was picked instead.
			// The () instead of {} works around a featurebug in C++ where new T{} doesn't work for an explicit defaulted constructor.
			explicit StaticArrayData(ValueInitT) : _data() {}

			// Same as in StaticArrayData<size_, T, true>
			template<class ...Args> explicit StaticArrayData(InPlaceInitT, Args&&... args) : _data{forward<Args>(args)...} {}
			template<std::size_t ...sequence> explicit StaticArrayData(InPlaceInitT, Implementation::Sequence<sequence...>, const T(&data)[sizeof...(sequence)]) : _data{data[sequence]...} {}

#ifndef DEATH_MSVC2017_COMPATIBILITY
			template<std::size_t ...sequence> explicit StaticArrayData(InPlaceInitT, Implementation::Sequence<sequence...>, T(&& data)[sizeof...(sequence)]) : _data{Death::move(data[sequence])...} {}
#endif

			// Compared to StaticArrayData<size_, T, true> we need to explicitly copy/move the union members
			StaticArrayData(const StaticArrayData<size_, T, false>& other) noexcept(std::is_nothrow_copy_constructible<T>::value);
			StaticArrayData(StaticArrayData<size_, T, false>&& other) noexcept(std::is_nothrow_move_constructible<T>::value);
			~StaticArrayData();
			StaticArrayData<size_, T, false>& operator=(const StaticArrayData<size_, T, false>&) noexcept(std::is_nothrow_copy_constructible<T>::value);
			StaticArrayData<size_, T, false>& operator=(StaticArrayData<size_, T, false>&&) noexcept(std::is_nothrow_move_constructible<T>::value);

			union {
				T _data[size_];
			};
		};

		template<std::size_t size_, class T> using StaticArrayDataFor = StaticArrayData<size_, T,
			/* std::is_trivially_constructible fails for (template) types where default
			   constructor isn't usable in libstdc++ before version 8, OTOH
			   std::is_trivial is deprecated in C++26 so can't use that one either.
			   Furthermore, libstdc++ before 6.1 doesn't have _GLIBCXX_RELEASE, so
			   there comparison will ealuate to 0 < 8 and pass as well. */
#if defined(DEATH_TARGET_LIBSTDCXX) && _GLIBCXX_RELEASE < 8
			std::is_trivial<T>::value
#else
			std::is_trivially_constructible<T>::value
#endif
			|| std::is_constructible<T, NoInitT>::value>;

	}

	/**
		@brief Compile-time-sized array
		@tparam size_   Array size
		@tparam T       Element type

		Like @ref Array, but with compile-time size information. Useful as a more
		featureful alternative to plain C arrays or @ref std::array, especially when it
		comes to initialization. A non-owning version of this container is a
		@ref StaticArrayView.

		@section Containers-StaticArray-usage Usage

		The @ref StaticArray class provides an access and slicing API similar to
		@ref Array, which in turn shares the basic workflow patterns with
		@ref ArrayView, see @ref Containers-ArrayView-usage "its usage docs" for
		details. The main difference is that @ref StaticArray doesn't do any heap
		allocation and thus has no concept of a deleter, and it has additional
		compile-time-sized overloads of @ref slice(), @ref prefix(), @ref suffix(),
		@ref exceptPrefix() and @ref exceptSuffix(), mirroring the APIs of
		@ref StaticArrayView.

		@subsection Containers-StaticArray-usage-initialization Array initialization

		The array is by default *value-initialized*, which means that trivial types
		are zero-initialized and the default constructor is called on other types. It
		is possible to initialize the array in a different way using so-called *tags*:

		-   @ref StaticArray(ValueInitT) is equivalent to the implicit parameterless
			constructor, zero-initializing trivial types and calling the default
			constructor elsewhere. Useful when you want to make the choice appear
			explicit. In other words, @cpp T array[size]{} @ce.
		-   @ref StaticArray(DirectInitT, Args&&... args) constructs every element of
			the array using provided arguments. In other words,
			@cpp T array[size]{T{args...}, T{args...}, …} @ce.
		-   @ref StaticArray(InPlaceInitT, Args&&... args) is equivalent to
			@ref StaticArray(Args&&... args). Again useful when you want to make
			the choice appear explicit). In other words,
			@cpp T array[size]{args...} @ce. Note that the variadic template means you
			can't use @cpp {} @ce for nested type initializers but have to specify the
			types explicitly. An alternative is directly passing an array, i.e. with
			the items wrapped in an additional @cpp {} @ce, with
			@ref StaticArray(InPlaceInitT, const T(&)[size]) or
			@ref StaticArray(InPlaceInitT, T(&&)[size]), or using the implicit
			@ref StaticArray(const T(&)[size]) and @ref StaticArray(T(&&)[size])
			variants.
		-   @ref StaticArray(NoInitT) does not initialize anything. Useful for trivial
			types when you'll be overwriting the contents anyway, for non-trivial types
			this is the dangerous option and you need to call the constructor on all
			elements manually using placement new, @ref std::uninitialized_copy() or
			similar. In other words, @cpp char array[size*sizeof(T)] @ce for non-trivial
			types to circumvent default construction and @cpp T array[size] @ce for trivial types.

		@subsection Containers-StaticArray-constexpr Usage in constexpr contexts

		In order to implement the @ref StaticArray(NoInitT) constructor for arbitrary
		types, internally the data has to be a @cpp union @ce array. That would however
		lose trivial copyability and possibility to use the type in @cpp constexpr @ce
		contexts, so to solve that, types that have a trivial default constructor or a
		constructor taking @ref NoInitT use a specialized internal
		representation without a @cpp union @ce. @ref StaticArray of such types is then
		a @cpp constexpr @ce type and is trivially copyable if the underlying type is
		trivially copyable.

		@section Containers-StaticArray-views Conversion to array views

		Arrays are implicitly convertible to @ref ArrayView / @ref StaticArrayView as
		described in the following table. The conversion is only allowed if @cpp T* @ce
		is implicitly convertible to @cpp U* @ce (or both are the same type) and both
		have the same size.

		Owning array type               | ↭ | Non-owning view type
		------------------------------- | - | ---------------------
		@ref StaticArray "Array<size, T>" | → | @ref StaticArrayView "ArrayView<size, U>"
		@ref StaticArray "Array<size, T>" | → | @ref StaticArrayView "ArrayView<size, const U>"
		@ref StaticArray "const Array<size, T>" | → | @ref StaticArrayView "ArrayView<size, const U>"
		@ref StaticArray "Array<size, T>" | → | @ref ArrayView "ArrayView&lt;U&gt;"
		@ref StaticArray "Array<size, T>" | → | @ref ArrayView "ArrayView<const U>"
		@ref StaticArray "const Array<size, T>" | → | @ref ArrayView "ArrayView<const U>"
	 */
	template<std::size_t size_, class T> class StaticArray : Implementation::StaticArrayDataFor<size_, T>
	{
	public:
		/** @brief Element type */
		typedef T Type;

		enum : std::size_t {
			Size = size_	/**< Array size */
		};

		/**
		 * @brief Construct a value-initialized array
		 *
		 * Creates array of given size, the contents are value-initialized
		 * (i.e., trivial types are zero-initialized, default constructor
		 * called otherwise). This is the same as @ref StaticArray().
		 */
		constexpr explicit StaticArray(ValueInitT) : Implementation::StaticArrayDataFor<size_, T>{ValueInit} {}

		/**
		 * @brief Construct an array without initializing its contents
		 *
		 * Creates array of given size, the contents are *not* initialized.
		 * Useful if you will be overwriting all elements later anyway or if
		 * you need to call custom constructors in a way that's not expressible
		 * via any other @ref StaticArray constructor.
		 *
		 * For trivial types is equivalent to @cpp T array[size] @ce (as
		 * opposed to @cpp T array[size]{} @ce). For non-trivial types, class
		 * destruction will explicitly call the destructor on *all elements*
		 * --- which means that for non-trivial types you're expected to
		 * construct all elements using placement new (or for example
		 * @ref std::uninitialized_copy()) in order to avoid calling
		 * destructors on uninitialized memory.
		 */
		explicit StaticArray(NoInitT) : Implementation::StaticArrayDataFor<size_, T>{NoInit} {}

		/**
		 * @brief Construct a direct-initialized array
		 *
		 * Constructs the array using the @ref StaticArray(NoInitT) constructor
		 * and then initializes each element with placement new using forwarded
		 * @p args.
		 */
		/* Not constexpr as it delegates to a NoInit constructor */
		template<class ...Args> explicit StaticArray(DirectInitT, Args&&... args);

		/**
		 * @brief Construct an in-place-initialized array
		 *
		 * The arguments are forwarded to the array constructor. Note that the
		 * variadic template means you can't use @cpp {} @ce for nested type
		 * initializers --- see @ref StaticArray(InPlaceInitT, const T(&)[size])
		 * or @ref StaticArray(InPlaceInitT, T(&&)[size]) for an
		 * alternative. Same as @ref StaticArray(Args&&... args).
		 *
		 * To prevent accidents, compared to regular C array initialization the
		 * constructor expects the number of arguments to match the size
		 * *exactly*. I.e., it's not possible to omit a suffix of the array to
		 * implicitly value-initialize it.
		 */
		template<class ...Args> constexpr explicit StaticArray(InPlaceInitT, Args&&... args) : Implementation::StaticArrayDataFor<size_, T>{InPlaceInit, Death::forward<Args>(args)...} {
			static_assert(sizeof...(args) == size_, "Containers::StaticArray: Wrong number of initializers");
		}

		/**
		 * @brief In-place construct an array by copying the elements from a fixed-size array
		 *
		 * Compared to @ref StaticArray(InPlaceInitT, Args&&... args) doesn't
		 * require the elements to have explicitly specified type. The array
		 * elements are copied to the array constructor, if you have a
		 * non-copyable type or want to move the elements, use
		 * @ref StaticArray(InPlaceInitT, T(&&)[size]) instead. Same as
		 * @ref StaticArray(const T(&)[size]).
		 *
		 * To prevent accidents, compared to regular C array initialization the
		 * constructor expects the number of arguments to match the size
		 * *exactly*. I.e., it's not possible to omit a suffix of the array to
		 * implicitly value-initialize it.
		 */
#if !defined(DEATH_TARGET_GCC) || defined(DEATH_TARGET_CLANG) || __GNUC__ >= 5
		template<std::size_t size> constexpr /*implicit*/ StaticArray(InPlaceInitT, const T(&data)[size]) : Implementation::StaticArrayDataFor<size_, T>{InPlaceInit, typename Implementation::GenerateSequence<size>::Type{}, data} {
			static_assert(size == size_, "Containers::StaticArray: Wrong number of initializers");
		}
#else
		/* GCC 4.8 isn't able to figure out the size on its own. Which means
		   there we use the type-provided size and lose the check for element
		   count, but at least it compiles. */
		constexpr /*implicit*/ StaticArray(InPlaceInitT, const T(&data)[size_]) : Implementation::StaticArrayDataFor<size_, T>{InPlaceInit, typename Implementation::GenerateSequence<size_>::Type{}, data} {}
#endif

#if !defined(DEATH_MSVC2017_COMPATIBILITY)
		/**
		* @brief In-place construct an array by moving the elements from a fixed-size array
		*
		* Compared to @ref StaticArray(InPlaceInitT, Args&&... args) doesn't
		* require the elements to have an explicitly specified type. Same as
		* @ref StaticArray(T(&&)[size]).
		* @partialsupport Not available on
		*      @ref DEATH_MSVC2015_COMPATIBILITY "MSVC 2015" and
		*      @ref DEATH_MSVC2017_COMPATIBILITY "MSVC 2017" as these
		*      compilers don't support moving arrays.
		*/
#	if !defined(DEATH_TARGET_GCC) || defined(DEATH_TARGET_CLANG) || __GNUC__ >= 5
		template<std::size_t size> constexpr /*implicit*/ StaticArray(InPlaceInitT, T(&&data)[size]) : Implementation::StaticArrayDataFor<size_, T>{InPlaceInit, typename Implementation::GenerateSequence<size>::Type{}, Death::move(data)} {
			static_assert(size == size_, "Containers::StaticArray: Wrong number of initializers");
		}
#	else
		/* GCC 4.8 isn't able to figure out the size on its own. Which means
		   there we use the type-provided size and lose the check for element
		   count, but at least it compiles. */
		constexpr /*implicit*/ StaticArray(InPlaceInitT, T(&&data)[size_]) : Implementation::StaticArrayDataFor<size_, T>{InPlaceInit, typename Implementation::GenerateSequence<size_>::Type{}, Death::move(data)} {}
#	endif
#endif

		/**
		 * @brief Construct a value-initialized array
		 *
		 * Alias to @ref StaticArray(ValueInitT).
		 */
		constexpr /*implicit*/ StaticArray() : Implementation::StaticArrayDataFor<size_, T>{ValueInit} {}

		/**
		 * @brief Construct an in-place-initialized array
		 *
		 * Alias to @ref StaticArray(InPlaceInitT, Args&&... args).
		 */
#ifdef DOXYGEN_GENERATING_OUTPUT
		template<class ...Args> constexpr /*implicit*/ StaticArray(Args&&... args);
#else
		template<class First, class ...Next, typename std::enable_if<std::is_convertible<First&&, T>::value, int>::type = 0> constexpr /*implicit*/ StaticArray(First&& first, Next&&... next) : Implementation::StaticArrayDataFor<size_, T>{InPlaceInit, Death::forward<First>(first), Death::forward<Next>(next)...} {
			static_assert(sizeof...(next) + 1 == size_, "Containers::StaticArray: Wrong number of initializers");
		}
#endif

		/**
		 * @brief In-place construct an array by copying the elements from a fixed-size array
		 *
		 * Alias to @ref StaticArray(InPlaceInitT, const T(&)[size]).
		 */
#if !defined(DEATH_TARGET_GCC) || defined(DEATH_TARGET_CLANG) || __GNUC__ >= 5
		template<std::size_t size> constexpr /*implicit*/ StaticArray(const T(&data)[size]) : Implementation::StaticArrayDataFor<size_, T>{InPlaceInit, typename Implementation::GenerateSequence<size>::Type{}, data} {
			static_assert(size == size_, "Containers::StaticArray: Wrong number of initializers");
		}
#else
		/* GCC 4.8 isn't able to figure out the size on its own. Which means
		   there we use the type-provided size and lose the check for element
		   count, but at least it compiles. */
		constexpr /*implicit*/ StaticArray(const T(&data)[size_]) : Implementation::StaticArrayDataFor<size_, T>{InPlaceInit, typename Implementation::GenerateSequence<size_>::Type{}, data} {}
#endif

#if !defined(DEATH_MSVC2017_COMPATIBILITY)
		/**
		* @brief In-place construct an array by moving the elements from a fixed-size array
		*
		* Alias to @ref StaticArray(InPlaceInitT, T(&&)[size]).
		* @partialsupport Not available on
		*      @ref DEATH_MSVC2015_COMPATIBILITY "MSVC 2015" and
		*      @ref DEATH_MSVC2017_COMPATIBILITY "MSVC 2017" as these
		*      compilers don't support moving arrays.
		*/
#	if !defined(DEATH_TARGET_GCC) || defined(DEATH_TARGET_CLANG) || __GNUC__ >= 5
		template<std::size_t size> constexpr /*implicit*/ StaticArray(T(&&data)[size]) : Implementation::StaticArrayDataFor<size_, T>{InPlaceInit, typename Implementation::GenerateSequence<size>::Type{}, Death::move(data)} {
			static_assert(size == size_, "Containers::StaticArray: Wrong number of initializers");
		}
#	else
		/* GCC 4.8 isn't able to figure out the size on its own. Which means
		   there we use the type-provided size and lose the check for element
		   count, but at least it compiles. */
		constexpr /*implicit*/ StaticArray(T(&&data)[size_]) : Implementation::StaticArrayDataFor<size_, T>{InPlaceInit, typename Implementation::GenerateSequence<size_>::Type{}, Death::move(data)} {}
#	endif
#endif

		/** @brief Convert to external view representation */
		template<class U, class = decltype(Implementation::StaticArrayViewConverter<size_, T, U>::to(std::declval<StaticArrayView<size_, T>>()))> /*implicit*/ operator U() {
			return Implementation::StaticArrayViewConverter<size_, T, U>::to(*this);
		}

		/** @overload */
		template<class U, class = decltype(Implementation::StaticArrayViewConverter<size_, const T, U>::to(std::declval<StaticArrayView<size_, const T>>()))> constexpr /*implicit*/ operator U() const {
			return Implementation::StaticArrayViewConverter<size_, const T, U>::to(*this);
		}

#if !defined(DEATH_MSVC2019_COMPATIBILITY)
		/* Disabled on MSVC without `/permissive-` to avoid ambiguous operator+() when doing pointer arithmetic. */
		/** @brief Whether the array is non-empty */
		constexpr explicit operator bool() const { return true; }
#endif

		/** @brief Conversion to array type */
		/*implicit*/ operator T*() & { return this->_data; }

		/** @overload */
		constexpr /*implicit*/ operator const T*() const & { return this->_data; }

		/** @brief Array data */
		T* data() { return this->_data; }
		constexpr const T* data() const { return this->_data; }				/**< @overload */

		/**
		 * @brief Array size
		 *
		 * Equivalent to @ref Size.
		 */
		constexpr std::size_t size() const { return size_; }

		/**
		 * @brief Whether the array is empty
		 *
		 * Always @cpp true @ce (it's not possible to create a zero-sized C
		 * array).
		 */
		constexpr bool empty() const { return !size_; }

		/** @brief Pointer to the first element */
		T* begin() { return this->_data; }
		constexpr const T* begin() const { return this->_data; }			/**< @overload */
		constexpr const T* cbegin() const { return this->_data; }			/**< @overload */

		/** @brief Pointer to (one item after) the last element */
		T* end() { return this->_data + size_; }
		constexpr const T* end() const { return this->_data + size_; }		/**< @overload */
		constexpr const T* cend() const { return this->_data + size_; }		/**< @overload */

		/** @brief First element */
		T& front() { return this->_data[0]; }
		constexpr const T& front() const { return this->_data[0]; }			/**< @overload */

		/** @brief Last element */
		T& back() { return this->_data[size_ - 1]; }
		constexpr const T& back() const { return this->_data[size_ - 1]; }	/**< @overload */

		/**
		 * @brief Element access
		 *
		 * Expects that @p i is less than @ref size().
		 */
#ifdef DOXYGEN_GENERATING_OUTPUT
		T& operator[](std::size_t i);
		/** @overload */
		constexpr const T& operator[](std::size_t i) const;
#else
		template<class U, typename std::enable_if<std::is_convertible<U, std::size_t>::value, int>::type = 0> T& operator[](U i);
		/** @overload */
		template<class U, typename std::enable_if<std::is_convertible<U, std::size_t>::value, int>::type = 0> constexpr const T& operator[](U i) const;
#endif

		/**
		 * @brief View on a slice
		 *
		 * Equivalent to @ref StaticArrayView::slice(T*, T*) const and
		 * overloads.
		 */
		ArrayView<T> slice(T* begin, T* end) {
			return ArrayView<T>(*this).slice(begin, end);
		}
		/** @overload */
		constexpr ArrayView<const T> slice(const T* begin, const T* end) const {
			return ArrayView<const T>(*this).slice(begin, end);
		}
		/** @overload */
		constexpr ArrayView<T> slice(std::size_t begin, std::size_t end) {
			return ArrayView<T>(*this).slice(begin, end);
		}
		/** @overload */
		constexpr ArrayView<const T> slice(std::size_t begin, std::size_t end) const {
			return ArrayView<const T>(*this).slice(begin, end);
		}

		/**
		 * @brief View on a slice of given size
		 *
		 * Equivalent to @ref StaticArrayView::sliceSize(T*, std::size_t) const
		 * and overloads.
		 */
#ifdef DOXYGEN_GENERATING_OUTPUT
		ArrayView<T> sliceSize(T* begin, std::size_t size);
#else
		template<class U, typename std::enable_if<std::is_convertible<U, T*>::value && !std::is_convertible<U, std::size_t>::value, int>::type = 0> ArrayView<T> sliceSize(U begin, std::size_t size) {
			return ArrayView<T>{*this}.sliceSize(begin, size);
		}
#endif
		/** @overload */
#ifdef DOXYGEN_GENERATING_OUTPUT
		constexpr ArrayView<const T> sliceSize(const T* begin, std::size_t size) const;
#else
		template<class U, typename std::enable_if<std::is_convertible<U, const T*>::value && !std::is_convertible<U, std::size_t>::value, int>::type = 0> constexpr ArrayView<const T> sliceSize(const U begin, std::size_t size) const {
			return ArrayView<const T>{*this}.sliceSize(begin, size);
		}
#endif
		/** @overload */
		ArrayView<T> sliceSize(std::size_t begin, std::size_t size) {
			return ArrayView<T>{*this}.sliceSize(begin, size);
		}
		/** @overload */
		constexpr ArrayView<const T> sliceSize(std::size_t begin, std::size_t size) const {
			return ArrayView<const T>{*this}.sliceSize(begin, size);
		}

		/**
		 * @brief Fixed-size view on a slice
		 *
		 * Equivalent to @ref StaticArrayView::slice(T*) const and overloads.
		 */
#ifdef DOXYGEN_GENERATING_OUTPUT
		template<std::size_t size__> StaticArrayView<size__, T> slice(T* begin);
#else
		template<std::size_t size__, class U, typename std::enable_if<std::is_convertible<U, T*>::value && !std::is_convertible<U, std::size_t>::value, int>::type = 0> StaticArrayView<size__, T> slice(U begin) {
			return ArrayView<T>(*this).template slice<size__>(begin);
		}
#endif
		/** @overload */
#ifdef DOXYGEN_GENERATING_OUTPUT
		template<std::size_t size__> constexpr StaticArrayView<size__, const T> slice(const T* begin) const;
#else
		template<std::size_t size__, class U, typename std::enable_if<std::is_convertible<U, const T*>::value && !std::is_convertible<U, std::size_t>::value, int>::type = 0> constexpr StaticArrayView<size__, const T> slice(U begin) const {
			return ArrayView<const T>(*this).template slice<size__>(begin);
		}
#endif
		/** @overload */
		template<std::size_t size__> StaticArrayView<size__, T> slice(std::size_t begin) {
			return ArrayView<T>(*this).template slice<size__>(begin);
		}
		/** @overload */
		template<std::size_t size__> constexpr StaticArrayView<size__, const T> slice(std::size_t begin) const {
			return ArrayView<const T>(*this).template slice<size__>(begin);
		}

		/**
		 * @brief Fixed-size view on a slice
		 *
		 * Equivalent to @ref StaticArrayView::slice() const.
		 */
		template<std::size_t begin_, std::size_t end_> StaticArrayView<end_ - begin_, T> slice() {
			return StaticArrayView<size_, T>(*this).template slice<begin_, end_>();
		}
		/** @overload */
		template<std::size_t begin_, std::size_t end_> constexpr StaticArrayView<end_ - begin_, const T> slice() const {
			return StaticArrayView<size_, const T>(*this).template slice<begin_, end_>();
		}

		/**
		 * @brief Fixed-size view on a slice of given size
		 *
		 * Equivalent to @ref StaticArrayView::sliceSize() const.
		 */
		template<std::size_t begin_, std::size_t size__> StaticArrayView<size__, T> sliceSize() {
			return StaticArrayView<size_, T>(*this).template sliceSize<begin_, size__>();
		}
		/** @overload */
		template<std::size_t begin_, std::size_t size__> constexpr StaticArrayView<size__, const T> sliceSize() const {
			return StaticArrayView<size_, const T>(*this).template sliceSize<begin_, size__>();
		}

		/**
		 * @brief View on a prefix until a pointer
		 *
		 * Equivalent to @ref StaticArrayView::prefix(T*) const.
		 */
#ifdef DOXYGEN_GENERATING_OUTPUT
		ArrayView<T> prefix(T* end);
#else
		template<class U, typename std::enable_if<std::is_convertible<U, T*>::value && !std::is_convertible<U, std::size_t>::value, int>::type = 0> ArrayView<T> prefix(U end) {
			return ArrayView<T>(*this).prefix(end);
		}
#endif
		/** @overload */
#ifdef DOXYGEN_GENERATING_OUTPUT
		constexpr ArrayView<const T> prefix(const T* end) const;
#else
		template<class U, typename std::enable_if<std::is_convertible<U, const T*>::value && !std::is_convertible<U, std::size_t>::value, int>::type = 0> constexpr ArrayView<const T> prefix(U end) const {
			return ArrayView<const T>(*this).prefix(end);
		}
#endif

		/**
		 * @brief View on a suffix after a pointer
		 *
		 * Equivalent to @ref StaticArrayView::suffix(T*) const.
		 */
		ArrayView<T> suffix(T* begin) {
			return ArrayView<T>(*this).suffix(begin);
		}
		/** @overload */
		constexpr ArrayView<const T> suffix(const T* begin) const {
			return ArrayView<const T>(*this).suffix(begin);
		}

		/**
		 * @brief View on the first @p size items
		 *
		 * Equivalent to @ref StaticArrayView::prefix(std::size_t) const.
		 */
		ArrayView<T> prefix(std::size_t size) {
			return ArrayView<T>(*this).prefix(size);
		}
		/** @overload */
		constexpr ArrayView<const T> prefix(std::size_t size) const {
			return ArrayView<const T>(*this).prefix(size);
		}

		/**
		* @brief Fixed-size view on the first @p size__ items
		*
		* Equivalent to @ref StaticArrayView::prefix() const and overloads.
		*/
		template<std::size_t size__> StaticArrayView<size__, T> prefix();
		/** @overload */
		template<std::size_t size__> constexpr StaticArrayView<size__, const T> prefix() const;

		/**
		 * @brief View except the first @p size items
		 *
		 * Equivalent to @ref StaticArrayView::exceptPrefix(std::size_t) const.
		 */
		ArrayView<T> exceptPrefix(std::size_t size) {
			return ArrayView<T>(*this).exceptPrefix(size);
		}
		/** @overload */
		constexpr ArrayView<const T> exceptPrefix(std::size_t size) const {
			return ArrayView<const T>(*this).exceptPrefix(size);
		}

		/**
		* @brief Fixed-size view except the first @p size__ items
		*
		* Equivalent to @ref StaticArrayView::exceptPrefix() const and
		* overloads.
		*/
		template<std::size_t size__> StaticArrayView<size_ - size__, T> exceptPrefix() {
			return StaticArrayView<size_, T>(*this).template exceptPrefix<size__>();
		}
		/** @overload */
		template<std::size_t size__> constexpr StaticArrayView<size_ - size__, const T> exceptPrefix() const {
			return StaticArrayView<size_, const T>(*this).template exceptPrefix<size__>();
		}

		/**
		 * @brief View except the last @p size items
		 *
		 * Equivalent to @ref StaticArrayView::exceptSuffix(std::size_t) const.
		 */
		ArrayView<T> exceptSuffix(std::size_t size) {
			return ArrayView<T>(*this).exceptSuffix(size);
		}
		/** @overload */
		constexpr ArrayView<const T> exceptSuffix(std::size_t size) const {
			return ArrayView<const T>(*this).exceptSuffix(size);
		}

		/**
		 * @brief Fixed-size view except the last @p size__ items
		 *
		 * Equivalent to @ref StaticArrayView::exceptSuffix() const.
		 */
		template<std::size_t size__> StaticArrayView<size_ - size__, T> exceptSuffix() {
			return StaticArrayView<size_, T>(*this).template exceptSuffix<size__>();
		}
		/** @overload */
		template<std::size_t size__> constexpr StaticArrayView<size_ - size__, const T> exceptSuffix() const {
			return StaticArrayView<size_, const T>(*this).template exceptSuffix<size__>();
		}

	private:
#if DEATH_CXX_STANDARD > 201402
		// There doesn't seem to be a way to call those directly, and I can't find any practical use of std::tuple_size,
		// tuple_element etc. on C++11 and C++14, so this is defined only for newer standards.
		template<std::size_t index> constexpr friend const T& get(const StaticArray<size_, T>& value) {
			return value._data[index];
		}
		template<std::size_t index> DEATH_CONSTEXPR14 friend T& get(StaticArray<size_, T>& value) {
			return value._data[index];
		}
		template<std::size_t index> DEATH_CONSTEXPR14 friend T&& get(StaticArray<size_, T>&& value) {
			return Death::move(value._data[index]);
		}
#endif
	};

	/** @relatesalso StaticArray
		@brief Make a view on a @ref StaticArray

		Convenience alternative to converting to an @ref ArrayView explicitly.
	*/
	template<std::size_t size, class T> constexpr ArrayView<T> arrayView(StaticArray<size, T>& array) {
		return ArrayView<T>{array};
	}

	/** @relatesalso StaticArray
		@brief Make a view on a const @ref StaticArray

		Convenience alternative to converting to an @ref ArrayView explicitly.
	*/
	template<std::size_t size, class T> constexpr ArrayView<const T> arrayView(const StaticArray<size, T>& array) {
		return ArrayView<const T>{array};
	}

	/** @relatesalso StaticArray
		@brief Make a static view on a @ref StaticArray

		Convenience alternative to converting to an @ref StaticArrayView explicitly.
	*/
	template<std::size_t size, class T> constexpr StaticArrayView<size, T> staticArrayView(StaticArray<size, T>& array) {
		return StaticArrayView<size, T>{array};
	}

	/** @relatesalso StaticArray
		@brief Make a static view on a const @ref StaticArray

		Convenience alternative to converting to an @ref StaticArrayView explicitly.
	*/
	template<std::size_t size, class T> constexpr StaticArrayView<size, const T> staticArrayView(const StaticArray<size, T>& array) {
		return StaticArrayView<size, const T>{array};
	}

	/** @relatesalso StaticArray
		@brief Reinterpret-cast a static array

		See @ref arrayCast(StaticArrayView<size, T>) for more information.
	*/
	template<class U, std::size_t size, class T> StaticArrayView<size * sizeof(T) / sizeof(U), U> arrayCast(StaticArray<size, T>& array) {
		return arrayCast<U>(staticArrayView(array));
	}

	/** @overload */
	template<class U, std::size_t size, class T> StaticArrayView<size * sizeof(T) / sizeof(U), const U> arrayCast(const StaticArray<size, T>& array) {
		return arrayCast<const U>(staticArrayView(array));
	}

	/** @relatesalso StaticArray
		@brief Static array size

		See @ref arraySize(ArrayView<T>) for more information.
	*/
	template<std::size_t size_, class T> constexpr std::size_t arraySize(const StaticArray<size_, T>&) {
		return size_;
	}

	template<std::size_t size_, class T> template<class ...Args> StaticArray<size_, T>::StaticArray(DirectInitT, Args&&... args) : StaticArray{NoInit} {
		for (T& i : this->_data) {
			Implementation::construct(i, Death::forward<Args>(args)...);
		}
	}

	namespace Implementation
	{
		template<std::size_t size_, class T> StaticArrayData<size_, T, false>::StaticArrayData(const StaticArrayData<size_, T, false>& other) noexcept(std::is_nothrow_copy_constructible<T>::value) : StaticArrayData{NoInit} {
			for (std::size_t i = 0; i != size_; ++i)
				/* Can't use {}, see the GCC 4.8-specific overload for details */
#if defined(DEATH_TARGET_GCC) && !defined(DEATH_TARGET_CLANG) && __GNUC__ < 5
				Implementation::construct(_data[i], other._data[i]);
#else
				new(_data + i) T{other._data[i]};
#endif
		}

		template<std::size_t size_, class T> StaticArrayData<size_, T, false>::StaticArrayData(StaticArrayData<size_, T, false>&& other) noexcept(std::is_nothrow_move_constructible<T>::value) : StaticArrayData{NoInit} {
			for (std::size_t i = 0; i != size_; ++i)
				/* Can't use {}, see the GCC 4.8-specific overload for details */
#if defined(DEATH_TARGET_GCC) && !defined(DEATH_TARGET_CLANG) && __GNUC__ < 5
				Implementation::construct(_data[i], Death::move(other._data[i]));
#else
				new(&_data[i]) T{Death::move(other._data[i])};
#endif
		}

		template<std::size_t size_, class T> StaticArrayData<size_, T, false>::~StaticArrayData() {
			for (T& i : _data) {
				i.~T();
#if defined(DEATH_MSVC2015_COMPATIBILITY)
				/* Complains i is set but not used for trivially destructible types */
				static_cast<void>(i);
#endif
			}
		}

		template<std::size_t size_, class T> StaticArrayData<size_, T, false>& StaticArrayData<size_, T, false>::operator=(const StaticArrayData<size_, T, false>& other) noexcept(std::is_nothrow_copy_constructible<T>::value) {
			for (std::size_t i = 0; i != size_; ++i) {
				_data[i] = other._data[i];
			}
			return *this;
		}

		template<std::size_t size_, class T> StaticArrayData<size_, T, false>& StaticArrayData<size_, T, false>::operator=(StaticArrayData<size_, T, false>&& other) noexcept(std::is_nothrow_move_constructible<T>::value) {
			using Death::swap;
			for (std::size_t i = 0; i != size_; ++i) {
				swap(_data[i], other._data[i]);
			}
			return *this;
		}
	}

#ifndef DOXYGEN_GENERATING_OUTPUT
	template<std::size_t size_, class T> template<class U, typename std::enable_if<std::is_convertible<U, std::size_t>::value, int>::type> constexpr const T& StaticArray<size_, T>::operator[](const U i) const {
		return DEATH_DEBUG_CONSTEXPR_ASSERT(std::size_t(i) < size_,
			("Index {} out of range for {} elements", std::size_t(i), size_)), this->_data[i];
	}

	template<std::size_t size_, class T> template<class U, typename std::enable_if<std::is_convertible<U, std::size_t>::value, int>::type> T& StaticArray<size_, T>::operator[](const U i) {
		return const_cast<T&>(static_cast<const StaticArray<size_, T>&>(*this)[i]);
	}
#endif

	template<std::size_t size_, class T> template<std::size_t size__> StaticArrayView<size__, T> StaticArray<size_, T>::prefix() {
		static_assert(size__ <= size_, "Prefix size too large");
		return StaticArrayView<size__, T>{this->_data};
	}

	template<std::size_t size_, class T> template<std::size_t size__> constexpr StaticArrayView<size__, const T> StaticArray<size_, T>::prefix() const {
		static_assert(size__ <= size_, "Prefix size too large");
		return StaticArrayView<size__, const T>{this->_data};
	}

	namespace Implementation
	{
		template<class U, std::size_t size, class T> struct ArrayViewConverter<U, StaticArray<size, T>> {
			template<class V = U, typename std::enable_if<std::is_convertible<T*, V*>::value, int>::type = 0> constexpr static ArrayView<U> from(StaticArray<size, T>& other) {
				static_assert(sizeof(T) == sizeof(U), "Types are not compatible");
				return { other.data(), other.size() };
			}
		};
		template<class U, std::size_t size, class T> struct ArrayViewConverter<const U, StaticArray<size, T>> {
			template<class V = U, typename std::enable_if<std::is_convertible<T*, V*>::value, int>::type = 0> constexpr static ArrayView<const U> from(const StaticArray<size, T>& other) {
				static_assert(sizeof(T) == sizeof(U), "Types are not compatible");
				return { other.data(), other.size() };
			}
		};
		template<class U, std::size_t size, class T> struct ArrayViewConverter<const U, StaticArray<size, const T>> {
			template<class V = U, typename std::enable_if<std::is_convertible<T*, V*>::value, int>::type = 0> constexpr static ArrayView<const U> from(const StaticArray<size, const T>& other) {
				static_assert(sizeof(T) == sizeof(U), "Types are not compatible");
				return { other.data(), other.size() };
			}
		};
		template<std::size_t size, class T> struct ErasedArrayViewConverter<StaticArray<size, T>> : ArrayViewConverter<T, StaticArray<size, T>> {};
		template<std::size_t size, class T> struct ErasedArrayViewConverter<const StaticArray<size, T>> : ArrayViewConverter<const T, StaticArray<size, T>> {};

		template<class U, std::size_t size, class T> struct StaticArrayViewConverter<size, U, StaticArray<size, T>> {
			template<class V = U, typename std::enable_if<std::is_convertible<T*, V*>::value, int>::type = 0> constexpr static StaticArrayView<size, U> from(StaticArray<size, T>& other) {
				static_assert(sizeof(T) == sizeof(U), "Types are not compatible");
				return StaticArrayView<size, T>{other.data()};
			}
		};
		template<class U, std::size_t size, class T> struct StaticArrayViewConverter<size, const U, StaticArray<size, T>> {
			template<class V = U, typename std::enable_if<std::is_convertible<T*, V*>::value, int>::type = 0> constexpr static StaticArrayView<size, const U> from(const StaticArray<size, T>& other) {
				static_assert(sizeof(T) == sizeof(U), "Types are not compatible");
				return StaticArrayView<size, const T>(other.data());
			}
		};
		template<class U, std::size_t size, class T> struct StaticArrayViewConverter<size, const U, StaticArray<size, const T>> {
			template<class V = U, typename std::enable_if<std::is_convertible<T*, V*>::value, int>::type = 0> constexpr static StaticArrayView<size, const U> from(const StaticArray<size, const T>& other) {
				static_assert(sizeof(T) == sizeof(U), "Types are not compatible");
				return StaticArrayView<size, const T>(other.data());
			}
		};
		template<std::size_t size, class T> struct ErasedStaticArrayViewConverter<StaticArray<size, T>> : StaticArrayViewConverter<size, T, StaticArray<size, T>> {};
		template<std::size_t size, class T> struct ErasedStaticArrayViewConverter<const StaticArray<size, T>> : StaticArrayViewConverter<size, const T, StaticArray<size, T>> {};
	}
}}

/* C++17 structured bindings */
#if DEATH_CXX_STANDARD > 201402
namespace std
{
	/* Note that `size` can't be used as it may conflict with std::size() in C++17 */
	template<size_t size_, class T> struct tuple_size<Death::Containers::StaticArray<size_, T>> : integral_constant<size_t, size_> {};
	template<size_t index, size_t size_, class T> struct tuple_element<index, Death::Containers::StaticArray<size_, T>> { typedef T type; };
}
#endif