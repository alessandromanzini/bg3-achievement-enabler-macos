#ifndef BG3_AE_MACOS_MACH_O_HPP
#define BG3_AE_MACOS_MACH_O_HPP

#include "pch.hpp"

#include "error.hpp"


namespace bg3ae::macho
{
   struct Segment
   {
      std::string name{};
      std::uint64_t vm_addr{ 0 };
      std::uint64_t vm_size{ 0 };
      std::uint64_t file_offset{ 0 };
      std::uint64_t file_size{ 0 };
   };

   struct Range
   {
      std::uint64_t begin{ 0 };
      std::uint64_t end{ 0 };

      [[nodiscard]] auto contains( std::uint64_t const address ) const noexcept -> bool { return address >= begin && address < end; }
   };

   struct Symbol
   {
      std::string_view name{};
      std::uint64_t address{ 0 };
   };


   /**
    * View over the arm64 code of a Mach-O executable, either a thin arm64 file or the arm64 slice of a universal binary.
    *
    * @warning The image references the bytes it was parsed from; they must outlive it.
    */
   class Arm64Image final
   {
   public:
      [[nodiscard]] static auto parse( std::span<std::byte const> file ) -> Result<Arm64Image>;

      /** LC_UUID of the arm64 slice. It identifies the build and survives both patching and re-signing. */
      [[nodiscard]] auto uuid( ) const noexcept -> std::string const& { return uuid_; }

      /** Address range of a function in __TEXT,__text, ending at the next symbol. */
      [[nodiscard]] auto find_function( std::string_view mangled_name ) const -> std::optional<Range>;

      /** Absolute offset of a virtual address in the file that was parsed (slice offset included). */
      [[nodiscard]] auto file_offset( std::uint64_t vm_addr ) const noexcept -> std::optional<std::uint64_t>;

      [[nodiscard]] auto read_u32( std::uint64_t vm_addr ) const noexcept -> std::optional<std::uint32_t>;

   private:
      std::span<std::byte const> slice_{};
      std::uint64_t slice_offset_{ 0 };
      std::string uuid_{};
      std::vector<Segment> segments_{};
      Range text_{};
      std::vector<Symbol> text_symbols_{};

      Arm64Image( ) = default;
      [[nodiscard]] auto slice_offset_of( std::uint64_t vm_addr ) const noexcept -> std::optional<std::uint64_t>;
   };
}


#endif //!BG3_AE_MACOS_MACH_O_HPP
