#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

#include "mapped_file.hpp"


namespace bg3ae
{
   auto MappedFile::open( std::filesystem::path const& path ) -> Result<MappedFile>
   {
      int const fd{ ::open( path.c_str( ), O_RDONLY | O_CLOEXEC ) };
      if ( fd < 0 )
      {
         return fail<ErrorType::io>( "Cannot open '{}': {}.", path.string( ), std::strerror( errno ) );
      }
      //
      struct stat info{};
      if ( ::fstat( fd, &info ) != 0 )
      {
         int const error{ errno };
         ::close( fd );
         return fail<ErrorType::io>( "Cannot stat '{}': {}.", path.string( ), std::strerror( error ) );
      }
      if ( info.st_size <= 0 )
      {
         ::close( fd );
         return fail<ErrorType::io>( "'{}' is empty.", path.string( ) );
      }
      //
      auto const size = static_cast<std::size_t>( info.st_size );
      void* data{ ::mmap( nullptr, size, PROT_READ, MAP_PRIVATE, fd, 0 ) };
      int const error{ errno };
      ::close( fd );
      if ( data == MAP_FAILED )
      {
         return fail<ErrorType::io>( "Cannot map '{}': {}.", path.string( ), std::strerror( error ) );
      }
      //
      return MappedFile{ data, size };
   }


   MappedFile::MappedFile( MappedFile&& other ) noexcept
      : data_{ std::exchange( other.data_, nullptr ) }
      , size_{ std::exchange( other.size_, 0 ) }
   { }


   MappedFile::~MappedFile( ) noexcept { release( ); }


   auto MappedFile::operator=( MappedFile&& other ) noexcept -> MappedFile&
   {
      if ( this != &other )
      {
         release( );
         data_ = std::exchange( other.data_, nullptr );
         size_ = std::exchange( other.size_, 0 );
      }
      return *this;
   }


   auto MappedFile::bytes( ) const noexcept -> std::span<std::byte const> { return { static_cast<std::byte const*>( data_ ), size_ }; }


   MappedFile::MappedFile( void* data, std::size_t const size ) noexcept
      : data_{ data }
      , size_{ size }
   { }


   auto MappedFile::release( ) noexcept -> void
   {
      if ( data_ != nullptr )
      {
         ::munmap( data_, size_ );
         data_ = nullptr;
         size_ = 0;
      }
   }
}
