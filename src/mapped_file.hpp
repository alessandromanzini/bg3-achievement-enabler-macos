#ifndef BG3_AE_MACOS_MAPPED_FILE_HPP
#define BG3_AE_MACOS_MAPPED_FILE_HPP

#include "pch.hpp"

#include "error.hpp"


namespace bg3ae
{
   /**
    * Read-only memory mapping of a whole file. The mapping keeps the original inode alive, so it stays valid even after the file is
    * replaced on disk.
    */
   class MappedFile final
   {
   public:
      [[nodiscard]] static auto open( std::filesystem::path const& path ) -> Result<MappedFile>;

      MappedFile( MappedFile const& ) = delete;
      MappedFile( MappedFile&& other ) noexcept;
      ~MappedFile( ) noexcept;

      auto operator=( MappedFile const& ) -> MappedFile& = delete;
      auto operator=( MappedFile&& other ) noexcept -> MappedFile&;

      [[nodiscard]] auto bytes( ) const noexcept -> std::span<std::byte const>;

   private:
      void* data_{ nullptr };
      std::size_t size_{ 0 };

      MappedFile( void* data, std::size_t size ) noexcept;
      auto release( ) noexcept -> void;
   };
}


#endif //!BG3_AE_MACOS_MAPPED_FILE_HPP
