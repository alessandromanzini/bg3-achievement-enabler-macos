#include "patch.hpp"


namespace bg3ae
{
   namespace
   {
      struct Target
      {
         std::string_view function{};
         std::string_view symbol{};
      };

      constexpr std::string_view is_standard_module_symbol{ "__ZNK2ls15ModuleShortDesc16IsStandardModuleEv" };

      constexpr std::array targets{
         Target{          "esv::AchievementManager::UnlockAchievement",
                "__ZN3esv18AchievementManager17UnlockAchievementERKN2ls11_StringViewIcEEPKNS_9CharacterE"                                             },
         Target{ "esv::AchievementManager::IncreaseAchievementCounter",
                "__ZN3esv18AchievementManager26IncreaseAchievementCounterEPKNS_9CharacterERKN2ls11FixedStringEtb"                                     },
         Target{         "esv::OsirisGameFunctions::UnlockAchievement", "__ZN3esv19OsirisGameFunctions17UnlockAchievementEPN3osi18ExecutionParameterE" },
      };

      // The checks sit in each function's prologue; there is no reason to scan further.
      constexpr std::uint64_t max_scan_bytes{ 0x1000 };
   }

   namespace
   {
      [[nodiscard]] auto encode_bl( std::uint64_t const from, std::uint64_t const to ) noexcept -> std::optional<std::uint32_t>
      {
         auto const delta = static_cast<std::int64_t>( to - from );
         if ( delta % 4 != 0 || delta < -( std::int64_t{ 1 } << 27 ) || delta >= ( std::int64_t{ 1 } << 27 ) )
         {
            return std::nullopt;
         }
         //
         return 0x9400'0000u | ( static_cast<std::uint32_t>( delta >> 2 ) & 0x03FF'FFFFu );
      }


      [[nodiscard]] constexpr auto is_tbz_w0_bit0( std::uint32_t const instruction ) noexcept -> bool
      {
         return ( instruction & 0xFFF8'001Fu ) == 0x3600'0000u;
      }


      [[nodiscard]] auto find_site( macho::Arm64Image const& image, Target const& target, std::uint64_t const is_standard_module )
        -> Result<PatchSite>
      {
         auto const function = image.find_function( target.symbol );
         if ( not function )
         {
            return fail<ErrorType::unsupported_build>( "Symbol for {} not found.", target.function );
         }
         //
         std::vector<PatchSite> candidates{};
         std::uint64_t const end{ std::min( function->end, function->begin + max_scan_bytes ) };
         for ( std::uint64_t address{ function->begin }; address + 8 <= end; address += 4 )
         {
            auto const instruction = image.read_u32( address );
            auto const next = image.read_u32( address + 4 );
            auto const call = encode_bl( address, is_standard_module );
            if ( not instruction || not next || not call || not is_tbz_w0_bit0( *next ) )
            {
               continue;
            }
            //
            if ( *instruction == *call || *instruction == patched_instruction )
            {
               auto const file_offset = image.file_offset( address );
               if ( not file_offset )
               {
                  return fail<ErrorType::unsupported_build>( "{}: call site at {:#x} has no file offset.", target.function, address );
               }
               candidates.push_back(
                 PatchSite{ target.function, address, *file_offset, *call, *instruction == *call ? SiteState::original : SiteState::patched } );
            }
         }
         //
         if ( candidates.size( ) != 1 )
         {
            return fail<ErrorType::unsupported_build>(
              "{}: expected exactly one IsStandardModule check, found {}.", target.function, candidates.size( ) );
         }
         return candidates.front( );
      }
   }


   auto Analysis::state( ) const noexcept -> PatchState
   {
      auto const patched = std::ranges::count( sites, SiteState::patched, &PatchSite::state );
      if ( patched == 0 )
      {
         return PatchState::off;
      }
      //
      return patched == std::ssize( sites ) ? PatchState::on : PatchState::partial;
   }


   auto analyze( macho::Arm64Image const& image ) -> Result<Analysis>
   {
      auto const is_standard_module = image.find_function( is_standard_module_symbol );
      if ( not is_standard_module )
      {
         return fail<ErrorType::unsupported_build>( "Symbol for ls::ModuleShortDesc::IsStandardModule not found." );
      }
      //
      Analysis analysis{};
      for ( std::size_t i{ 0 }; i < targets.size( ); ++i )
      {
         auto const site = find_site( image, targets[i], is_standard_module->begin );
         if ( not site )
         {
            return std::unexpected{ site.error( ) };
         }
         analysis.sites[i] = *site;
      }
      //
      return analysis;
   }
}
