#include "FileSystem.h"
#include "FileStream.h"
#include "MemoryStream.h"
#include "../CommonWindows.h"
#include "../Asserts.h"
#include "../Environment.h"
#include "../Utf8.h"
#include "../Containers/DateTime.h"
#include "../Containers/SmallVector.h"
#include "../Containers/StringConcatenable.h"

#if defined(DEATH_TARGET_WINDOWS)
#	include <fileapi.h>
#	include <shellapi.h>
#	include <shlobj.h>
#	if defined(DEATH_TARGET_WINDOWS_RT)
#		include <winrt/Windows.Foundation.h>
#		include <winrt/Windows.Storage.h>
#		include <winrt/Windows.System.h>
#	endif
#else
#	include <cerrno>
#	include <cstdio>
#	include <cstring>
#	include <ctime>
#	include <unistd.h>
#	include <sys/stat.h>
#	include <libgen.h>
#	include <pwd.h>
#	include <dirent.h>
#	include <fcntl.h>
#	include <ftw.h>
#	if defined(DEATH_TARGET_ANDROID) || defined(DEATH_TARGET_APPLE) || defined(DEATH_TARGET_UNIX)
#		include <sys/mman.h>
#	endif
#	if defined(DEATH_TARGET_ANDROID)
#		include "AndroidAssetStream.h"
#	endif
#	if defined(DEATH_TARGET_SWITCH)
#		include <alloca.h>
#	endif
#	if defined(DEATH_TARGET_UNIX)
#		include <sys/wait.h>
#	endif
#	if defined(DEATH_TARGET_APPLE)
#		include <copyfile.h>
#		include <objc/objc-runtime.h>
#		include <mach-o/dyld.h>
#	elif defined(DEATH_TARGET_EMSCRIPTEN)
#		include <emscripten/emscripten.h>
#	elif defined(__linux__)
#		include <sys/sendfile.h>
#	endif
#	if defined(__FreeBSD__)
#		include <sys/types.h>
#		include <sys/sysctl.h>
#	endif
#endif

using namespace Death::Containers;
using namespace Death::Containers::Literals;

namespace Death { namespace IO {
//###==##====#=====--==~--~=~- --- -- -  -  -   -

#if defined(DEATH_TARGET_WINDOWS)
	const char* __GetWin32ErrorSuffix(DWORD error);
#else
	const char* __GetUnixErrorSuffix(std::int32_t error);
#endif

	namespace
	{
		static std::size_t GetPathRootLength(StringView path)
		{
			if (path.empty()) return 0;

#if defined(DEATH_TARGET_WINDOWS)
			constexpr StringView ExtendedPathPrefix = "\\\\?\\"_s;
			constexpr StringView UncExtendedPathPrefix = "\\\\?\\UNC\\"_s;

			std::size_t i = 0;
			std::size_t pathLength = path.size();
			std::size_t volumeSeparatorLength = 2;		// Length to the colon "C:"
			std::size_t uncRootLength = 2;				// Length to the start of the server name "\\"

			bool extendedSyntax = path.hasPrefix(ExtendedPathPrefix);
			bool extendedUncSyntax = path.hasPrefix(UncExtendedPathPrefix);
			if (extendedSyntax) {
				// Shift the position we look for the root from to account for the extended prefix
				if (extendedUncSyntax) {
					// "\\" -> "\\?\UNC\"
					uncRootLength = UncExtendedPathPrefix.size();
				} else {
					// "C:" -> "\\?\C:"
					volumeSeparatorLength += ExtendedPathPrefix.size();
				}
			}

			if ((!extendedSyntax || extendedUncSyntax) && (path[0] == '/' || path[0] == '\\')) {
				// UNC or simple rooted path (e.g., "\foo", NOT "\\?\C:\foo")
				i = 1;
				if (extendedUncSyntax || (pathLength > 1 && (path[1] == '/' || path[1] == '\\'))) {
					// UNC ("\\?\UNC\" or "\\"), scan past the next two directory separators at most (e.g., "\\?\UNC\Server\Share" or "\\Server\Share\")
					i = uncRootLength;
					std::int32_t n = 2;	// Maximum separators to skip
					while (i < pathLength && ((path[i] != '/' && path[i] != '\\') || --n > 0)) {
						i++;
					}
					// Keep the last path separator as part of root prefix
					if (i < pathLength && (path[i] == '/' || path[i] == '\\')) {
						i++;
					}
				}
			} else if (pathLength >= volumeSeparatorLength && path[volumeSeparatorLength - 1] == ':') {
				// Path is at least longer than where we expect a colon and has a colon ("\\?\A:", "A:")
				// If the colon is followed by a directory separator, move past it
				i = volumeSeparatorLength;
				if (pathLength >= volumeSeparatorLength + 1 && (path[volumeSeparatorLength] == '/' || path[volumeSeparatorLength] == '\\')) {
					i++;
				}
			}
			
			return i;
#else
#	if defined(DEATH_TARGET_ANDROID)
			if (path.hasPrefix(AndroidAssetStream::Prefix)) {
				return AndroidAssetStream::Prefix.size();
			}
#	elif defined(DEATH_TARGET_SWITCH)
			constexpr StringView RomfsPrefix = "romfs:/"_s;
			constexpr StringView SdmcPrefix = "sdmc:/"_s;
			if (path.hasPrefix(RomfsPrefix)) {
				return RomfsPrefix.size();
			}
			if (path.hasPrefix(SdmcPrefix)) {
				return SdmcPrefix.size();
			}
#	endif
			return (path[0] == '/' || path[0] == '\\' ? 1 : 0);
#endif
		}

#if defined(DEATH_TARGET_WINDOWS)
		static bool DeleteDirectoryInternal(ArrayView<wchar_t> path, bool recursive, std::int32_t depth)
		{
			if (recursive) {
				if (path.size() + 3 <= FileSystem::MaxPathLength) {
					Array<wchar_t> bufferExtended{NoInit, FileSystem::MaxPathLength};
					std::memcpy(bufferExtended.data(), path.data(), path.size() * sizeof(wchar_t));

					std::size_t bufferOffset = path.size();
					if (bufferExtended[bufferOffset - 1] == L'/' || path[bufferOffset - 1] == L'\\') {
						bufferExtended[bufferOffset - 1] = L'\\';
					} else {
						bufferExtended[bufferOffset] = L'\\';
						bufferOffset++;
					}

					// Adding a wildcard to list all files in the directory
					bufferExtended[bufferOffset] = L'*';
					bufferExtended[bufferOffset + 1] = L'\0';

					WIN32_FIND_DATA data;
#	if defined(DEATH_TARGET_WINDOWS_RT)
					HANDLE hFindFile = ::FindFirstFileExFromAppW(bufferExtended, FindExInfoBasic, &data, FindExSearchNameMatch, nullptr, FIND_FIRST_EX_LARGE_FETCH);
#	else
					HANDLE hFindFile = ::FindFirstFileExW(bufferExtended, Environment::IsWindows7() ? FindExInfoBasic : FindExInfoStandard,
						&data, FindExSearchNameMatch, nullptr, Environment::IsWindows7() ? FIND_FIRST_EX_LARGE_FETCH : 0);
#	endif
					if (hFindFile != NULL && hFindFile != INVALID_HANDLE_VALUE) {
						do {
							if (data.cFileName[0] == L'.' && (data.cFileName[1] == L'\0' || (data.cFileName[1] == L'.' && data.cFileName[2] == L'\0'))) {
								continue;
							}

							std::size_t fileNameLength = wcslen(data.cFileName);
							std::memcpy(&bufferExtended[bufferOffset], data.cFileName, fileNameLength * sizeof(wchar_t));

							if ((data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) == FILE_ATTRIBUTE_DIRECTORY) {
								// Don't follow symbolic links
								bool shouldRecurse = ((data.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) != FILE_ATTRIBUTE_REPARSE_POINT);
								if (shouldRecurse) {
									bufferExtended[bufferOffset + fileNameLength] = L'\0';

									if (depth < 16 && !DeleteDirectoryInternal(bufferExtended.prefix(bufferOffset + fileNameLength), true, depth + 1)) {
										break;
									}
								} else {
									bufferExtended[bufferOffset + fileNameLength] = L'\\';
									bufferExtended[bufferOffset + fileNameLength + 1] = L'\0';

									// Check to see if this is a mount point and unmount it
									if (data.dwReserved0 == IO_REPARSE_TAG_MOUNT_POINT) {
										// Use full path plus a trailing '\'
										if (!::DeleteVolumeMountPointW(bufferExtended)) {
											// Cannot unmount this mount point
										}
									}

									// RemoveDirectory() on a symbolic link will remove the link itself
#	if defined(DEATH_TARGET_WINDOWS_RT)
									if (!::RemoveDirectoryFromAppW(bufferExtended)) {
#	else
									if (!::RemoveDirectoryW(bufferExtended)) {
#	endif
										DWORD error = ::GetLastError();
										if (error != ERROR_PATH_NOT_FOUND) {
											// Cannot remove symbolic link
										}
									}
								}
							} else {
								bufferExtended[bufferOffset + fileNameLength] = L'\0';

#	if defined(DEATH_TARGET_WINDOWS_RT)
								::DeleteFileFromAppW(bufferExtended);
#	else
								::DeleteFileW(bufferExtended);
#	endif
							}
						} while (::FindNextFileW(hFindFile, &data));

						::FindClose(hFindFile);
					}
				}
			}

#	if defined(DEATH_TARGET_WINDOWS_RT)
			return ::RemoveDirectoryFromAppW(path);
#	else
			return ::RemoveDirectoryW(path);
#	endif
		}
#else
		static FileSystem::Permission NativeModeToEnum(std::uint32_t nativeMode)
		{
			FileSystem::Permission mode = FileSystem::Permission::None;

			if (nativeMode & S_IRUSR)
				mode |= FileSystem::Permission::Read;
			if (nativeMode & S_IWUSR)
				mode |= FileSystem::Permission::Write;
			if (nativeMode & S_IXUSR)
				mode |= FileSystem::Permission::Execute;

			return mode;
		}

		static std::uint32_t AddPermissionsToCurrent(std::uint32_t currentMode, FileSystem::Permission mode)
		{
			if ((mode & FileSystem::Permission::Read) == FileSystem::Permission::Read)
				currentMode |= S_IRUSR;
			if ((mode & FileSystem::Permission::Write) == FileSystem::Permission::Write)
				currentMode |= S_IWUSR;
			if ((mode & FileSystem::Permission::Execute) == FileSystem::Permission::Execute)
				currentMode |= S_IXUSR;

			return currentMode;
		}

		static std::uint32_t RemovePermissionsFromCurrent(std::uint32_t currentMode, FileSystem::Permission mode)
		{
			if ((mode & FileSystem::Permission::Read) == FileSystem::Permission::Read)
				currentMode &= ~S_IRUSR;
			if ((mode & FileSystem::Permission::Write) == FileSystem::Permission::Write)
				currentMode &= ~S_IWUSR;
			if ((mode & FileSystem::Permission::Execute) == FileSystem::Permission::Execute)
				currentMode &= ~S_IXUSR;

			return currentMode;
		}

#	if !defined(DEATH_TARGET_SWITCH)
		static std::int32_t DeleteDirectoryInternalCallback(const char* fpath, const struct stat* sb, std::int32_t typeflag, struct FTW* ftwbuf)
		{
			return ::remove(fpath);
		}
#	endif

		static bool DeleteDirectoryInternal(StringView path)
		{
#	if defined(DEATH_TARGET_SWITCH)
			// nftw() is missing in libnx
			auto nullTerminatedPath = String::nullTerminatedView(path);
			DIR* d = ::opendir(nullTerminatedPath.data());
			std::int32_t r = -1;
			if (d != nullptr) {
				r = 0;
				struct dirent* p;
				while (r == 0 && (p = ::readdir(d)) != nullptr) {
					if (strcmp(p->d_name, ".") == 0 || strcmp(p->d_name, "..") == 0) {
						continue;
					}

					String fileName = FileSystem::CombinePath(path, p->d_name);
					struct stat sb;
					if (::lstat(fileName.data(), &sb) == 0) { // Don't follow symbolic links
						if (S_ISDIR(sb.st_mode)) {
							DeleteDirectoryInternal(fileName);
						} else {
							r = ::unlink(fileName.data());
						}
					}
				}
				::closedir(d);
			}

			if (r == 0) {
				r = ::rmdir(nullTerminatedPath.data());
			}
			return (r == 0);
#	else
			// Don't follow symbolic links
			return ::nftw(String::nullTerminatedView(path).data(), DeleteDirectoryInternalCallback, 64, FTW_DEPTH | FTW_PHYS) == 0;
#	endif
		}

#	if defined(DEATH_TARGET_UNIX)
		static bool RedirectFileDescriptorToNull(std::int32_t fd)
		{
			if (fd < 0) {
				return false;
			}

			std::int32_t tempfd;
			do {
				tempfd = ::open("/dev/null", O_RDWR | O_NOCTTY);
			} while (tempfd == -1 && errno == EINTR);

			if (tempfd == -1) {
				return false;
			}

			if (tempfd != fd) {
				if (::dup2(tempfd, fd) == -1) {
					::close(tempfd);
					return false;
				}
				if (::close(tempfd) == -1) {
					return false;
				}
			}

			return true;
		}

		static void TryCloseAllFileDescriptors()
		{
			DIR* d = ::opendir("/proc/self/fd/");
			if (d == nullptr) {
				const long fd_max = ::sysconf(_SC_OPEN_MAX);
				long fd;
				for (fd = 0; fd <= fd_max; fd++) {
					::close(fd);
				}
				return;
			}

			std::int32_t dfd = ::dirfd(d);
			struct dirent* ent;
			while ((ent = ::readdir(d)) != nullptr) {
				if (ent->d_name[0] >= '0' && ent->d_name[0] <= '9') {
					const char* p = &ent->d_name[1];
					std::int32_t fd = ent->d_name[0] - '0';
					while (*p >= '0' && *p <= '9') {
						fd = (10 * fd) + *(p++) - '0';
					}
					if (*p || fd == dfd) {
						continue;
					}
					::close(fd);
				}
			}
			::closedir(d);
		}
#	endif
#endif

#if defined(DEATH_TARGET_EMSCRIPTEN)
		EM_JS(int, __asyncjs__MountAsPersistent, (const char* path, int pathLength), {
			return Asyncify.handleSleep(function(callback) {
				var p = UTF8ToString(path, pathLength);

				FS.mkdir(p);
				FS.mount(IDBFS, {}, p);

				FS.syncfs(true, function(error) {
					callback(error ? 0 : 1);
				});
			});
		});
#endif
	}

	class FileSystem::Directory::Impl
	{
		friend class Directory;

