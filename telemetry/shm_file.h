#pragma once

#include <filesystem>   // For std::filesystem::path
#include <fcntl.h>      // For open(), O_RDWR, O_CREAT, etc.
#include <unistd.h>     // For close(), ftruncate()
#include <sys/mman.h>   // For mmap(), munmap(), msync(), PROT_*, MAP_*
#include <sys/stat.h>   // For fstat(), struct stat
#include <cstring>      // For std::strerror
#include <iostream>     // For std::cout, std::cerr
#include <stdexcept>    // For std::runtime_error
#include <string>       // For std::string, std::to_string
#include <cerrno>       // For errno
#include <new>          // For placement new: new (ptr) ObjType(...)

namespace hprof
{
    enum class OpenFilePolicy {CreateNew, ReuseIfExists};

	template<typename ObjType>
	class ShmFile final
	{
		//static_assert(alignof(ObjType) <= alignof(std::max_align_t), "ObjType alignment is too large for mmap");
		static_assert(sizeof(ObjType) % alignof(ObjType) == 0, "ObjType size must be multiple size of it's alignment");
	public:
		ShmFile() = default;
		template <typename... Ts>
		ShmFile(std::filesystem::path filename, OpenFilePolicy opf, Ts&&... args)
		{
			struct RAII final
			{
				int _fd{ -1 };
                void* _beginAddr{nullptr};
				~RAII(){
                    if (_fd != -1) { close(_fd); }
                    if (_beginAddr != nullptr) { munmap(_beginAddr, sizeof(ObjType)); }
                }
			};
			RAII raii;

			bool isCreator = true;
			if (opf == OpenFilePolicy::CreateNew)
			{
				raii._fd = ::open(filename.c_str(), O_RDWR | O_CREAT | O_TRUNC, 0644);
			}
			else if (opf == OpenFilePolicy::ReuseIfExists)
			{
				raii._fd = ::open(filename.c_str(), O_RDWR | O_CREAT | O_EXCL, 0644);
			}
			else
			{
				throw std::runtime_error{"Invalid OpenFilePolicy: " + std::to_string(static_cast<int>(opf))};
			}

			if (raii._fd == -1)
			{
				if (errno == EEXIST)
				{
                    isCreator = false;
    				raii._fd = ::open(filename.c_str(), O_RDWR);
				}
			}
			if (raii._fd == -1)
			{
				const auto err{ errno };
				throw std::runtime_error{"FAILED to open " + filename.string() + ", errno: " + std::strerror(err)};
			}

			if (isCreator)
			{
				if (::ftruncate(raii._fd, sizeof(ObjType)) == -1)
				{
					const auto err{ errno };
					throw std::runtime_error{"FAILED to ftruncate " + filename.string() + ", errno: " + std::strerror(err)};
				}
			}

			raii._beginAddr = mmap(nullptr, sizeof(ObjType), PROT_READ | PROT_WRITE, MAP_SHARED, raii._fd, 0);
			if (raii._beginAddr == MAP_FAILED)
			{
				const auto err{ errno };
				throw std::runtime_error{"FAILED to mmap " + filename.string() + ", errno: " + std::strerror(err)};
			}

			if (isCreator)
			{
				_objPtr = new (raii._beginAddr) ObjType(std::forward<Ts>(args)...);
			}
			else
			{
                struct stat st;
                if (fstat(raii._fd, &st) == -1)
                {
                    const auto err{errno};
                    throw std::runtime_error{"FAILED to fstat " + filename.string() + ", errno: " + std::strerror(err)};
                }
                if (st.st_size != static_cast<off_t>(sizeof(ObjType)))
                {
                    throw std::runtime_error{"File size mismatch: " + filename.string()};
                }

				_objPtr = reinterpret_cast<ObjType*>(raii._beginAddr);
				if (!_objPtr->verifyMagic())
				{
					_objPtr = nullptr;
					throw std::runtime_error{"FAILED to verify magic, corrupted file:  " + filename.string()};
				}
			}

    		std::cout << "Success to create shmFile: " << filename << ", addr: " << raii._beginAddr << std::endl;
            raii._beginAddr = nullptr;
		}

		~ShmFile()
		{
			if (_objPtr != nullptr)
			{
    			if (-1 == msync(_objPtr, sizeof(ObjType), MS_SYNC))
				{
					const auto err{ errno };
					std::cerr << __FILE__ << ':' << __LINE__
						<< " FAILED to msync addr: " << static_cast<void*>(_objPtr) << ", errno: " << std::strerror(err) << std::endl;
				}
				if (-1 == munmap(_objPtr, sizeof(ObjType)))
				{
					const auto err{ errno };
					std::cerr << __FILE__ << ':' << __LINE__
						<< " FAILED to munmap addr: " << static_cast<void*>(_objPtr) << ", errno: " << std::strerror(err) << std::endl;
				}
			}
		}

        explicit operator bool() const noexcept { return _objPtr != nullptr; }
		ObjType& get() {return *_objPtr;}
        const ObjType& get() const { return *_objPtr; }

        ShmFile(ShmFile&&) = delete;
		ShmFile& operator=(ShmFile&&) = delete;
		ShmFile(ShmFile&) = delete;
		ShmFile& operator=(ShmFile&) = delete;

		private:
		ObjType* _objPtr{nullptr};
	};
} // namespace hprof