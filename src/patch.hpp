#ifndef BG3_AE_MACOS_PATCH_HPP
#define BG3_AE_MACOS_PATCH_HPP

#include "pch.hpp"

#include "error.hpp"
#include "mach-o.hpp"


namespace bg3ae
{
   /**
    * The server only grants achievements while every loaded module is a standard (Larian) one. Each gated function loops over the loaded
    * modules and bails out at the first one for which ls::ModuleShortDesc::IsStandardModule() returns false:
    *
    *    bl   ls::ModuleShortDesc::IsStandardModule
    *    tbz  w0, #0, <non-standard module found>
    *
    * Patching swaps that single call for 'mov w0, #1' at each site, so the loop sees only standard modules. IsStandardModule() itself and
    * every other caller (mod browser, savegame checks, load order) stay untouched.
    */
   inline constexpr std::uint32_t patched_instruction{ 0x5280'0020 }; // mov w0, #1

   enum class SiteState : std::uint8_t { original, patched };
   enum class PatchState : std::uint8_t { off, on, partial };

   struct PatchSite
   {
      std::string_view function{};
      std::uint64_t address{ 0 };
      std::uint64_t file_offset{ 0 };
      std::uint32_t original_instruction{ 0 };
      SiteState state{ SiteState::original };
   };

   struct Analysis
   {
      std::array<PatchSite, 3> sites{};

      [[nodiscard]] auto state( ) const noexcept -> PatchState;
   };

   /**
    * Locates the three call sites from the executable's symbol table and checks the instructions around each one. Any surprise (missing
    * symbol, zero or several candidate sites in a function, unexpected bytes) is reported as an error, so an unknown build is never touched.
    */
   [[nodiscard]] auto analyze( macho::Arm64Image const& image ) -> Result<Analysis>;
}


#endif //!BG3_AE_MACOS_PATCH_HPP