	public:
		Impl(StringView path, EnumerationOptions options)
			: _fileNamePart(nullptr)
#if defined(DEATH_TARGET_WINDOWS)
				, _hFindFile(NULL)
#else
#	if defined(DEATH_TARGET_ANDROID)
				, _assetDir(nullptr)
#	endif
				, _dirStream(nullptr)
#endif
		{
			Open(path, options);
		}

		~Impl()
		{
			Close();
		}

		Impl(const Impl&) = delete;
		Impl& operator=(const Impl&) = delete;

		bool Open(StringView path, EnumerationOptions options)
		{
			Close();

			_options = options;
			_path[0] = '\0';
#if defined(DEATH_TARGET_WINDOWS)
			if (!path.empty() && DirectoryExists(path)) {
				// Prepare full path to found files
				{
					String absPath = GetAbsolutePath(path);
					std::size_t pathLength = absPath.size();
					std::memcpy(_path, absPath.data(), pathLength);
					if (_path[pathLength - 1] == '/' || _path[pathLength - 1] == '\\') {
						_path[pathLength - 1] = '\\';
						_path[pathLength] = '\0';
						_fileNamePart = _path + pathLength;
					} else {
						_path[pathLength] = '\\';
						_path[pathLength + 1] = '\0';
						_fileNamePart = _path + pathLength + 1;
					}
				}

				std::size_t pathLength = (_fileNamePart - _path);
				SmallVector<wchar_t, MAX_PATH + 2> pathW(DefaultInit, pathLength + 2);
				std::int32_t pathLengthW = Utf8::ToUtf16(pathW.data(), std::int32_t(pathW.size()), _path, std::int32_t(pathLength));
				if (pathLengthW + 2 <= MaxPathLength) {
					// Adding a wildcard to list all files in the directory
					pathW[pathLengthW] = L'*';
					pathW[pathLengthW + 1] = L'\0';

					WIN32_FIND_DATA data;
#	if defined(DEATH_TARGET_WINDOWS_RT)
					_hFindFile = ::FindFirstFileExFromAppW(pathW.data(), FindExInfoBasic, &data, FindExSearchNameMatch, nullptr, FIND_FIRST_EX_LARGE_FETCH);
#	else
					_hFindFile = ::FindFirstFileExW(pathW.data(), Environment::IsWindows7() ? FindExInfoBasic : FindExInfoStandard,
						&data, FindExSearchNameMatch, nullptr, Environment::IsWindows7() ? FIND_FIRST_EX_LARGE_FETCH : 0);
#	endif
					if (_hFindFile != NULL && _hFindFile != INVALID_HANDLE_VALUE) {
						if ((data.cFileName[0] == L'.' && (data.cFileName[1] == L'\0' || (data.cFileName[1] == L'.' && data.cFileName[2] == L'\0'))) ||
							((_options & EnumerationOptions::SkipDirectories) == EnumerationOptions::SkipDirectories && (data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) == FILE_ATTRIBUTE_DIRECTORY) ||
							((_options & EnumerationOptions::SkipFiles) == EnumerationOptions::SkipFiles && (data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != FILE_ATTRIBUTE_DIRECTORY)) {
							// Skip this file
							Increment();
						} else {
							Utf8::FromUtf16(_fileNamePart, std::int32_t(sizeof(_path) - (_fileNamePart - _path)), data.cFileName);
							// Write terminating NULL in case the string was longer and did not fit into the array
							_path[sizeof(_path) - 1] = '\0';
						}
					}
				}
			}
			return (_hFindFile != NULL && _hFindFile != INVALID_HANDLE_VALUE);
#else
			auto nullTerminatedPath = String::nullTerminatedView(path);
			if (!nullTerminatedPath.empty()) {
#	if defined(DEATH_TARGET_ANDROID)
				if (auto strippedPath = AndroidAssetStream::TryGetAssetPath(nullTerminatedPath)) {
					// It probably supports only files
					if ((_options & EnumerationOptions::SkipFiles) != EnumerationOptions::SkipFiles) {
						_assetDir = AndroidAssetStream::OpenDirectory(strippedPath);
						if (_assetDir != nullptr) {
							std::size_t pathLength = path.size();
							std::memcpy(_path, path.data(), pathLength);
							if (_path[pathLength - 1] == '/' || _path[pathLength - 1] == '\\') {
								_path[pathLength - 1] = '/';
								_path[pathLength] = '\0';
								_fileNamePart = _path + pathLength;
							} else {
								_path[pathLength] = '/';
								_path[pathLength + 1] = '\0';
								_fileNamePart = _path + pathLength + 1;
							}
							return true;
						}
					}
					return false;
				}
#	endif
				_dirStream = ::opendir(nullTerminatedPath.data());
				if (_dirStream != nullptr) {
					String absPath = GetAbsolutePath(path);
					std::size_t pathLength = absPath.size();
					std::memcpy(_path, absPath.data(), pathLength);
					if (_path[pathLength - 1] == '/' || _path[pathLength - 1] == '\\') {
						_path[pathLength - 1] = '/';
						_path[pathLength] = '\0';
						_fileNamePart = _path + pathLength;
					} else {
						_path[pathLength] = '/';
						_path[pathLength + 1] = '\0';
						_fileNamePart = _path + pathLength + 1;
					}
					return true;
				}
			}
			return false;
#endif
		}

		void Close()
		{
#if defined(DEATH_TARGET_WINDOWS)
			if (_hFindFile != NULL && _hFindFile != INVALID_HANDLE_VALUE) {
				::FindClose(_hFindFile);
				_hFindFile = NULL;
			}
#else
#	if defined(DEATH_TARGET_ANDROID)
			if (_assetDir != nullptr) {
				AndroidAssetStream::CloseDirectory(_assetDir);
				_assetDir = nullptr;
			} else
#	endif
			if (_dirStream != nullptr) {
				::closedir(_dirStream);
				_dirStream = nullptr;
			}
#endif
		}

		void Increment()
		{
#if defined(DEATH_TARGET_WINDOWS)
			if (_hFindFile == NULL || _hFindFile == INVALID_HANDLE_VALUE) {
				_path[0] = '\0';
				return;
			}

		Retry:
			WIN32_FIND_DATA data;
			if (::FindNextFileW(_hFindFile, &data)) {
				if ((data.cFileName[0] == L'.' && (data.cFileName[1] == L'\0' || (data.cFileName[1] == L'.' && data.cFileName[2] == L'\0'))) ||
					((_options & EnumerationOptions::SkipDirectories) == EnumerationOptions::SkipDirectories && (data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) == FILE_ATTRIBUTE_DIRECTORY) ||
					((_options & EnumerationOptions::SkipFiles) == EnumerationOptions::SkipFiles && (data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != FILE_ATTRIBUTE_DIRECTORY)) {
					goto Retry;
				} else {
					Utf8::FromUtf16(_fileNamePart, std::int32_t(sizeof(_path) - (_fileNamePart - _path)), data.cFileName);
					// Write terminating NULL in case the string was longer and did not fit into the array
					_path[sizeof(_path) - 1] = '\0';
				}
			} else {
				_path[0] = '\0';
			}
#else
#	if defined(DEATH_TARGET_ANDROID)
			// It does not return directory names
			if (_assetDir != nullptr) {
				const char* assetName = AndroidAssetStream::GetNextFileName(_assetDir);
				if (assetName == nullptr) {
					_path[0] = '\0';
					return;
				}
				strcpy(_fileNamePart, assetName);
				return;
			}
#	endif
			if (_dirStream == nullptr) {
				_path[0] = '\0';
				return;
			}

			struct dirent* entry;
		Retry:
			entry = ::readdir(_dirStream);
			if (entry != nullptr) {
				if (entry->d_name[0] == L'.' && (entry->d_name[1] == L'\0' || (entry->d_name[1] == L'.' && entry->d_name[2] == L'\0'))) {
					goto Retry;
				}

				if ((_options & EnumerationOptions::SkipDirectories) == EnumerationOptions::SkipDirectories && entry->d_type == DT_DIR)
					goto Retry;
#	if !defined(DEATH_TARGET_EMSCRIPTEN)
				if ((_options & EnumerationOptions::SkipFiles) == EnumerationOptions::SkipFiles && entry->d_type == DT_REG)
					goto Retry;
				if ((_options & EnumerationOptions::SkipSpecial) == EnumerationOptions::SkipSpecial && entry->d_type != DT_DIR && entry->d_type != DT_REG && entry->d_type != DT_LNK)
					goto Retry;
#	else
				// Emscripten doesn't set DT_REG for files, so we treat everything that's not a DT_DIR as a file. SkipSpecial has no effect here.
				if ((_options & EnumerationOptions::SkipFiles) == EnumerationOptions::SkipFiles && entry->d_type != DT_DIR)
					goto Retry;
#	endif
				std::size_t charsLeft = sizeof(_path) - (_fileNamePart - _path) - 1;
#	if defined(__FreeBSD__)
				std::size_t fileNameLength = entry->d_namlen;
#	else
				std::size_t fileNameLength = std::strlen(entry->d_name);
#	endif
				if (fileNameLength > charsLeft) {
					// Path is too long, skip this file
					goto Retry;
				}
				std::memcpy(_fileNamePart, entry->d_name, fileNameLength);
				_fileNamePart[fileNameLength] = '\0';
			} else {
				_path[0] = '\0';
			}
#endif
		}

	private:
		EnumerationOptions _options;
		char _path[MaxPathLength];
		char* _fileNamePart;
#if defined(DEATH_TARGET_WINDOWS)
		void* _hFindFile;
#else
#	if defined(DEATH_TARGET_ANDROID)
		AAssetDir* _assetDir;
#	endif
		DIR* _dirStream;
#endif
	};

	class FileSystem::Directory::Proxy
	{
		friend class Directory;

	public:
		Containers::StringView operator*() const & noexcept;

	private:
		explicit Proxy(Containers::StringView path);

		Containers::String _path;
	};

	FileSystem::Directory::Directory() noexcept
	{
	}

	FileSystem::Directory::Directory(StringView path, EnumerationOptions options)
		: _impl(std::make_shared<Impl>(path, options))
	{
	}

	FileSystem::Directory::~Directory()
	{
	}

	FileSystem::Directory::Directory(const Directory& other)
		: _impl(other._impl)
	{
	}

	FileSystem::Directory::Directory(Directory&& other) noexcept
		: _impl(Death::move(other._impl))
	{
	}

	FileSystem::Directory& FileSystem::Directory::operator=(const Directory& other)
	{
		_impl = other._impl;
		return *this;
	}

	FileSystem::Directory& FileSystem::Directory::operator=(Directory&& other) noexcept
	{
		_impl = Death::move(other._impl);
		return *this;
	}

	StringView FileSystem::Directory::operator*() const & noexcept
	{
		return _impl->_path;
	}

	FileSystem::Directory& FileSystem::Directory::operator++()
	{
		_impl->Increment();
		return *this;
	}

	FileSystem::Directory::Proxy FileSystem::Directory::operator++(int)
	{
		Proxy p{**this};
		++*this;
		return p;
	}

	bool FileSystem::Directory::operator==(const Directory& other) const
	{
		bool isEnd1 = (_impl == nullptr || _impl->_path[0] == '\0');
		bool isEnd2 = (other._impl == nullptr || other._impl->_path[0] == '\0');
		if (isEnd1 || isEnd2) {
			return (isEnd1 && isEnd2);
		}
		return (_impl == other._impl);
	}

	bool FileSystem::Directory::operator!=(const Directory& other) const
	{
		return !(*this == other);
	}

	FileSystem::Directory::Proxy::Proxy(StringView path)
		: _path(path)
	{
	}

	StringView FileSystem::Directory::Proxy::operator*() const & noexcept
	{
		return _path;
	}

#if !defined(DEATH_TARGET_WINDOWS) && !defined(DEATH_TARGET_SWITCH)
	String FileSystem::FindPathCaseInsensitive(StringView path)
	{
		if (path.empty() || Exists(path)) {
			return path;
		}

		DIR* d = nullptr;
		String result = path;
		MutableStringView partialResult = result;
		char* nextPartBegin;

		while (MutableStringView separator = partialResult.findLast('/')) {
			if DEATH_UNLIKELY(separator.begin() == result.begin()) {
				// Nothing left, only first slash of absolute path
				break;
			}

			partialResult = partialResult.prefix(separator.begin());
			separator[0] = '\0';
			d = ::opendir(result.data());
			separator[0] = '/';
			if (d != nullptr) {
				nextPartBegin = separator.end();
				break;
			}
		}

		if (d == nullptr) {
			if (result[0] == '/' || result[0] == '\\') {
				d = ::opendir("/");
				nextPartBegin = result.begin() + 1;
			} else {
				d = ::opendir(".");
				nextPartBegin = result.begin();
			}

			if DEATH_UNLIKELY(d == nullptr) {
				return {};
			}
		}

		while (true) {
			partialResult = result.suffix(nextPartBegin);
			MutableStringView nextSeparator = partialResult.findOr('/', result.end());
			if DEATH_UNLIKELY(nextSeparator.begin() == nextPartBegin) {
				// Skip empty parts
				nextPartBegin = nextSeparator.end();
				continue;
			}

			bool hasNextSeparator = (nextSeparator.begin() != result.end());
			if DEATH_LIKELY(hasNextSeparator) {
				nextSeparator[0] = '\0';
			}

			struct dirent* entry = ::readdir(d);
			while (entry != nullptr) {
				if (::strcasecmp(partialResult.begin(), entry->d_name) == 0) {
#	if defined(__FreeBSD__)
					std::size_t fileNameLength = entry->d_namlen;
#	else
					std::size_t fileNameLength = std::strlen(entry->d_name);
#	endif
					DEATH_DEBUG_ASSERT(partialResult.begin() + fileNameLength == nextSeparator.begin());
					std::memcpy(partialResult.begin(), entry->d_name, fileNameLength);
					::closedir(d);

					nextPartBegin = nextSeparator.end();
					if (!hasNextSeparator || nextPartBegin == result.end()) {
						if (hasNextSeparator) {
							nextSeparator[0] = '/';
						}
						return result;
					}

					d = ::opendir(result.data());
					if DEATH_UNLIKELY(d == nullptr) {
						return {};
					}
					nextSeparator[0] = '/';
					break;
				}

				entry = ::readdir(d);
			}

			if DEATH_UNLIKELY(entry == nullptr) {
				::closedir(d);
				return {};
			}
		}
	}
#endif

	String FileSystem::CombinePath(StringView first, StringView second)
	{
		std::size_t firstSize = first.size();
		std::size_t secondSize = second.size();

		if (secondSize == 0) {
			return first;
		}
		if (firstSize == 0 || GetPathRootLength(second) > 0) {
			return second;
		}

#	if defined(DEATH_TARGET_ANDROID)
		if (first == AndroidAssetStream::Prefix) {
			return first + second;
		}
#	endif

		if (first[firstSize - 1] == '/' || first[firstSize - 1] == '\\') {
			// Path has trailing separator
			return first + second;
		} else {
			// Both paths have no clashing separators
#if defined(DEATH_TARGET_WINDOWS)
			return "\\"_s.join({ first, second });
#else
			return "/"_s.join({ first, second });
#endif
		}
	}

	String FileSystem::CombinePath(ArrayView<const StringView> paths)
	{
		if (paths.empty()) return {};

		std::size_t count = paths.size();
		std::size_t resultSize = 0;
		std::size_t startIdx = 0;
		for (std::size_t i = 0; i < count; i++) {
			std::size_t pathSize = paths[i].size();
			if (pathSize == 0) {
				continue;
			}
			if (GetPathRootLength(paths[i]) > 0) {
				resultSize = 0;
				startIdx = i;
			}

			resultSize += pathSize;

			if (i + 1 < count && paths[i][pathSize - 1] != '/' && paths[i][pathSize - 1] != '\\') {
				resultSize++;
			}
		}

		String result{NoInit, resultSize};
		resultSize = 0;
		for (std::size_t i = startIdx; i < count; i++) {
			std::size_t pathSize = paths[i].size();
			if (pathSize == 0) {
				continue;
			}

			std::memcpy(&result[resultSize], paths[i].data(), pathSize);
			resultSize += pathSize;

			if (i + 1 < count && paths[i][pathSize - 1] != '/' && paths[i][pathSize - 1] != '\\') {
#if defined(DEATH_TARGET_WINDOWS)
				result[resultSize] = '\\';
#else
				result[resultSize] = '/';
#endif
				resultSize++;
			}
		}

		return result;
	}

	String FileSystem::CombinePath(std::initializer_list<StringView> paths)
	{
		return CombinePath(arrayView(paths));
	}

	StringView FileSystem::GetDirectoryName(StringView path)
	{
		if (path.empty()) return {};

		std::size_t pathRootLength = GetPathRootLength(path);
		std::size_t i = path.size();
		// Strip any trailing path separators
		while (i > pathRootLength && (path[i - 1] == '/' || path[i - 1] == '\\')) {
			i--;
		}
		if (i <= pathRootLength) return {};
		// Try to get the last path separator
		while (i > pathRootLength && path[--i] != '/' && path[i] != '\\');

		return path.slice(0, i);
	}

	StringView FileSystem::GetFileName(StringView path)
	{
		if (path.empty()) return {};

		std::size_t pathRootLength = GetPathRootLength(path);
		std::size_t pathLength = path.size();
		// Strip any trailing path separators
		while (pathLength > pathRootLength && (path[pathLength - 1] == '/' || path[pathLength - 1] == '\\')) {
			pathLength--;
		}
		if (pathLength <= pathRootLength) return {};
		std::size_t i = pathLength;
		// Try to get the last path separator
		while (i > pathRootLength && path[--i] != '/' && path[i] != '\\');

		if (path[i] == '/' || path[i] == '\\') {
			i++;
		}
		return path.slice(i, pathLength);
	}

	StringView FileSystem::GetFileNameWithoutExtension(StringView path)
	{
		StringView fileName = GetFileName(path);
		if (fileName.empty()) return {};

		const StringView foundDot = fileName.findLastOr('.', fileName.end());
		if (foundDot.begin() == fileName.end()) return fileName;

		bool initialDots = true;
		for (char c : fileName.prefix(foundDot.begin())) {
			if (c != '.') {
				initialDots = false;
				break;
			}
		}
		if (initialDots) return fileName;
		return fileName.prefix(foundDot.begin());
	}

	String FileSystem::GetExtension(StringView path)
	{
		StringView fileName = GetFileName(path);
		if (fileName.empty()) return {};

		const StringView foundDot = fileName.findLastOr('.', fileName.end());
		if (foundDot.begin() == fileName.end()) return {};

		bool initialDots = true;
		for (char c : fileName.prefix(foundDot.begin())) {
			if (c != '.') {
				initialDots = false;
				break;
			}
		}
		if (initialDots) return {};
		String result = fileName.suffix(foundDot.end());

		// Convert to lower-case
		for (char& c : result) {
			if (c >= 'A' && c <= 'Z') {
				c |= 0x20;
			}
		}

		return result;
	}

#if defined(DEATH_TARGET_WINDOWS)
	String FileSystem::FromNativeSeparators(String path)
	{
		// Take ownership first if not already (e.g., directly from `String::nullTerminatedView()`)
		if (!path.isSmall() && path.deleter()) {
			path = String{path};
		}

		for (char& c : path) {
			if (c == '\\') {
				c = '/';
			}
		}

		return path;
	}

	String FileSystem::ToNativeSeparators(String path)
	{
		// Take ownership first if not already (e.g., directly from `String::nullTerminatedView()`)
		if (!path.isSmall() && path.deleter()) {
			path = String{path};
		}

		for (char& c : path) {
			if (c == '/') {
				c = '\\';
			}
		}

		return path;
	}
#endif

	String FileSystem::GetAbsolutePath(StringView path)
	{
		if (path.empty()) return {};

#if defined(DEATH_TARGET_WINDOWS)
		SmallVector<wchar_t, MAX_PATH + 1> pathW(DefaultInit, path.size() + 1);
		Utf8::ToUtf16(pathW.data(), std::int32_t(pathW.size()), path.data(), std::int32_t(path.size()));

		wchar_t buffer[MaxPathLength + 1];
		DWORD length = ::GetFullPathNameW(pathW.data(), DWORD(arraySize(buffer)), buffer, nullptr);
		if (length == 0) {
			return {};
		}

		return Utf8::FromUtf16(buffer, length);
#elif defined(DEATH_TARGET_SWITCH)
		// realpath() is missing in libnx
		char left[MaxPathLength];
		char nextToken[MaxPathLength];
		char result[MaxPathLength];
		std::size_t resultLength = 0;
#	if !defined(DEATH_TARGET_SWITCH)
		std::int32_t symlinks = 0;
#	endif

		std::size_t pathRootLength = GetPathRootLength(path);
		if (pathRootLength > 0) {
			strncpy(result, path.data(), pathRootLength);
			resultLength = pathRootLength;
			if (path.size() == pathRootLength) {
				return String{result, resultLength};
			}

			strncpy(left, path.data() + pathRootLength, sizeof(left));
		} else {
			if (::getcwd(result, sizeof(result)) == nullptr) {
				return "."_s;
			}
			resultLength = std::strlen(result);
			strncpy(left, path.data(), sizeof(left));
		}
		std::size_t leftLength = std::strlen(left);
		if (leftLength >= sizeof(left) || resultLength >= MaxPathLength) {
			// Path is too long
			return path;
		}

		while (leftLength != 0) {
			char* p = strchr(left, '/');
			char* s = (p != nullptr ? p : left + leftLength);
			std::size_t nextTokenLength = s - left;
			if (nextTokenLength >= sizeof(nextToken)) {
				// Path is too long
				return path;
			}
			std::memcpy(nextToken, left, nextTokenLength);
			nextToken[nextTokenLength] = '\0';
			leftLength -= nextTokenLength;
			if (p != nullptr) {
				std::memmove(left, s + 1, leftLength + 1);
				leftLength--;
			}
			if (result[resultLength - 1] != '/') {
				if (resultLength + 1 >= MaxPathLength) {
					return path;
				}
				result[resultLength++] = '/';
			}
			if (nextToken[0] == '\0' || strcmp(nextToken, ".") == 0) {
				continue;
			}
			if (strcmp(nextToken, "..") == 0) {
				if (resultLength > 1) {
					result[resultLength - 1] = '\0';
					char* q = strrchr(result, '/') + 1;
					resultLength = q - result;
				}
				continue;
			}

			if (resultLength + nextTokenLength >= sizeof(result)) {
				// Path is too long
				return path;
			}
			std::memcpy(result + resultLength, nextToken, nextTokenLength);
			resultLength += nextTokenLength;
			result[resultLength] = '\0';

			struct stat sb;
			if (::lstat(result, &sb) != 0) {
				if (errno == ENOENT && p == nullptr) {
					return String{result, resultLength};
				}
				return {};
			}
#	if !defined(DEATH_TARGET_SWITCH)
			// readlink() is missing in libnx
			if (S_ISLNK(sb.st_mode)) {
				if (++symlinks > 8) {
					// Too many symlinks
					return {};
				}
				ssize_t symlinkLength = ::readlink(result, nextToken, sizeof(nextToken) - 1);
				if (symlinkLength < 0) {
					// Cannot resolve the symlink
					return {};
				}
				nextToken[symlinkLength] = '\0';
				if (nextToken[0] == '/') {
					resultLength = 1;
				} else if (resultLength > 1) {
					result[resultLength - 1] = '\0';
					char* q = strrchr(result, '/') + 1;
					resultLength = q - result;
				}

				if (p != nullptr) {
					if (nextToken[symlinkLength - 1] != '/') {
						if (static_cast<std::size_t>(symlinkLength) + 1 >= sizeof(nextToken)) {
							// Path is too long
							return {};
						}
						nextToken[symlinkLength++] = '/';
					}
					strncpy(nextToken + symlinkLength, left, sizeof(nextToken) - symlinkLength);
				}
				strncpy(left, nextToken, sizeof(left));
				leftLength = std::strlen(left);
			}
#	endif
		}

		if (resultLength > 1 && result[resultLength - 1] == '/') {
			resultLength--;
		}
		return String{result, resultLength};
#else
#	if defined(DEATH_TARGET_ANDROID)
		if (path.hasPrefix(AndroidAssetStream::Prefix)) {
			return path;
		}
#	endif
		char buffer[MaxPathLength];
		const char* resolvedPath = ::realpath(String::nullTerminatedView(path).data(), buffer);
		if (resolvedPath == nullptr) {
			return {};
		}
		return buffer;
#endif
	}

	bool FileSystem::IsAbsolutePath(StringView path)
	{
		return GetPathRootLength(path) > 0;
	}

	String FileSystem::GetExecutablePath()
	{
#if defined(DEATH_TARGET_EMSCRIPTEN)
		return "/"_s;
#elif defined(DEATH_TARGET_APPLE)
		// Get path size (need to set it to 0 to avoid filling nullptr with random data and crashing)
		std::uint32_t size = 0;
		if (_NSGetExecutablePath(nullptr, &size) != -1) {
			return {};
		}

		// Allocate proper size and get the path. The size includes a null terminator which the String handles on its own, so subtract it
		String path{NoInit, size - 1};
		if (_NSGetExecutablePath(path.data(), &size) != 0) {
			return {};
		}
		return path;
#elif defined(__FreeBSD__)
		std::size_t size;
		static const std::int32_t mib[4] = { CTL_KERN, KERN_PROC, KERN_PROC_PATHNAME, -1 };
		sysctl(mib, 4, nullptr, &size, nullptr, 0);
		String path{NoInit, size};
		sysctl(mib, 4, path.data(), &size, nullptr, 0);
		return path;
#elif defined(DEATH_TARGET_UNIX)
		// Reallocate like hell until we have enough place to store the path. Can't use lstat because
		// the /proc/self/exe symlink is not a real symlink and so stat::st_size returns 0.
		static const char self[] = "/proc/self/exe";
		Array<char> path;
		arrayResize(path, NoInit, 16);
		ssize_t size;
		while ((size = ::readlink(self, path, path.size())) == ssize_t(path.size())) {
			arrayResize(path, NoInit, path.size() * 2);
		}

		if (size == -1) {
			LOGE("Cannot read \"{}\" with error {}{}", self, errno, __GetUnixErrorSuffix(errno));
			return {};
		}

		// readlink() doesn't put the null terminator into the array, do it ourselves. The above loop guarantees
		// that path.size() is always larger than size - if it would be equal, we'd try once more with a larger buffer
		path[size] = '\0';
		const auto deleter = path.deleter();
		return String{path.release(), std::size_t(size), deleter};
#elif defined(DEATH_TARGET_WINDOWS) && !defined(DEATH_TARGET_WINDOWS_RT)
		wchar_t path[MaxPathLength + 1];
		// Returns size *without* the null terminator
		const std::size_t size = ::GetModuleFileNameW(NULL, path, DWORD(arraySize(path)));
		return Utf8::FromUtf16(arrayView(path, size));
#else
		return {};
#endif
	}

	String FileSystem::GetSavePath(StringView applicationName)
	{
#if defined(DEATH_TARGET_ANDROID)
		StringView savePath = AndroidAssetStream::GetInternalDataPath();
		if (!DirectoryExists(savePath)) {
			// Trying to create the data directory
			if (!CreateDirectories(savePath)) {
				return {};
			}
		}
		return savePath;
#elif defined(DEATH_TARGET_APPLE)
		StringView home = ::getenv("HOME");
		if (home.empty()) {
			LOGE("Cannot find home directory");
			return {};
		}

		return CombinePath({ home, "Library/Application Support"_s, applicationName });
#elif defined(DEATH_TARGET_UNIX) || defined(DEATH_TARGET_EMSCRIPTEN)
		StringView config = ::getenv("XDG_CONFIG_HOME");
		if (IsAbsolutePath(config)) {
			return CombinePath(config, applicationName);
		}

		StringView home = ::getenv("HOME");
		if (home.empty()) {
			LOGE("Cannot find home directory");
			return {};
		}

		return CombinePath({ home, ".config"_s, applicationName });
#elif defined(DEATH_TARGET_WINDOWS_RT)
		auto appData = winrt::Windows::Storage::ApplicationData::Current().LocalFolder().Path();
		return Death::Utf8::FromUtf16(appData.data(), appData.size());
#elif defined(DEATH_TARGET_WINDOWS)
		String result;
		wchar_t* pathW = nullptr;
		bool success = SUCCEEDED(::SHGetKnownFolderPath(FOLDERID_SavedGames, KF_FLAG_DEFAULT, nullptr, &pathW));
		if (!success || pathW == nullptr || pathW[0] == L'\0') {
			::CoTaskMemFree(pathW);
			pathW = nullptr;
			// Fallback to "%AppData%\Roaming" if "%UserProfile%\Saved Games" cannot be found
			success = SUCCEEDED(::SHGetKnownFolderPath(FOLDERID_RoamingAppData, KF_FLAG_DEFAULT, nullptr, &pathW));
		}
		if (success && pathW != nullptr && pathW[0] != L'\0') {
			std::int32_t pathLengthW = std::int32_t(wcslen(pathW));
			SmallVector<char, MAX_PATH + 1> path(DefaultInit, pathLengthW * 4 + 1);
			std::size_t pathLength = Utf8::FromUtf16(path.data(), std::int32_t(path.size()), pathW, pathLengthW);
			result = CombinePath({ path.data(), pathLength }, applicationName);
		} else {
			LOGE("SHGetKnownFolderPath(FOLDERID_RoamingAppData) failed with error 0x{:.8x}", ::GetLastError());
		}
		::CoTaskMemFree(pathW);
		return result;
#endif
	}

	String FileSystem::GetWorkingDirectory()
	{
#if defined(DEATH_TARGET_EMSCRIPTEN)
		return "/"_s;
#elif defined(DEATH_TARGET_WINDOWS)
		SmallVector<wchar_t, MAX_PATH + 1> pathW(DefaultInit, MaxPathLength + 1);
		DWORD length = ::GetCurrentDirectoryW(DWORD(pathW.size()), pathW.data());
		if (length > pathW.size()) {
			pathW.resize_for_overwrite(length);
			DWORD length2 = ::GetCurrentDirectoryW(length, pathW.data());
			DEATH_DEBUG_ASSERT(length2 == (length - 1));
			return Utf8::FromUtf16(pathW.data(), length2);
		}
		if (length == 0) {
			DWORD error = ::GetLastError();
			LOGE("GetCurrentDirectory() failed with error 0x{:.8x}{}", error, __GetWin32ErrorSuffix(error));
			return {};
		}
		return Utf8::FromUtf16(pathW.data(), length);
#else
		char buffer[MaxPathLength];
		if (::getcwd(buffer, MaxPathLength) == nullptr) {
			LOGE("getcwd() failed with error {}{}", errno, __GetUnixErrorSuffix(errno));
			return {};
		}
		return buffer;
#endif
	}

	bool FileSystem::SetWorkingDirectory(StringView path)
	{
#if defined(DEATH_TARGET_EMSCRIPTEN)
		return false;
#elif defined(DEATH_TARGET_WINDOWS)
		SmallVector<wchar_t, MAX_PATH + 1> pathW(DefaultInit, path.size() + 1);
		Utf8::ToUtf16(pathW.data(), std::int32_t(pathW.size()), path.data(), std::int32_t(path.size()));
		return ::SetCurrentDirectoryW(pathW.data());
#else
		return (::chdir(String::nullTerminatedView(path).data()) == 0);
#endif
	}

	String FileSystem::GetHomeDirectory()
	{
#if defined(DEATH_TARGET_WINDOWS_RT)
		// This method is not supported on WinRT
		return {};
#elif defined(DEATH_TARGET_WINDOWS)
		String result;
		wchar_t* path = nullptr;
		if (SUCCEEDED(::SHGetKnownFolderPath(FOLDERID_Profile, KF_FLAG_DEFAULT, nullptr, &path))) {
			result = Utf8::FromUtf16(path);
		} else {
			LOGE("SHGetKnownFolderPath(FOLDERID_Profile) failed with error 0x{:.8x}", ::GetLastError());
		}
		::CoTaskMemFree(path);
		return result;
#else
		StringView home = ::getenv("HOME");
		if (!home.empty()) {
			return home;
		}
#	if !defined(DEATH_TARGET_EMSCRIPTEN)
		// `getpwuid()` is not yet implemented on Emscripten
		const struct passwd* pw = ::getpwuid(getuid());
		if (pw != nullptr) {
			return pw->pw_dir;
		}
#	endif
		LOGE("Cannot find home directory");
		return {};
#endif
	}

	String FileSystem::GetTempDirectory()
	{
#if defined(DEATH_TARGET_WINDOWS)
		SmallVector<wchar_t, MAX_PATH + 1> pathW(DefaultInit, MaxPathLength + 1);
		DWORD length = ::GetTempPathW(DWORD(pathW.size()), pathW.data());
		if (length > pathW.size()) {
			pathW.resize_for_overwrite(length);
			DWORD length2 = ::GetTempPathW(length, pathW.data());
			DEATH_DEBUG_ASSERT(length2 == (length - 1));
			return Utf8::FromUtf16(pathW.data(), length2);
		}
		if (length == 0) {
			DWORD error = ::GetLastError();
			LOGE("GetTempPath() failed with error 0x{:.8x}{}", error, __GetWin32ErrorSuffix(error));
			return {};
		}
		return Utf8::FromUtf16(pathW.data(), length);
#else
		StringView tmpDir = ::getenv("TMPDIR");
		if (DirectoryExists(tmpDir)) {
			return tmpDir;
		}

		tmpDir = ::getenv("TMP");
		if (DirectoryExists(tmpDir)) {
			return tmpDir;
		}

		tmpDir = ::getenv("TEMP");
		if (DirectoryExists(tmpDir)) {
			return tmpDir;
		}

		if (DirectoryExists("/tmp"_s)) {
			return "/tmp"_s;
		}

		LOGE("Cannot find temporary directory");
		return "."_s;
#endif
	}

#if defined(DEATH_TARGET_ANDROID)
	String FileSystem::GetExternalStorage()
	{
		StringView extStorage = ::getenv("EXTERNAL_STORAGE");
		if (!extStorage.empty()) {
			return extStorage;
		}
		return "/sdcard"_s;
	}
#endif
#if defined(DEATH_TARGET_UNIX)
	String FileSystem::GetLocalStorage()
	{
		StringView localStorage = ::getenv("XDG_DATA_HOME");
		if (IsAbsolutePath(localStorage)) {
			return localStorage;
		}

		StringView home = ::getenv("HOME");
		if (home.empty()) {
			LOGE("Cannot find home directory");
			return {};
		}

		return CombinePath(home, ".local/share/"_s);
	}
#endif
#if defined(DEATH_TARGET_WINDOWS)
	String FileSystem::GetWindowsDirectory()
	{
		SmallVector<wchar_t, MAX_PATH + 1> pathW(DefaultInit, MaxPathLength + 1);
		UINT length = ::GetSystemWindowsDirectoryW(pathW.data(), UINT(pathW.size()));
		if (length > pathW.size()) {
			pathW.resize_for_overwrite(length);
			UINT length2 = ::GetSystemWindowsDirectoryW(pathW.data(), length);
			DEATH_DEBUG_ASSERT(length2 == (length - 1));
			return Utf8::FromUtf16(pathW.data(), length2);
		}
		if (length == 0) {
			DWORD error = ::GetLastError();
			LOGE("GetSystemWindowsDirectory() failed with error 0x{:.8x}{}", error, __GetWin32ErrorSuffix(error));
			return {};
		}
		return Utf8::FromUtf16(pathW.data(), length);
	}
#endif

	bool FileSystem::DirectoryExists(StringView path)
	{
		if (path.empty()) return false;

#if defined(DEATH_TARGET_WINDOWS_RT)
		WIN32_FILE_ATTRIBUTE_DATA lpFileInfo;
		return (::GetFileAttributesExFromAppW(Utf8::ToUtf16(path), GetFileExInfoStandard, &lpFileInfo) && (lpFileInfo.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) == FILE_ATTRIBUTE_DIRECTORY);
#elif defined(DEATH_TARGET_WINDOWS)
		const DWORD attrs = ::GetFileAttributesW(Utf8::ToUtf16(path));
		return (attrs != INVALID_FILE_ATTRIBUTES && (attrs & FILE_ATTRIBUTE_DIRECTORY) == FILE_ATTRIBUTE_DIRECTORY);
#else
		auto nullTerminatedPath = String::nullTerminatedView(path);
#	if defined(DEATH_TARGET_ANDROID)
		if (auto strippedPath = AndroidAssetStream::TryGetAssetPath(nullTerminatedPath)) {
			return AndroidAssetStream::TryOpenDirectory(strippedPath);
		}
#	endif
		struct stat sb;
		if (::stat(nullTerminatedPath.data(), &sb) == 0) {
			return ((sb.st_mode & S_IFMT) == S_IFDIR);
		}
		return false;
#endif
	}

	bool FileSystem::FileExists(StringView path)
	{
		if (path.empty()) return false;

#if defined(DEATH_TARGET_WINDOWS_RT)
		SmallVector<wchar_t, MAX_PATH + 1> pathW(DefaultInit, path.size() + 1);
		Utf8::ToUtf16(pathW.data(), std::int32_t(pathW.size()), path.data(), std::int32_t(path.size()));
		WIN32_FILE_ATTRIBUTE_DATA lpFileInfo;
		return (::GetFileAttributesExFromAppW(pathW.data(), GetFileExInfoStandard, &lpFileInfo) && (lpFileInfo.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != FILE_ATTRIBUTE_DIRECTORY);
#elif defined(DEATH_TARGET_WINDOWS)
		SmallVector<wchar_t, MAX_PATH + 1> pathW(DefaultInit, path.size() + 1);
		Utf8::ToUtf16(pathW.data(), std::int32_t(pathW.size()), path.data(), std::int32_t(path.size()));
		const DWORD attrs = ::GetFileAttributesW(pathW.data());
		return (attrs != INVALID_FILE_ATTRIBUTES && (attrs & FILE_ATTRIBUTE_DIRECTORY) != FILE_ATTRIBUTE_DIRECTORY);
#else
		auto nullTerminatedPath = String::nullTerminatedView(path);
#	if defined(DEATH_TARGET_ANDROID)
		if (auto strippedPath = AndroidAssetStream::TryGetAssetPath(nullTerminatedPath)) {
			return AndroidAssetStream::TryOpenFile(strippedPath);
		}
#	endif
		struct stat sb;
		if (::stat(nullTerminatedPath.data(), &sb) == 0) {
			return ((sb.st_mode & S_IFMT) == S_IFREG);
		}
		return false;
#endif
	}

	bool FileSystem::Exists(StringView path)
	{
		if (path.empty()) return false;

#if defined(DEATH_TARGET_WINDOWS_RT)
		SmallVector<wchar_t, MAX_PATH + 1> pathW(DefaultInit, path.size() + 1);
		Utf8::ToUtf16(pathW.data(), std::int32_t(pathW.size()), path.data(), std::int32_t(path.size()));
		WIN32_FILE_ATTRIBUTE_DATA lpFileInfo;
		return !(!::GetFileAttributesExFromAppW(pathW.data(), GetFileExInfoStandard, &lpFileInfo) && ::GetLastError() == ERROR_FILE_NOT_FOUND);
#elif defined(DEATH_TARGET_WINDOWS)
		SmallVector<wchar_t, MAX_PATH + 1> pathW(DefaultInit, path.size() + 1);
		Utf8::ToUtf16(pathW.data(), std::int32_t(pathW.size()), path.data(), std::int32_t(path.size()));
		const DWORD attrs = ::GetFileAttributesW(pathW.data());
		return !(attrs == INVALID_FILE_ATTRIBUTES && ::GetLastError() == ERROR_FILE_NOT_FOUND);
#else
		auto nullTerminatedPath = String::nullTerminatedView(path);
#	if defined(DEATH_TARGET_ANDROID)
		if (auto strippedPath = AndroidAssetStream::TryGetAssetPath(nullTerminatedPath)) {
			return AndroidAssetStream::TryOpen(strippedPath);
		}
#	endif
		struct stat sb;
		return (::lstat(nullTerminatedPath.data(), &sb) == 0);
#endif
	}

	bool FileSystem::IsReadable(StringView path)
	{
		if (path.empty()) return false;

#if defined(DEATH_TARGET_WINDOWS_RT)
		SmallVector<wchar_t, MAX_PATH + 1> pathW(DefaultInit, path.size() + 1);
		Utf8::ToUtf16(pathW.data(), std::int32_t(pathW.size()), path.data(), std::int32_t(path.size()));
		WIN32_FILE_ATTRIBUTE_DATA lpFileInfo;
		return ::GetFileAttributesExFromAppW(pathW.data(), GetFileExInfoStandard, &lpFileInfo);
#elif defined(DEATH_TARGET_WINDOWS)
		SmallVector<wchar_t, MAX_PATH + 1> pathW(DefaultInit, path.size() + 1);
		Utf8::ToUtf16(pathW.data(), std::int32_t(pathW.size()), path.data(), std::int32_t(path.size()));
		const DWORD attrs = ::GetFileAttributesW(pathW.data());
		return (attrs != INVALID_FILE_ATTRIBUTES);
#else
		auto nullTerminatedPath = String::nullTerminatedView(path);
#	if defined(DEATH_TARGET_ANDROID)
		if (auto strippedPath = AndroidAssetStream::TryGetAssetPath(nullTerminatedPath)) {
			return AndroidAssetStream::TryOpen(strippedPath);
		}
#	endif
		struct stat sb;
		if (::stat(nullTerminatedPath.data(), &sb) == 0) {
			return ((sb.st_mode & S_IRUSR) != 0);
		}
		return false;
#endif
	}

	bool FileSystem::IsWritable(StringView path)
	{
		if (path.empty()) return false;

#if defined(DEATH_TARGET_WINDOWS_RT)
		SmallVector<wchar_t, MAX_PATH + 1> pathW(DefaultInit, path.size() + 1);
		Utf8::ToUtf16(pathW.data(), std::int32_t(pathW.size()), path.data(), std::int32_t(path.size()));
		WIN32_FILE_ATTRIBUTE_DATA lpFileInfo;
		return (::GetFileAttributesExFromAppW(pathW.data(), GetFileExInfoStandard, &lpFileInfo) && (lpFileInfo.dwFileAttributes & FILE_ATTRIBUTE_READONLY) == 0);
#elif defined(DEATH_TARGET_WINDOWS)
		SmallVector<wchar_t, MAX_PATH + 1> pathW(DefaultInit, path.size() + 1);
		Utf8::ToUtf16(pathW.data(), std::int32_t(pathW.size()), path.data(), std::int32_t(path.size()));
		const DWORD attrs = ::GetFileAttributesW(pathW.data());
		return (attrs != INVALID_FILE_ATTRIBUTES && (attrs & FILE_ATTRIBUTE_READONLY) == 0);
#else
		auto nullTerminatedPath = String::nullTerminatedView(path);
#	if defined(DEATH_TARGET_ANDROID)
		if (AndroidAssetStream::TryGetAssetPath(nullTerminatedPath)) {
			// Android assets are not writable
			return false;
		}
#	endif
		struct stat sb;
		if (::stat(nullTerminatedPath.data(), &sb) == 0) {
			return ((sb.st_mode & S_IWUSR) != 0);
		}
		return false;
#endif
	}

	bool FileSystem::IsExecutable(StringView path)
	{
		if (path.empty()) return false;

#if defined(DEATH_TARGET_WINDOWS_RT)
		return false;
#elif defined(DEATH_TARGET_WINDOWS)
		// Assuming that every file that exists is also executable
		SmallVector<wchar_t, MAX_PATH + 1> pathW(DefaultInit, path.size() + 1);
		Utf8::ToUtf16(pathW.data(), std::int32_t(pathW.size()), path.data(), std::int32_t(path.size()));
		const DWORD attrs = ::GetFileAttributesW(pathW.data());
		// Assuming that every existing directory is accessible
		if (attrs != INVALID_FILE_ATTRIBUTES && (attrs & FILE_ATTRIBUTE_DIRECTORY) == FILE_ATTRIBUTE_DIRECTORY) {
			return true;
		} else if (attrs != INVALID_FILE_ATTRIBUTES) {
			// Using some of the Windows executable extensions to detect executable files
			auto extension = GetExtension(path);
			return (extension == "exe"_s || extension == "bat"_s || extension == "com"_s);
		} else {
			return false;
		}
#else
		auto nullTerminatedPath = String::nullTerminatedView(path);
#	if defined(DEATH_TARGET_ANDROID)
		if (auto strippedPath = AndroidAssetStream::TryGetAssetPath(nullTerminatedPath)) {
			return AndroidAssetStream::TryOpenDirectory(strippedPath);
		}
#	endif
		return (::access(nullTerminatedPath.data(), X_OK) == 0);
#endif
	}

	bool FileSystem::IsReadableFile(StringView path)
	{
		if (path.empty()) return false;

#if defined(DEATH_TARGET_WINDOWS_RT)
		SmallVector<wchar_t, MAX_PATH + 1> pathW(DefaultInit, path.size() + 1);
		Utf8::ToUtf16(pathW.data(), std::int32_t(pathW.size()), path.data(), std::int32_t(path.size()));
		WIN32_FILE_ATTRIBUTE_DATA lpFileInfo;
		return (::GetFileAttributesExFromAppW(pathW.data(), GetFileExInfoStandard, &lpFileInfo) && (lpFileInfo.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != FILE_ATTRIBUTE_DIRECTORY);
#elif defined(DEATH_TARGET_WINDOWS)
		SmallVector<wchar_t, MAX_PATH + 1> pathW(DefaultInit, path.size() + 1);
		Utf8::ToUtf16(pathW.data(), std::int32_t(pathW.size()), path.data(), std::int32_t(path.size()));
		const DWORD attrs = ::GetFileAttributesW(pathW.data());
		return (attrs != INVALID_FILE_ATTRIBUTES && (attrs & FILE_ATTRIBUTE_DIRECTORY) != FILE_ATTRIBUTE_DIRECTORY);
#else
		auto nullTerminatedPath = String::nullTerminatedView(path);
#	if defined(DEATH_TARGET_ANDROID)
		if (auto strippedPath = AndroidAssetStream::TryGetAssetPath(nullTerminatedPath)) {
			return AndroidAssetStream::TryOpenFile(strippedPath);
		}
#	endif
		struct stat sb;
		if (::stat(nullTerminatedPath.data(), &sb) == 0) {
			return ((sb.st_mode & S_IFMT) == S_IFREG && (sb.st_mode & S_IRUSR) != 0);
		}
#endif
		return false;
	}

	bool FileSystem::IsWritableFile(StringView path)
	{
		if (path.empty()) return false;

#if defined(DEATH_TARGET_WINDOWS_RT)
		SmallVector<wchar_t, MAX_PATH + 1> pathW(DefaultInit, path.size() + 1);
		Utf8::ToUtf16(pathW.data(), std::int32_t(pathW.size()), path.data(), std::int32_t(path.size()));
		WIN32_FILE_ATTRIBUTE_DATA lpFileInfo;
		return (::GetFileAttributesExFromAppW(pathW.data(), GetFileExInfoStandard, &lpFileInfo) && (lpFileInfo.dwFileAttributes & (FILE_ATTRIBUTE_READONLY | FILE_ATTRIBUTE_DIRECTORY)) == 0);
#elif defined(DEATH_TARGET_WINDOWS)
		SmallVector<wchar_t, MAX_PATH + 1> pathW(DefaultInit, path.size() + 1);
		Utf8::ToUtf16(pathW.data(), std::int32_t(pathW.size()), path.data(), std::int32_t(path.size()));
		const DWORD attrs = ::GetFileAttributesW(pathW.data());
		return (attrs != INVALID_FILE_ATTRIBUTES && (attrs & (FILE_ATTRIBUTE_READONLY | FILE_ATTRIBUTE_DIRECTORY)) == 0);
#else
		auto nullTerminatedPath = String::nullTerminatedView(path);
#	if defined(DEATH_TARGET_ANDROID)
		if (AndroidAssetStream::TryGetAssetPath(nullTerminatedPath)) {
			// Android assets are not writable
			return false;
		}
#	endif
		struct stat sb;
		if (::stat(nullTerminatedPath.data(), &sb) == 0) {
			return ((sb.st_mode & S_IFMT) == S_IFREG && (sb.st_mode & S_IWUSR) != 0);
		}
#endif
		return false;
	}

	bool FileSystem::IsSymbolicLink(StringView path)
	{
		if (path.empty()) return false;

#if defined(DEATH_TARGET_WINDOWS_RT)
		SmallVector<wchar_t, MAX_PATH + 1> pathW(DefaultInit, path.size() + 1);
		Utf8::ToUtf16(pathW.data(), std::int32_t(pathW.size()), path.data(), std::int32_t(path.size()));
		WIN32_FILE_ATTRIBUTE_DATA lpFileInfo;
		return (::GetFileAttributesExFromAppW(pathW.data(), GetFileExInfoStandard, &lpFileInfo) && (lpFileInfo.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) == FILE_ATTRIBUTE_REPARSE_POINT);
#elif defined(DEATH_TARGET_WINDOWS)
		SmallVector<wchar_t, MAX_PATH + 1> pathW(DefaultInit, path.size() + 1);
		Utf8::ToUtf16(pathW.data(), std::int32_t(pathW.size()), path.data(), std::int32_t(path.size()));
		const DWORD attrs = ::GetFileAttributesW(pathW.data());
		return (attrs != INVALID_FILE_ATTRIBUTES && (attrs & FILE_ATTRIBUTE_REPARSE_POINT) == FILE_ATTRIBUTE_REPARSE_POINT);
#else
		auto nullTerminatedPath = String::nullTerminatedView(path);
#	if defined(DEATH_TARGET_ANDROID)
		if (AndroidAssetStream::TryGetAssetPath(nullTerminatedPath)) {
			return false;
		}
#	endif
		struct stat sb;
		if (::lstat(nullTerminatedPath.data(), &sb) == 0) {
			return ((sb.st_mode & S_IFMT) == S_IFLNK);
		}
#endif
		return false;
	}

	bool FileSystem::IsHidden(StringView path)
	{
		if (path.empty()) return false;

#if defined(DEATH_TARGET_WINDOWS_RT)
		SmallVector<wchar_t, MAX_PATH + 1> pathW(DefaultInit, path.size() + 1);
		Utf8::ToUtf16(pathW.data(), std::int32_t(pathW.size()), path.data(), std::int32_t(path.size()));
		WIN32_FILE_ATTRIBUTE_DATA lpFileInfo;
		return (::GetFileAttributesExFromAppW(pathW.data(), GetFileExInfoStandard, &lpFileInfo) && (lpFileInfo.dwFileAttributes & FILE_ATTRIBUTE_HIDDEN) == FILE_ATTRIBUTE_HIDDEN);
#elif defined(DEATH_TARGET_WINDOWS)
		SmallVector<wchar_t, MAX_PATH + 1> pathW(DefaultInit, path.size() + 1);
		Utf8::ToUtf16(pathW.data(), std::int32_t(pathW.size()), path.data(), std::int32_t(path.size()));
		const DWORD attrs = ::GetFileAttributesW(pathW.data());
		return (attrs != INVALID_FILE_ATTRIBUTES && (attrs & FILE_ATTRIBUTE_HIDDEN) == FILE_ATTRIBUTE_HIDDEN);
#elif defined(DEATH_TARGET_APPLE) || defined(__FreeBSD__)
		auto nullTerminatedPath = String::nullTerminatedView(path);
		struct stat sb;
		return (::stat(nullTerminatedPath.data(), &sb) == 0 && (sb.st_flags & UF_HIDDEN) == UF_HIDDEN);
#else
		auto nullTerminatedPath = String::nullTerminatedView(path);
#	if defined(DEATH_TARGET_ANDROID)
		if (AndroidAssetStream::TryGetAssetPath(nullTerminatedPath)) {
			return false;
		}
#	endif
		char buffer[MaxPathLength];
		std::size_t pathLength = std::min((std::size_t)MaxPathLength - 1, path.size());
		strncpy(buffer, path.data(), pathLength);
		buffer[pathLength] = '\0';
		const char* baseName = ::basename(buffer);
		return (baseName != nullptr && baseName[0] == '.');
#endif
	}

	bool FileSystem::SetHidden(StringView path, bool hidden)
	{
		if (path.empty()) return false;

#if defined(DEATH_TARGET_WINDOWS_RT)
		SmallVector<wchar_t, MAX_PATH + 1> pathW(DefaultInit, path.size() + 1);
		Utf8::ToUtf16(pathW.data(), std::int32_t(pathW.size()), path.data(), std::int32_t(path.size()));
		WIN32_FILE_ATTRIBUTE_DATA lpFileInfo;
		if (!::GetFileAttributesExFromAppW(pathW.data(), GetFileExInfoStandard, &lpFileInfo)) {
			return false;
		}

		if (hidden == ((lpFileInfo.dwFileAttributes & FILE_ATTRIBUTE_HIDDEN) == FILE_ATTRIBUTE_HIDDEN)) {
			return true;
		} else if (hidden) {
			return ::SetFileAttributes(pathW.data(), lpFileInfo.dwFileAttributes | FILE_ATTRIBUTE_HIDDEN);
		} else {
			return ::SetFileAttributes(pathW.data(), lpFileInfo.dwFileAttributes & ~FILE_ATTRIBUTE_HIDDEN);
		}
#elif defined(DEATH_TARGET_WINDOWS)
		SmallVector<wchar_t, MAX_PATH + 1> pathW(DefaultInit, path.size() + 1);
		Utf8::ToUtf16(pathW.data(), std::int32_t(pathW.size()), path.data(), std::int32_t(path.size()));
		DWORD attrs = ::GetFileAttributesW(pathW.data());
		if (attrs == INVALID_FILE_ATTRIBUTES) {
			return false;
		}

		if (hidden == ((attrs & FILE_ATTRIBUTE_HIDDEN) == FILE_ATTRIBUTE_HIDDEN)) {
			return true;
		} else if (hidden) {
			return ::SetFileAttributesW(pathW.data(), attrs | FILE_ATTRIBUTE_HIDDEN);
		} else {
			return ::SetFileAttributesW(pathW.data(), attrs & ~FILE_ATTRIBUTE_HIDDEN);
		}
#elif defined(DEATH_TARGET_APPLE) || defined(__FreeBSD__)
		auto nullTerminatedPath = String::nullTerminatedView(path);
		struct stat sb;
		if (::stat(nullTerminatedPath.data(), &sb) != 0) {
			return false;
		}

		if (hidden == ((sb.st_flags & UF_HIDDEN) == UF_HIDDEN)) {
			return true;
		} else if (hidden) {
			return ::chflags(nullTerminatedPath.data(), sb.st_flags | UF_HIDDEN) == 0;
		} else {
			return ::chflags(nullTerminatedPath.data(), sb.st_flags & ~UF_HIDDEN) == 0;
		}
#else
		auto nullTerminatedPath = String::nullTerminatedView(path);
#	if defined(DEATH_TARGET_ANDROID)
		if (AndroidAssetStream::TryGetAssetPath(nullTerminatedPath)) {
			return false;
		}
#	endif
		char buffer[MaxPathLength];
		std::size_t pathLength = std::min((std::size_t)MaxPathLength - 1, path.size());
		strncpy(buffer, nullTerminatedPath.data(), pathLength);
		buffer[pathLength] = '\0';
		const char* baseName = ::basename(buffer);
		if (hidden && baseName != nullptr && baseName[0] != '.') {
			String newPath = CombinePath(GetDirectoryName(nullTerminatedPath), String("."_s + baseName));
			return (::rename(nullTerminatedPath.data(), newPath.data()) == 0);
		} else if (!hidden && baseName != nullptr && baseName[0] == '.') {
			std::int32_t numDots = 0;
			while (baseName[numDots] == '.') {
				numDots++;
			}
			String newPath = CombinePath(GetDirectoryName(nullTerminatedPath), &buffer[numDots]);
			return (::rename(nullTerminatedPath.data(), newPath.data()) == 0);
		}
#endif
		return false;
	}

	bool FileSystem::IsReadOnly(StringView path)
	{
		if (path.empty()) return false;

#if defined(DEATH_TARGET_WINDOWS_RT)
		SmallVector<wchar_t, MAX_PATH + 1> pathW(DefaultInit, path.size() + 1);
		Utf8::ToUtf16(pathW.data(), std::int32_t(pathW.size()), path.data(), std::int32_t(path.size()));
		WIN32_FILE_ATTRIBUTE_DATA lpFileInfo;
		return (::GetFileAttributesExFromAppW(pathW.data(), GetFileExInfoStandard, &lpFileInfo) && (lpFileInfo.dwFileAttributes & FILE_ATTRIBUTE_READONLY) == FILE_ATTRIBUTE_READONLY);
#elif defined(DEATH_TARGET_WINDOWS)
		SmallVector<wchar_t, MAX_PATH + 1> pathW(DefaultInit, path.size() + 1);
		Utf8::ToUtf16(pathW.data(), std::int32_t(pathW.size()), path.data(), std::int32_t(path.size()));
		const DWORD attrs = ::GetFileAttributesW(pathW.data());
		return (attrs != INVALID_FILE_ATTRIBUTES && (attrs & FILE_ATTRIBUTE_READONLY) == FILE_ATTRIBUTE_READONLY);
#elif defined(DEATH_TARGET_APPLE) || defined(__FreeBSD__)
		auto nullTerminatedPath = String::nullTerminatedView(path);
		struct stat sb;
		return (::stat(nullTerminatedPath.data(), &sb) == 0 && (sb.st_flags & UF_IMMUTABLE) == UF_IMMUTABLE);
#else
		return false;
#endif
	}

	bool FileSystem::SetReadOnly(StringView path, bool readonly)
	{
		if (path.empty()) return false;

#if defined(DEATH_TARGET_WINDOWS_RT)
		SmallVector<wchar_t, MAX_PATH + 1> pathW(DefaultInit, path.size() + 1);
		Utf8::ToUtf16(pathW.data(), std::int32_t(pathW.size()), path.data(), std::int32_t(path.size()));
		WIN32_FILE_ATTRIBUTE_DATA lpFileInfo;
		if (!::GetFileAttributesExFromAppW(pathW.data(), GetFileExInfoStandard, &lpFileInfo)) {
			return false;
		}

		if (readonly == ((lpFileInfo.dwFileAttributes & FILE_ATTRIBUTE_READONLY) == FILE_ATTRIBUTE_READONLY)) {
			return true;
		} else if (readonly) {
			return ::SetFileAttributes(pathW.data(), lpFileInfo.dwFileAttributes | FILE_ATTRIBUTE_READONLY);
		} else {
			return ::SetFileAttributes(pathW.data(), lpFileInfo.dwFileAttributes & ~FILE_ATTRIBUTE_READONLY);
		}
#elif defined(DEATH_TARGET_WINDOWS)
		SmallVector<wchar_t, MAX_PATH + 1> pathW(DefaultInit, path.size() + 1);
		Utf8::ToUtf16(pathW.data(), std::int32_t(pathW.size()), path.data(), std::int32_t(path.size()));
		DWORD attrs = ::GetFileAttributesW(pathW.data());
		if (attrs == INVALID_FILE_ATTRIBUTES) {
			return false;
		}

		if (readonly == ((attrs & FILE_ATTRIBUTE_READONLY) == FILE_ATTRIBUTE_READONLY)) {
			return true;
		} else if (readonly) {
			return ::SetFileAttributesW(pathW.data(), attrs | FILE_ATTRIBUTE_READONLY);
		} else {
			return ::SetFileAttributesW(pathW.data(), attrs & ~FILE_ATTRIBUTE_READONLY);
		}
#elif defined(DEATH_TARGET_APPLE) || defined(__FreeBSD__)
		auto nullTerminatedPath = String::nullTerminatedView(path);
		struct stat sb;
		if (::stat(nullTerminatedPath.data(), &sb) != 0) {
			return false;
		}

		if (readonly == ((sb.st_flags & UF_IMMUTABLE) == UF_IMMUTABLE)) {
			return true;
		} else if (readonly) {
			return ::chflags(nullTerminatedPath.data(), sb.st_flags | UF_IMMUTABLE) == 0;
		} else {
			return ::chflags(nullTerminatedPath.data(), sb.st_flags & ~UF_IMMUTABLE) == 0;
		}
#endif
		return false;
	}

	bool FileSystem::CreateDirectories(StringView path)
	{
		if (path.empty()) return false;

#if defined(DEATH_TARGET_WINDOWS)
		SmallVector<wchar_t, MAX_PATH + 1> fullPath(DefaultInit, path.size() + 1);
		std::int32_t fullPathSize = Utf8::ToUtf16(fullPath.data(), std::int32_t(fullPath.size()), path.data(), std::int32_t(path.size()));
		// Don't use DirectoryExists() to avoid calling Utf8::ToUtf16() twice
#	if defined(DEATH_TARGET_WINDOWS_RT)
		WIN32_FILE_ATTRIBUTE_DATA lpFileInfo;
		if (::GetFileAttributesExFromAppW(fullPath.data(), GetFileExInfoStandard, &lpFileInfo) && (lpFileInfo.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) == FILE_ATTRIBUTE_DIRECTORY) {
			return true;
		}
#	else
		const DWORD attrs = ::GetFileAttributesW(fullPath.data());
		if (attrs != INVALID_FILE_ATTRIBUTES && (attrs & FILE_ATTRIBUTE_DIRECTORY) == FILE_ATTRIBUTE_DIRECTORY) {
			return true;
		}
#	endif

		std::int32_t startIdx = 0;
		if (fullPathSize >= 2) {
			if (fullPath[0] == L'\\' && fullPath[1] == L'\\') {
				// Skip the first part of UNC paths ("\\.\", "\\?\" or "\\hostname\")
				startIdx = 3;
				while (fullPath[startIdx] != L'\\' && fullPath[startIdx] != L'\0') {
					startIdx++;
				}
				startIdx++;
			}
			if (fullPath[startIdx + 1] == L':') {
				startIdx += 3;
			}
		}
		if (startIdx == 0 && (fullPath[0] == L'/' || fullPath[0] == L'\\')) {
			startIdx = 1;
		}

		bool slashWasLast = true;
		std::int32_t i = startIdx;
		for (; i < fullPathSize; i++) {
			if (fullPath[i] == L'\0') {
				break;
			}

			if (fullPath[i] == L'/' || fullPath[i] == L'\\') {
				fullPath[i] = L'\0';
#	if defined(DEATH_TARGET_WINDOWS_RT)
				if (!::GetFileAttributesExFromAppW(fullPath.data(), GetFileExInfoStandard, &lpFileInfo)) {
					if (!::CreateDirectoryFromAppW(fullPath.data(), NULL)) {
#	else
				const DWORD attrs = ::GetFileAttributesW(fullPath.data());
				if (attrs == INVALID_FILE_ATTRIBUTES) {
					if (!::CreateDirectoryW(fullPath.data(), NULL)) {
#	endif
						DWORD error = ::GetLastError();
						if (error != ERROR_ALREADY_EXISTS) {
#	if defined(DEATH_TRACE_VERBOSE_IO)
							LOGW("Cannot create directory \"{}\" with error 0x{:.8x}{}", Utf8::FromUtf16(fullPath.data(), i), error, __GetWin32ErrorSuffix(error));
#	endif
							return false;
						}
					}
				}
				fullPath[i] = L'\\';
				slashWasLast = true;
			} else {
				slashWasLast = false;
			}
		}

		if (!slashWasLast) {
#	if defined(DEATH_TARGET_WINDOWS_RT)
			if (!::CreateDirectoryFromAppW(fullPath.data(), NULL)) {
#	else
			if (!::CreateDirectoryW(fullPath.data(), NULL)) {
#	endif
				DWORD error = ::GetLastError();
				if (error != ERROR_ALREADY_EXISTS) {
#	if defined(DEATH_TRACE_VERBOSE_IO)
					LOGW("Cannot create directory \"{}\" with error 0x{:.8x}{}", Utf8::FromUtf16(fullPath.data(), i), error, __GetWin32ErrorSuffix(error));
#	endif
					return false;
				}
			}
		}
		return true;
#else
		if (DirectoryExists(path)) {
			return true;
		}

		auto nullTerminatedPath = String::nullTerminatedView(path);
#	if defined(DEATH_TARGET_ANDROID)
		if (AndroidAssetStream::TryGetAssetPath(nullTerminatedPath)) {
			return false;
		}
#	endif

		String fullPath = String{nullTerminatedPath};
		bool slashWasLast = true;
		struct stat sb;
		for (std::size_t i = 0; i < fullPath.size(); i++) {
			if (fullPath[i] == '\0') {
				break;
			}

			if (fullPath[i] == '/' || fullPath[i] == '\\') {
				if (i > 0) {
					fullPath[i] = '\0';
					if (::lstat(fullPath.data(), &sb) != 0) {
						if (::mkdir(fullPath.data(), S_IRWXU | S_IRWXG | S_IROTH | S_IXOTH) != 0 && errno != EEXIST) {
#	if defined(DEATH_TRACE_VERBOSE_IO)
							LOGW("Cannot create directory \"{}\" with error {}{}", fullPath, errno, __GetUnixErrorSuffix(errno));
#	endif
							return false;
						}
					}
					fullPath[i] = '/';
				}
				slashWasLast = true;
			} else {
				slashWasLast = false;
			}
		}

		if (!slashWasLast) {
			if (::mkdir(fullPath.data(), S_IRWXU | S_IRWXG | S_IROTH | S_IXOTH) != 0 && errno != EEXIST) {
#	if defined(DEATH_TRACE_VERBOSE_IO)
				LOGW("Cannot create directory \"{}\" with error {}{}", fullPath, errno, __GetUnixErrorSuffix(errno));
#	endif
				return false;
			}
		}
		return true;
#endif
	}

	bool FileSystem::RemoveDirectoryRecursive(StringView path)
	{
		if (path.empty()) return false;

#if defined(DEATH_TARGET_WINDOWS_RT)
		SmallVector<wchar_t, MAX_PATH + 1> pathW(DefaultInit, path.size() + 1);
		Utf8::ToUtf16(pathW.data(), std::int32_t(pathW.size()), path.data(), std::int32_t(path.size()));
		WIN32_FILE_ATTRIBUTE_DATA lpFileInfo;
		if (!::GetFileAttributesExFromAppW(pathW.data(), GetFileExInfoStandard, &lpFileInfo) || (lpFileInfo.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != FILE_ATTRIBUTE_DIRECTORY) {
			return false;
		}

		// Do not recursively delete through reparse points
		Array<wchar_t> absPath = Utf8::ToUtf16(GetAbsolutePath(path));
		return DeleteDirectoryInternal(absPath, (lpFileInfo.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) != FILE_ATTRIBUTE_REPARSE_POINT, 0);
#elif defined(DEATH_TARGET_WINDOWS)
		SmallVector<wchar_t, MAX_PATH + 1> pathW(DefaultInit, path.size() + 1);
		Utf8::ToUtf16(pathW.data(), std::int32_t(pathW.size()), path.data(), std::int32_t(path.size()));
		const DWORD attrs = ::GetFileAttributesW(pathW.data());
		if (attrs == INVALID_FILE_ATTRIBUTES || (attrs & FILE_ATTRIBUTE_DIRECTORY) != FILE_ATTRIBUTE_DIRECTORY) {
			return false;
		}

		// Do not recursively delete through reparse points
		Array<wchar_t> absPath = Utf8::ToUtf16(GetAbsolutePath(path));
		return DeleteDirectoryInternal(absPath, (attrs & FILE_ATTRIBUTE_REPARSE_POINT) != FILE_ATTRIBUTE_REPARSE_POINT, 0);
#else
		return DeleteDirectoryInternal(path);
#endif
	}

	bool FileSystem::RemoveFile(StringView path)
	{
		if (path.empty()) return false;

#if defined(DEATH_TARGET_WINDOWS_RT)
		SmallVector<wchar_t, MAX_PATH + 1> pathW(path.size() + 1);
		Utf8::ToUtf16(pathW.data(), std::int32_t(pathW.size()), path.data(), std::int32_t(path.size()));
		return ::DeleteFileFromAppW(pathW.data());
#elif defined(DEATH_TARGET_WINDOWS)
		SmallVector<wchar_t, MAX_PATH + 1> pathW(path.size() + 1);
		Utf8::ToUtf16(pathW.data(), std::int32_t(pathW.size()), path.data(), std::int32_t(path.size()));
		return ::DeleteFileW(pathW.data());
#else
		auto nullTerminatedPath = String::nullTerminatedView(path);
#	if defined(DEATH_TARGET_ANDROID)
		if (AndroidAssetStream::TryGetAssetPath(nullTerminatedPath)) {
			return false;
		}
#	endif
		return (::unlink(nullTerminatedPath.data()) == 0);
#endif
	}

	bool FileSystem::Move(StringView oldPath, StringView newPath)
	{
		if (oldPath.empty() || newPath.empty()) return false;

#if defined(DEATH_TARGET_WINDOWS)
		SmallVector<wchar_t, MAX_PATH + 1> oldPathW(oldPath.size() + 1);
		Utf8::ToUtf16(oldPathW.data(), std::int32_t(oldPathW.size()), oldPath.data(), std::int32_t(oldPath.size()));
		SmallVector<wchar_t, MAX_PATH + 1> newPathW(newPath.size() + 1);
		Utf8::ToUtf16(newPathW.data(), std::int32_t(newPathW.size()), newPath.data(), std::int32_t(newPath.size()));
		return ::MoveFileExW(oldPathW.data(), newPathW.data(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_COPY_ALLOWED);
#else
		auto nullTerminatedOldPath = String::nullTerminatedView(oldPath);
		auto nullTerminatedNewPath = String::nullTerminatedView(newPath);
#	if defined(DEATH_TARGET_ANDROID)
		if (AndroidAssetStream::TryGetAssetPath(nullTerminatedOldPath) || AndroidAssetStream::TryGetAssetPath(nullTerminatedNewPath)) {
			return false;
		}
#	endif
		return (::rename(nullTerminatedOldPath.data(), nullTerminatedNewPath.data()) == 0);
#endif
	}

	bool FileSystem::MoveToTrash(StringView path)
	{
		if (path.empty()) return false;

#if defined(DEATH_TARGET_APPLE)
		Class nsStringClass = objc_getClass("NSString");
		Class nsUrlClass = objc_getClass("NSURL");
		Class nsFileManager = objc_getClass("NSFileManager");
		if (nsStringClass != nullptr && nsUrlClass != nullptr && nsFileManager != nullptr) {
			id pathString = ((id(*)(Class, SEL, const char*))objc_msgSend)(nsStringClass, sel_getUid("stringWithUTF8String:"), String::nullTerminatedView(path).data());
			id pathUrl = ((id(*)(Class, SEL, id))objc_msgSend)(nsUrlClass, sel_getUid("fileURLWithPath:"), pathString);
			id fileManagerInstance = ((id(*)(Class, SEL))objc_msgSend)(nsFileManager, sel_getUid("defaultManager"));
			return ((bool(*)(id, SEL, id, SEL, id, SEL, id))objc_msgSend)(fileManagerInstance, sel_getUid("trashItemAtURL:"), pathUrl, sel_getUid("resultingItemURL:"), nullptr, sel_getUid("error:"), nullptr);
		}
		return false;
#elif defined(DEATH_TARGET_WINDOWS) && !defined(DEATH_TARGET_WINDOWS_RT)
		SmallVector<wchar_t, MAX_PATH + 1> pathW(path.size() + 1);
		Utf8::ToUtf16(pathW.data(), std::int32_t(pathW.size()), path.data(), std::int32_t(path.size()));

		SHFILEOPSTRUCTW sf;
		sf.hwnd = NULL;
		sf.wFunc = FO_DELETE;
		sf.pFrom = pathW.data();
		sf.pTo = nullptr;
		sf.fFlags = FOF_ALLOWUNDO | FOF_NOCONFIRMATION | FOF_NOERRORUI | FOF_SILENT;
		sf.fAnyOperationsAborted = FALSE;
		sf.hNameMappings = nullptr;
		sf.lpszProgressTitle = nullptr;
		return ::SHFileOperationW(&sf) == 0;
#else
		return false;
#endif
	}

	bool FileSystem::Copy(StringView oldPath, StringView newPath, bool overwrite)
	{
		if (oldPath.empty() || newPath.empty()) return false;

#if defined(DEATH_TARGET_WINDOWS_RT)
		SmallVector<wchar_t, MAX_PATH + 1> oldPathW(oldPath.size() + 1);
		Utf8::ToUtf16(oldPathW.data(), std::int32_t(oldPathW.size()), oldPath.data(), std::int32_t(oldPath.size()));
		SmallVector<wchar_t, MAX_PATH + 1> newPathW(newPath.size() + 1);
		Utf8::ToUtf16(newPathW.data(), std::int32_t(newPathW.size()), newPath.data(), std::int32_t(newPath.size()));
		return ::CopyFileFromAppW(oldPathW.data(), newPathW.data(), overwrite ? TRUE : FALSE);
#elif defined(DEATH_TARGET_WINDOWS)
		SmallVector<wchar_t, MAX_PATH + 1> oldPathW(oldPath.size() + 1);
		Utf8::ToUtf16(oldPathW.data(), std::int32_t(oldPathW.size()), oldPath.data(), std::int32_t(oldPath.size()));
		SmallVector<wchar_t, MAX_PATH + 1> newPathW(newPath.size() + 1);
		Utf8::ToUtf16(newPathW.data(), std::int32_t(newPathW.size()), newPath.data(), std::int32_t(newPath.size()));
		return ::CopyFileW(oldPathW.data(), newPathW.data(), overwrite ? TRUE : FALSE);
#else
		auto nullTerminatedOldPath = String::nullTerminatedView(oldPath);
		auto nullTerminatedNewPath = String::nullTerminatedView(newPath);
#	if defined(DEATH_TARGET_ANDROID)
		if (AndroidAssetStream::TryGetAssetPath(nullTerminatedOldPath) || AndroidAssetStream::TryGetAssetPath(nullTerminatedNewPath)) {
			return false;
		}
#	endif
		if (!overwrite && Exists(newPath)) {
			return false;
		}

		std::int32_t sourceFd, destFd;
		if ((sourceFd = ::open(nullTerminatedOldPath.data(), O_RDONLY | O_CLOEXEC)) == -1) {
			return false;
		}

		struct stat sb;
		if (::fstat(sourceFd, &sb) != 0) {
			return false;
		}

		mode_t sourceMode = sb.st_mode;
		mode_t destMode = sourceMode;
#if !defined(DEATH_TARGET_EMSCRIPTEN)
		// Enable writing for the newly created files, needed for some file systems
		destMode |= S_IWUSR;
#endif
		if ((destFd = ::open(nullTerminatedNewPath.data(), O_WRONLY | O_CLOEXEC | O_CREAT | O_TRUNC, destMode)) == -1) {
			::close(sourceFd);
			return false;
		}

#if !defined(DEATH_TARGET_APPLE) && !defined(DEATH_TARGET_SWITCH) && !defined(__FreeBSD__)
		while (true) {
			if (::fallocate(destFd, FALLOC_FL_KEEP_SIZE, 0, sb.st_size) == 0) {
				break;
			}
			std::int32_t error = errno;
			if (error == EOPNOTSUPP || error == ENOSYS) {
				break;
			}
			if (error != EINTR) {
				return false;
			}
		}
#endif

#	if defined(DEATH_TARGET_APPLE)
		// fcopyfile works on FreeBSD and OS X 10.5+ 
		bool success = (::fcopyfile(sourceFd, destFd, 0, COPYFILE_ALL) == 0);
#	elif defined(__linux__)
		constexpr std::size_t MaxSendSize = 0x7FFFF000u;

		std::uint64_t size = sb.st_size;
		std::uint64_t offset = 0;
		bool success = true;
		while (offset < size) {
			std::uint64_t bytesLeft = size - offset;
			std::size_t bytesToCopy = MaxSendSize;
			if (bytesLeft < static_cast<std::uint64_t>(MaxSendSize)) {
				bytesToCopy = static_cast<std::size_t>(bytesLeft);
			}
			ssize_t sz = ::sendfile(destFd, sourceFd, nullptr, bytesToCopy);
			if DEATH_UNLIKELY(sz < 0) {
				std::int32_t error = errno;
				if (error == EINTR) {
					continue;
				}

				success = false;
				break;
			}

			offset += sz;
		}
#	else
#		if defined(POSIX_FADV_SEQUENTIAL) && (!defined(__ANDROID__) || __ANDROID_API__ >= 21) && !defined(DEATH_TARGET_SWITCH)
		// As noted in https://eklitzke.org/efficient-file-copying-on-linux, might make the file reading faster
		::posix_fadvise(sourceFd, 0, 0, POSIX_FADV_SEQUENTIAL);
#		endif

#		if defined(DEATH_TARGET_EMSCRIPTEN)
		constexpr std::size_t BufferSize = 8 * 1024;
#		else
		constexpr std::size_t BufferSize = 128 * 1024;
#		endif
		char buffer[BufferSize];
		bool success = true;
		while (true) {
			ssize_t bytesRead = ::read(sourceFd, buffer, BufferSize);
			if (bytesRead == 0) {
				break;
			}
			if DEATH_UNLIKELY(bytesRead < 0) {
				if (errno == EINTR) {
					continue;	// Retry
				}

				success = false;
				goto End;
			}

			ssize_t bytesWritten = 0;
			do {
				ssize_t sz = ::write(destFd, buffer + bytesWritten, bytesRead);
				if DEATH_UNLIKELY(sz < 0) {
					if (errno == EINTR) {
						continue;	// Retry
					}

					success = false;
					goto End;
				}

				bytesRead -= sz;
				bytesWritten += sz;
			} while (bytesRead > 0);
		}
	End:
#	endif

#	if !defined(DEATH_TARGET_EMSCRIPTEN)
		// If we created a new file with an explicitly added S_IWUSR permission, we may need to update its mode bits to match the source file.
		if (destMode != sourceMode && ::fchmod(destFd, sourceMode) != 0) {
			success = false;
		}
#	endif

		::close(sourceFd);
		::close(destFd);

		return success;
#endif
	}

	std::int64_t FileSystem::GetFileSize(StringView path)
	{
		if (path.empty()) return false;

#if defined(DEATH_TARGET_WINDOWS)
#	if defined(DEATH_TARGET_WINDOWS_RT)
		SmallVector<wchar_t, MAX_PATH + 1> pathW(path.size() + 1);
		Utf8::ToUtf16(pathW.data(), std::int32_t(pathW.size()), path.data(), std::int32_t(path.size()));
		HANDLE hFile = ::CreateFileFromAppW(pathW.data(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS, NULL);
#	else
		SmallVector<wchar_t, MAX_PATH + 1> pathW(path.size() + 1);
		Utf8::ToUtf16(pathW.data(), std::int32_t(pathW.size()), path.data(), std::int32_t(path.size()));
		HANDLE hFile = ::CreateFileW(pathW.data(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS, NULL);
#	endif
		LARGE_INTEGER fileSize;
		fileSize.QuadPart = 0;
		const BOOL status = ::GetFileSizeEx(hFile, &fileSize);
		::CloseHandle(hFile);
		return (status != 0 ? static_cast<std::int64_t>(fileSize.QuadPart) : -1);
#else
		auto nullTerminatedPath = String::nullTerminatedView(path);
#	if defined(DEATH_TARGET_ANDROID)
		if (auto strippedPath = AndroidAssetStream::TryGetAssetPath(nullTerminatedPath)) {
			return AndroidAssetStream::GetFileSize(strippedPath);
		}
#	endif
		struct stat sb;
		if (::stat(nullTerminatedPath.data(), &sb) != 0) {
			return -1;
		}
		return static_cast<std::int64_t>(sb.st_size);
#endif
	}

	DateTime FileSystem::GetCreationTime(StringView path)
	{
		if (path.empty()) return {};

		DateTime date;
#if defined(DEATH_TARGET_WINDOWS)
		SmallVector<wchar_t, MAX_PATH + 1> pathW(path.size() + 1);
		Utf8::ToUtf16(pathW.data(), std::int32_t(pathW.size()), path.data(), std::int32_t(path.size()));
#	if defined(DEATH_TARGET_WINDOWS_RT)
		HANDLE hFile = ::CreateFileFromAppW(pathW.data(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS, NULL);
#	else
		HANDLE hFile = ::CreateFileW(pathW.data(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS, NULL);
#	endif
		FILETIME fileTime;
		if (::GetFileTime(hFile, &fileTime, nullptr, nullptr)) {
			date = DateTime(fileTime);
		}
		::CloseHandle(hFile);
#elif defined(DEATH_TARGET_APPLE) && defined(_DARWIN_FEATURE_64_BIT_INODE)
		auto nullTerminatedPath = String::nullTerminatedView(path);
		struct stat sb;
		if (::stat(nullTerminatedPath.data(), &sb) == 0) {
			date = DateTime(sb.st_birthtimespec.tv_sec);
			date.SetMillisecond(sb.st_birthtimespec.tv_nsec / 1000000);
		}
#else
		auto nullTerminatedPath = String::nullTerminatedView(path);
#	if defined(DEATH_TARGET_ANDROID)
		if (AndroidAssetStream::TryGetAssetPath(nullTerminatedPath)) {
			return date;
		}
#	endif
		struct stat sb;
		if (::stat(nullTerminatedPath.data(), &sb) == 0) {
			// Creation time is not available on Linux, return the last change of inode instead
			date = DateTime(sb.st_ctime);
		}
#endif
		return date;
	}

	DateTime FileSystem::GetLastModificationTime(StringView path)
	{
		if (path.empty()) return {};

		DateTime date;
#if defined(DEATH_TARGET_WINDOWS)
		SmallVector<wchar_t, MAX_PATH + 1> pathW(path.size() + 1);
		Utf8::ToUtf16(pathW.data(), std::int32_t(pathW.size()), path.data(), std::int32_t(path.size()));
#	if defined(DEATH_TARGET_WINDOWS_RT)
		HANDLE hFile = ::CreateFileFromAppW(pathW.data(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS, NULL);
#	else
		HANDLE hFile = ::CreateFileW(pathW.data(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS, NULL);
#	endif
		FILETIME fileTime;
		if (::GetFileTime(hFile, nullptr, nullptr, &fileTime)) {
			date = DateTime(fileTime);
		}
		::CloseHandle(hFile);
#elif defined(DEATH_TARGET_APPLE) && defined(_DARWIN_FEATURE_64_BIT_INODE)
		auto nullTerminatedPath = String::nullTerminatedView(path);
		struct stat sb;
		if (::stat(nullTerminatedPath.data(), &sb) == 0) {
			date = DateTime(sb.st_mtimespec.tv_sec);
			date.SetMillisecond(sb.st_mtimespec.tv_nsec / 1000000);
		}
#else
		auto nullTerminatedPath = String::nullTerminatedView(path);
#	if defined(DEATH_TARGET_ANDROID)
		if (AndroidAssetStream::TryGetAssetPath(nullTerminatedPath)) {
			return date;
		}
#	endif
		struct stat sb;
		if (::stat(nullTerminatedPath.data(), &sb) == 0) {
			date = DateTime(sb.st_mtime);
		}
#endif
		return date;
	}

	DateTime FileSystem::GetLastAccessTime(StringView path)
	{
		if (path.empty()) return {};

		DateTime date;
#if defined(DEATH_TARGET_WINDOWS)
		SmallVector<wchar_t, MAX_PATH + 1> pathW(path.size() + 1);
		Utf8::ToUtf16(pathW.data(), std::int32_t(pathW.size()), path.data(), std::int32_t(path.size()));
#	if defined(DEATH_TARGET_WINDOWS_RT)
		HANDLE hFile = ::CreateFileFromAppW(pathW.data(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS, NULL);
#	else
		HANDLE hFile = ::CreateFileW(pathW.data(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS, NULL);
#	endif
		FILETIME fileTime;
		if (::GetFileTime(hFile, nullptr, &fileTime, nullptr)) {
			date = DateTime(fileTime);
		}
		::CloseHandle(hFile);
#elif defined(DEATH_TARGET_APPLE) && defined(_DARWIN_FEATURE_64_BIT_INODE)
		auto nullTerminatedPath = String::nullTerminatedView(path);
		struct stat sb;
		if (::stat(nullTerminatedPath.data(), &sb) == 0) {
			date = DateTime(sb.st_atimespec.tv_sec);
			date.SetMillisecond(sb.st_atimespec.tv_nsec / 1000000);
		}
#else
		auto nullTerminatedPath = String::nullTerminatedView(path);
#	if defined(DEATH_TARGET_ANDROID)
		if (AndroidAssetStream::TryGetAssetPath(nullTerminatedPath)) {
			return date;
		}
#	endif
		struct stat sb;
		if (::stat(nullTerminatedPath.data(), &sb) == 0) {
			date = DateTime(sb.st_atime);
		}
#endif
		return date;
	}

	FileSystem::Permission FileSystem::GetPermissions(StringView path)
	{
		if (path.empty()) return Permission::None;

#if defined(DEATH_TARGET_WINDOWS)
		SmallVector<wchar_t, MAX_PATH + 1> pathW(path.size() + 1);
		Utf8::ToUtf16(pathW.data(), std::int32_t(pathW.size()), path.data(), std::int32_t(path.size()));
		Permission mode = Permission::Read;
		if (IsExecutable(path)) {
			mode |= Permission::Execute;
		}
		const DWORD attrs = ::GetFileAttributesW(pathW.data());
		if (attrs != INVALID_FILE_ATTRIBUTES && (attrs & FILE_ATTRIBUTE_READONLY) == 0) {
			mode |= Permission::Write;
		}
		return mode;
#else
		auto nullTerminatedPath = String::nullTerminatedView(path);
#	if defined(DEATH_TARGET_ANDROID)
		if (auto strippedPath = AndroidAssetStream::TryGetAssetPath(nullTerminatedPath)) {
			if (AndroidAssetStream::TryOpenDirectory(strippedPath)) {
				return Permission::Read | Permission::Execute;
			} else if (AndroidAssetStream::TryOpenFile(strippedPath)) {
				return Permission::Read;
			} else {
				return Permission::None;
			}
		}
#	endif
		struct stat sb;
		if (::stat(nullTerminatedPath.data(), &sb) != 0) {
			return Permission::None;
		}
		return NativeModeToEnum(sb.st_mode);
#endif
	}

	bool FileSystem::ChangePermissions(StringView path, Permission mode)
	{
		if (path.empty()) return false;

#if defined(DEATH_TARGET_WINDOWS)
		SmallVector<wchar_t, MAX_PATH + 1> pathW(path.size() + 1);
		Utf8::ToUtf16(pathW.data(), std::int32_t(pathW.size()), path.data(), std::int32_t(path.size()));
		DWORD attrs = ::GetFileAttributesW(pathW.data());
		if (attrs != INVALID_FILE_ATTRIBUTES) {
			if ((mode & Permission::Write) == Permission::Write && (attrs & FILE_ATTRIBUTE_READONLY) == FILE_ATTRIBUTE_READONLY) {
				// Adding the write permission
				attrs &= ~FILE_ATTRIBUTE_READONLY;
				return ::SetFileAttributesW(pathW.data(), attrs);
			} else if ((mode & Permission::Write) != Permission::Write && (attrs & FILE_ATTRIBUTE_READONLY) != FILE_ATTRIBUTE_READONLY) {
				// Removing the write permission
				attrs |= FILE_ATTRIBUTE_READONLY;
				return ::SetFileAttributesW(pathW.data(), attrs);
			}
			return true;
		}
		return false;
#else
		auto nullTerminatedPath = String::nullTerminatedView(path);
#	if defined(DEATH_TARGET_ANDROID)
		if (AndroidAssetStream::TryGetAssetPath(nullTerminatedPath)) {
			return false;
		}
#	endif
		struct stat sb;
		if (::stat(nullTerminatedPath.data(), &sb) != 0) {
			return false;
		}
		const std::uint32_t currentMode = sb.st_mode;
		std::uint32_t newMode = AddPermissionsToCurrent(currentMode & ~(S_IRUSR | S_IWUSR | S_IXUSR), mode);
		return (::chmod(nullTerminatedPath.data(), newMode) == 0);
#endif
	}

	bool FileSystem::AddPermissions(StringView path, Permission mode)
	{
		if (path.empty()) return false;

#if defined(DEATH_TARGET_WINDOWS)
		SmallVector<wchar_t, MAX_PATH + 1> pathW(path.size() + 1);
		Utf8::ToUtf16(pathW.data(), std::int32_t(pathW.size()), path.data(), std::int32_t(path.size()));
		DWORD attrs = ::GetFileAttributesW(pathW.data());
		if (attrs != INVALID_FILE_ATTRIBUTES) {
			// Adding the write permission
			if ((mode & Permission::Write) == Permission::Write && (attrs & FILE_ATTRIBUTE_READONLY) == FILE_ATTRIBUTE_READONLY) {
				attrs &= ~FILE_ATTRIBUTE_READONLY;
				return ::SetFileAttributesW(pathW.data(), attrs);
			}
			return true;
		}
		return false;
#else
		auto nullTerminatedPath = String::nullTerminatedView(path);
#	if defined(DEATH_TARGET_ANDROID)
		if (AndroidAssetStream::TryGetAssetPath(nullTerminatedPath)) {
			return false;
		}
#	endif
		struct stat sb;
		if (::stat(nullTerminatedPath.data(), &sb) != 0) {
			return false;
		}
		const std::uint32_t currentMode = sb.st_mode;
		const std::uint32_t newMode = AddPermissionsToCurrent(currentMode, mode);
		return (::chmod(nullTerminatedPath.data(), newMode) == 0);
#endif
	}

	bool FileSystem::RemovePermissions(StringView path, Permission mode)
	{
		if (path.empty()) return false;

#if defined(DEATH_TARGET_WINDOWS)
		SmallVector<wchar_t, MAX_PATH + 1> pathW(path.size() + 1);
		Utf8::ToUtf16(pathW.data(), std::int32_t(pathW.size()), path.data(), std::int32_t(path.size()));
		DWORD attrs = ::GetFileAttributesW(pathW.data());
		if (attrs != INVALID_FILE_ATTRIBUTES) {
			// Removing the write permission
			if ((mode & Permission::Write) == Permission::Write && (attrs & FILE_ATTRIBUTE_READONLY) != FILE_ATTRIBUTE_READONLY) {
				attrs |= FILE_ATTRIBUTE_READONLY;
				return ::SetFileAttributesW(pathW.data(), attrs);
			}
			return true;
		}
		return false;
#else
		auto nullTerminatedPath = String::nullTerminatedView(path);
#	if defined(DEATH_TARGET_ANDROID)
		if (AndroidAssetStream::TryGetAssetPath(nullTerminatedPath)) {
			return false;
		}
#	endif
		struct stat sb;
		if (::stat(nullTerminatedPath.data(), &sb) != 0) {
			return false;
		}
		const std::uint32_t currentMode = sb.st_mode;
		const std::uint32_t newMode = RemovePermissionsFromCurrent(currentMode, mode);
		return (::chmod(nullTerminatedPath.data(), newMode) == 0);
#endif
	}

	bool FileSystem::LaunchDirectoryAsync(StringView path)
	{
#if defined(DEATH_TARGET_APPLE)
		if (!DirectoryExists(path)) {
			return false;
		}
		Class nsStringClass = objc_getClass("NSString");
		Class nsUrlClass = objc_getClass("NSURL");
		Class nsWorkspaceClass = objc_getClass("NSWorkspace");
		if (nsStringClass != nullptr && nsUrlClass != nullptr && nsWorkspaceClass != nullptr) {
			id pathString = ((id(*)(Class, SEL, const char*))objc_msgSend)(nsStringClass, sel_getUid("stringWithUTF8String:"), String::nullTerminatedView(path).data());
			id pathUrl = ((id(*)(Class, SEL, id))objc_msgSend)(nsUrlClass, sel_getUid("fileURLWithPath:"), pathString);
			id workspaceInstance = ((id(*)(Class, SEL))objc_msgSend)(nsWorkspaceClass, sel_getUid("sharedWorkspace"));
			return ((bool(*)(id, SEL, id))objc_msgSend)(workspaceInstance, sel_getUid("openURL:"), pathUrl);
		}
		return false;
#elif defined(DEATH_TARGET_WINDOWS_RT)
		if (!DirectoryExists(path)) {
			return false;
		}
		SmallVector<wchar_t, MAX_PATH + 1> pathW(path.size() + 1);
		std::int32_t lengthW = Utf8::ToUtf16(pathW.data(), std::int32_t(pathW.size()), path.data(), std::int32_t(path.size()));
		winrt::Windows::System::Launcher::LaunchFolderPathAsync(winrt::hstring(pathW.data(), (winrt::hstring::size_type)lengthW));
		return true;
#elif defined(DEATH_TARGET_WINDOWS)
		if (!DirectoryExists(path)) {
			return false;
		}
		SmallVector<wchar_t, MAX_PATH + 1> pathW(path.size() + 1);
		Utf8::ToUtf16(pathW.data(), std::int32_t(pathW.size()), path.data(), std::int32_t(path.size()));
		return (INT_PTR)::ShellExecuteW(NULL, nullptr, pathW.data(), nullptr, nullptr, SW_SHOWNORMAL) > 32;
#elif defined(DEATH_TARGET_UNIX)
		if (!DirectoryExists(path)) {
			return false;
		}

		pid_t child = ::fork();
		if (child < 0) {
			return false;
		}
		if (child == 0) {
			pid_t doubleFork = ::fork();
			if (doubleFork < 0) {
				_exit(1);
			}
			if (doubleFork == 0) {
				TryCloseAllFileDescriptors();

				RedirectFileDescriptorToNull(STDIN_FILENO);
				RedirectFileDescriptorToNull(STDOUT_FILENO);
				RedirectFileDescriptorToNull(STDERR_FILENO);

				// Execute child process in a new process group
				::setsid();

				// Execute "xdg-open"
				::execlp("xdg-open", "xdg-open", String::nullTerminatedView(path).data(), (char*)0);
				_exit(1);
			} else {
				_exit(0);
			}
		}

		std::int32_t status;
		::waitpid(child, &status, 0);
		return (WEXITSTATUS(status) == 0);
#else
		return false;
#endif
	}

#if defined(DEATH_TARGET_EMSCRIPTEN)
	bool FileSystem::MountAsPersistent(StringView path)
	{
		// It's calling asynchronous API synchronously, so it can block main thread for a while
		std::int32_t result = __asyncjs__MountAsPersistent(path.data(), path.size());
		if (!result) {
#	if defined(DEATH_TRACE_VERBOSE_IO)
			LOGW("MountAsPersistent(\"{}\") failed", path);
#	endif
			return false;
		}
		return true;
	}

	void FileSystem::SyncToPersistent()
	{
		EM_ASM({
			FS.syncfs(false, function(err) {
				// Don't wait for completion, it should take ~1 second, so it doesn't matter
			});
		});
	}
#endif

	std::unique_ptr<Stream> FileSystem::Open(StringView path, FileAccess mode)
	{
#if defined(DEATH_TARGET_ANDROID)
		if (auto strippedPath = AndroidAssetStream::TryGetAssetPath(path)) {
			return std::make_unique<AndroidAssetStream>(strippedPath, mode);
		}
#endif
		return std::make_unique<FileStream>(path, mode);
	}

#if defined(DEATH_TARGET_ANDROID) || defined(DEATH_TARGET_APPLE) || defined(DEATH_TARGET_UNIX) || (defined(DEATH_TARGET_WINDOWS) && !defined(DEATH_TARGET_WINDOWS_RT))
	void FileSystem::MapDeleter::operator()(const char* const data, const std::size_t size)
	{
#	if defined(DEATH_TARGET_ANDROID) || defined(DEATH_TARGET_APPLE) || defined(DEATH_TARGET_UNIX)
		if (data != nullptr) ::munmap(const_cast<char*>(data), size);
		if (_fd != 0) ::close(_fd);
#	elif defined(DEATH_TARGET_WINDOWS) && !defined(DEATH_TARGET_WINDOWS_RT)
		if (data != nullptr) ::UnmapViewOfFile(data);
		if (_hMap != nullptr) ::CloseHandle(_hMap);
		if (_hFile != NULL) ::CloseHandle(_hFile);
		static_cast<void>(size);
#	endif
	}

	std::optional<Array<char, FileSystem::MapDeleter>> FileSystem::OpenAsMemoryMapped(StringView path, FileAccess mode)
	{
#	if defined(DEATH_TARGET_ANDROID) || defined(DEATH_TARGET_APPLE) || defined(DEATH_TARGET_UNIX)
		int flags, prot;
		switch (mode) {
			case FileAccess::Read:
				flags = O_RDONLY;
				prot = PROT_READ;
				break;
			case FileAccess::ReadWrite:
				flags = O_RDWR;
				prot = PROT_READ | PROT_WRITE;
				break;
			default:
#		if defined(DEATH_TRACE_VERBOSE_IO)
				LOGE("Failed to open file \"{}\" because of invalid mode ({})", path, std::uint32_t(mode));
#		endif
				return {};
		}

		const std::int32_t fd = ::open(String::nullTerminatedView(path).data(), flags);
		if (fd == -1) {
#		if defined(DEATH_TRACE_VERBOSE_IO)
			LOGE("Failed to open file \"{}\" with error {}{}", path, errno, __GetUnixErrorSuffix(errno));
#		endif
			return {};
		}

		// Explicitly fail if opening directories for reading on Unix to prevent silent errors
		struct stat sb;
		if (::fstat(fd, &sb) == 0 && S_ISDIR(sb.st_mode)) {
#		if defined(DEATH_TRACE_VERBOSE_IO)
			LOGE("Cannot open directory \"{}\" as memory-mapped file", path);
#		endif
			::close(fd);
			return {};
		}

		const off_t currentPos = ::lseek(fd, 0, SEEK_CUR);
		const std::size_t size = ::lseek(fd, 0, SEEK_END);
		::lseek(fd, currentPos, SEEK_SET);

		// Can't call mmap() with a zero size, so if the file is empty just set the pointer to null - but for consistency keep
		// the fd open and let it be handled by the deleter. Array guarantees that deleter gets called even in case of a null data.
		char* data;
		if (size == 0) {
			data = nullptr;
		} else if ((data = reinterpret_cast<char*>(::mmap(nullptr, size, prot, MAP_SHARED, fd, 0))) == MAP_FAILED) {
			::close(fd);
			return {};
		}

		return Array<char, MapDeleter>{data, size, MapDeleter{fd}};
#	elif defined(DEATH_TARGET_WINDOWS) && !defined(DEATH_TARGET_WINDOWS_RT)
		DWORD fileDesiredAccess, shareMode, protect, mapDesiredAccess;
		switch (mode) {
			case FileAccess::Read:
				fileDesiredAccess = GENERIC_READ;
				shareMode = FILE_SHARE_READ;
				protect = PAGE_READONLY;
				mapDesiredAccess = FILE_MAP_READ;
				break;
			case FileAccess::ReadWrite:
				fileDesiredAccess = GENERIC_READ | GENERIC_WRITE;
				shareMode = FILE_SHARE_READ | FILE_SHARE_WRITE;
				protect = PAGE_READWRITE;
				mapDesiredAccess = FILE_MAP_ALL_ACCESS;
				break;
			default:
#		if defined(DEATH_TRACE_VERBOSE_IO)
				LOGE("Failed to open file \"{}\" because of invalid mode ({})", path, std::uint32_t(mode));
#		endif
				return {};
		}

		SmallVector<wchar_t, MAX_PATH + 1> pathW(path.size() + 1);
		Utf8::ToUtf16(pathW.data(), std::int32_t(pathW.size()), path.data(), std::int32_t(path.size()));
		HANDLE hFile = ::CreateFileW(pathW.data(), fileDesiredAccess, shareMode, nullptr, OPEN_EXISTING, 0, nullptr);
		if (hFile == INVALID_HANDLE_VALUE) {
#		if defined(DEATH_TRACE_VERBOSE_IO)
			DWORD error = ::GetLastError();
			LOGE("Failed to open file \"{}\" with error 0x{:.8x}{}", path, error, __GetWin32ErrorSuffix(error));
#		endif
			return {};
		}

		const std::size_t size = ::GetFileSize(hFile, nullptr);

		// Can't call CreateFileMapping() with a zero size, so if the file is empty just set the pointer to null -- but for consistency keep
		// the handle open and let it be handled by the deleter. Array guarantees that deleter gets called even in case of a null data.
		HANDLE hMap;
		char* data;
		if (size == 0) {
			hMap = {};
			data = nullptr;
		} else {
			if (!(hMap = ::CreateFileMappingW(hFile, nullptr, protect, 0, 0, nullptr))) {
#		if defined(DEATH_TRACE_VERBOSE_IO)
				DWORD error = ::GetLastError();
				LOGE("Failed to open file \"{}\" with error 0x{:.8x}{}", path, error, __GetWin32ErrorSuffix(error));
#		endif
				::CloseHandle(hFile);
				return {};
			}

			if (!(data = reinterpret_cast<char*>(::MapViewOfFile(hMap, mapDesiredAccess, 0, 0, 0)))) {
#		if defined(DEATH_TRACE_VERBOSE_IO)
				DWORD error = ::GetLastError();
				LOGE("Failed to open file \"{}\" with error 0x{:.8x}{}", path, error, __GetWin32ErrorSuffix(error));
#		endif
				::CloseHandle(hMap);
				::CloseHandle(hFile);
				return {};
			}
		}

		return Array<char, MapDeleter>{data, size, MapDeleter{hFile, hMap}};
#	endif
	}
#endif

}}
