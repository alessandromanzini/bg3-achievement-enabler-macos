#ifndef BG3_AE_MACOS_INSTALL_HPP
#define BG3_AE_MACOS_INSTALL_HPP

#include "pch.hpp"

#include "error.hpp"


namespace bg3ae
{
   struct GameInstall
   {
      std::string store{};
      std::filesystem::path executable{};
   };

   struct InstructionWrite
   {
      std::uint64_t file_offset{ 0 };
      std::uint32_t instruction{ 0 };
   };

   /**
    * Finds the game executable. Explicit paths win; otherwise the Steam libraries listed in libraryfolders.vdf are searched first, then the
    * usual GOG locations.
    */
   [[nodiscard]] auto locate_game( std::optional<std::filesystem::path> const& app, std::optional<std::filesystem::path> const& executable )
     -> Result<GameInstall>;

   [[nodiscard]] auto is_running( std::filesystem::path const& executable ) -> bool;

   [[nodiscard]] auto default_backup_root( ) -> std::filesystem::path;
   [[nodiscard]] auto backup_file( std::filesystem::path const& root, std::string_view uuid, std::filesystem::path const& executable )
     -> std::filesystem::path;

   /**
    * Copies the executable into root/<uuid>/ unless that backup already exists. Backups of other builds are deleted, since they can never be
    * restored onto this build. Returns whether a new backup was written.
    */
   [[nodiscard]] auto ensure_backup( std::filesystem::path const& executable, std::filesystem::path const& root, std::string_view uuid )
     -> Result<bool>;

   /**
    * Writes source, plus the given instruction writes, over target without touching target in place: the copy is staged next to target,
    * optionally ad-hoc signed as a standalone Mach-O, verified, then renamed over target. A fresh inode keeps the kernel from matching the
    * new bytes against the old file's cached signature.
    */
   [[nodiscard]] auto
     install_copy( std::filesystem::path const& source, std::filesystem::path const& target, std::span<InstructionWrite const> writes, bool sign )
       -> Result<>;
}


#endif //!BG3_AE_MACOS_INSTALL_HPP
