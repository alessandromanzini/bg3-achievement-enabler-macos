#include "install.hpp"
#include "mach-o.hpp"
#include "mapped_file.hpp"
#include "patch.hpp"


namespace
{
   namespace fs = std::filesystem;
   using namespace bg3ae;

   enum ExitCode : int { ok = 0, error = 1, unsupported = 2 };
   enum class Command : std::uint8_t { help, status, on, off, restore };

   struct Options
   {
      Command command{ Command::help };
      std::optional<fs::path> app{};
      std::optional<fs::path> executable{};
      fs::path backup_root{ default_backup_root( ) };
   };

   struct LoadedGame
   {
      GameInstall install{};
      MappedFile file;
      macho::Arm64Image image;
   };

   struct ExePair
   {
      MappedFile file;
      macho::Arm64Image image;
   };
}

namespace
{
   auto print_usage( ) -> void
   {
      std::println(
        "bg3-achievement-enabler-macos {} - keeps Baldur's Gate 3 achievements enabled with mods (macOS, Apple Silicon)\n"
        "\n"
        "usage: bg3ae-macos <command> [options]\n"
        "\n"
        "commands:\n"
        "  status    show the game build and the state of each patch site\n"
        "  on        enable achievements with mods (backs up the original executable first)\n"
        "  off       put the three original instructions back (the executable stays ad-hoc signed)\n"
        "  restore   copy the backed-up original executable back, Larian's signature included\n"
        "\n"
        "options:\n"
        "  --app <path>         Baldur's Gate 3.app to use (default: auto-detect Steam, then GOG)\n"
        "  --exe <path>         game executable to use directly\n"
        "  --backup-dir <path>  backup location (default: {})",
        BG3AE_VERSION, default_backup_root( ).string( ) );
   }


   /** Prints a failure the way its category calls for and picks the exit code. */
   auto report( Error const& error ) -> ExitCode
   {
      switch ( error.type )
      {
         case ErrorType::usage:
            std::println( stderr, "error: {}\n", error.message );
            print_usage( );
            return ExitCode::error;

         case ErrorType::unsupported_build:
            std::println( "Patch:  UNKNOWN BUILD / BYTES" );
            std::println( stderr, "error: {}\nThis game build is not supported. Nothing was changed.", error.message );
            return ExitCode::unsupported;

         default: std::println( stderr, "error: {}", error.message ); return ExitCode::error;
      }
   }


   [[nodiscard]] auto parse_options( std::span<char* const> const args ) -> Result<Options>
   {
      Options options{};
      if ( args.size( ) < 2 )
      {
         return options;
      }
      //
      std::string_view const command{ args[1] };
      if ( command == "status" )
      {
         options.command = Command::status;
      }
      else if ( command == "on" )
      {
         options.command = Command::on;
      }
      else if ( command == "off" )
      {
         options.command = Command::off;
      }
      else if ( command == "restore" )
      {
         options.command = Command::restore;
      }
      else if ( command == "help" || command == "--help" || command == "-h" )
      {
         return options;
      }
      else
      {
         return fail<ErrorType::usage>( "Unknown command '{}'.", command );
      }
      //
      for ( std::size_t i{ 2 }; i < args.size( ); ++i )
      {
         std::string_view const flag{ args[i] };
         if ( i + 1 >= args.size( ) )
         {
            return fail<ErrorType::usage>( "'{}' needs a value.", flag );
         }
         //
         fs::path value{ args[++i] };
         if ( flag == "--app" )
         {
            options.app = std::move( value );
         }
         else if ( flag == "--exe" )
         {
            options.executable = std::move( value );
         }
         else if ( flag == "--backup-dir" )
         {
            options.backup_root = std::move( value );
         }
         else
         {
            return fail<ErrorType::usage>( "Unknown option '{}'.", flag );
         }
      }
      //
      return options;
   }


   [[nodiscard]] auto load_executable( fs::path const& path ) -> Result<ExePair>
   {
      auto file = MappedFile::open( path );
      if ( not file )
      {
         return std::unexpected{ file.error( ) };
      }
      //
      // The image points into the mapping, which stays put when the MappedFile object is moved.
      auto image = macho::Arm64Image::parse( file->bytes( ) );
      if ( not image )
      {
         return with_context( image.error( ), "Cannot read '{}'.", path.string( ) );
      }
      //
      return ExePair{ std::move( *file ), std::move( *image ) };
   }


   [[nodiscard]] auto load_game( Options const& options ) -> Result<LoadedGame>
   {
      auto install = locate_game( options.app, options.executable );
      if ( not install )
      {
         return std::unexpected{ install.error( ) };
      }
      //
      auto loaded = load_executable( install->executable );
      if ( not loaded )
      {
         return std::unexpected{ loaded.error( ) };
      }
      //
      return LoadedGame{ std::move( *install ), std::move( loaded->file ), std::move( loaded->image ) };
   }


   [[nodiscard]] auto state_name( PatchState const state ) -> std::string_view
   {
      switch ( state )
      {
         case PatchState::on     : return "ON";
         case PatchState::off    : return "OFF";
         case PatchState::partial: return "PARTIAL";
      }
      return "?";
   }


   /** Re-reads the executable from disk and checks it reached the expected state. */
   [[nodiscard]] auto confirm_state( fs::path const& executable, PatchState const expected ) -> Result<>
   {
      auto const loaded = load_executable( executable );
      if ( not loaded )
      {
         return std::unexpected{ loaded.error( ) };
      }
      //
      // Not an unsupported build at this point: the executable was just written, so 'nothing was changed' would be a lie.
      auto const analysis = analyze( loaded->image );
      if ( not analysis )
      {
         return fail<ErrorType::verification>( "Cannot re-read the written executable. {}", analysis.error( ).message );
      }
      if ( analysis->state( ) != expected )
      {
         return fail<ErrorType::verification>(
           "Executable is {} after writing, expected {}.", state_name( analysis->state( ) ), state_name( expected ) );
      }
      //
      return {};
   }


   auto print_header( LoadedGame const& game ) -> void
   {
      std::println( "Game:   {} ({})", game.install.executable.string( ), game.install.store );
      std::println( "Build:  arm64 {}", game.image.uuid( ) );
   }


   [[nodiscard]] auto ensure_not_running( fs::path const& executable ) -> Result<>
   {
      if ( is_running( executable ) )
      {
         return fail<ErrorType::game_running>( "Baldur's Gate 3 is running; quit it completely first." );
      }
      return {};
   }
}

namespace
{
   [[nodiscard]] auto run_status( Options const& options ) -> Result<>
   {
      auto const game = load_game( options );
      if ( not game )
      {
         return std::unexpected{ game.error( ) };
      }
      print_header( *game );
      //
      auto const analysis = analyze( game->image );
      if ( not analysis )
      {
         return std::unexpected{ analysis.error( ) };
      }
      //
      for ( auto const& site : analysis->sites )
      {
         std::println(
           "  {:<10} {} (address {:#x}, file offset {:#x})", site.state == SiteState::patched ? "[patched]" : "[original]", site.function,
           site.address, site.file_offset );
      }
      //
      fs::path const backup{ backup_file( options.backup_root, game->image.uuid( ), game->install.executable ) };
      std::error_code error{};
      std::println( "Backup: {}", fs::is_regular_file( backup, error ) ? backup.string( ) : "none for this build" );
      std::println( "Patch:  {}", state_name( analysis->state( ) ) );
      //
      return {};
   }


   [[nodiscard]] auto run_on( Options const& options ) -> Result<>
   {
      auto const game = load_game( options );
      if ( not game )
      {
         return std::unexpected{ game.error( ) };
      }
      print_header( *game );
      //
      auto const analysis = analyze( game->image );
      if ( not analysis )
      {
         return std::unexpected{ analysis.error( ) };
      }
      if ( analysis->state( ) == PatchState::on )
      {
         std::println( "Achievement patch: ON (already)" );
         return {};
      }
      if ( auto const idle = ensure_not_running( game->install.executable ); not idle )
      {
         return idle;
      }
      //
      fs::path const& executable{ game->install.executable };
      if ( analysis->state( ) == PatchState::off )
      {
         auto const created = ensure_backup( executable, options.backup_root, game->image.uuid( ) );
         if ( not created )
         {
            return with_context( created.error( ), "Backup failed, nothing was changed." );
         }
         if ( *created )
         {
            std::println( "Backed up the original executable to {}", backup_file( options.backup_root, game->image.uuid( ), executable ).string( ) );
         }
      }
      else
      {
         std::println( stderr, "warning: the executable is already partially patched; no new backup is made." );
      }
      //
      std::vector<InstructionWrite> writes{};
      for ( auto const& site : analysis->sites )
      {
         if ( site.state == SiteState::original )
         {
            writes.push_back( InstructionWrite{ site.file_offset, patched_instruction } );
         }
      }
      //
      if ( auto const installed = install_copy( executable, executable, writes, true ); not installed )
      {
         return installed;
      }
      if ( auto const confirmed = confirm_state( executable, PatchState::on ); not confirmed )
      {
         return confirmed;
      }
      std::println( "Achievement patch: ON" );
      //
      return {};
   }


   [[nodiscard]] auto run_off( Options const& options ) -> Result<>
   {
      auto const game = load_game( options );
      if ( not game )
      {
         return std::unexpected{ game.error( ) };
      }
      print_header( *game );
      //
      auto const analysis = analyze( game->image );
      if ( not analysis )
      {
         return std::unexpected{ analysis.error( ) };
      }
      if ( analysis->state( ) == PatchState::off )
      {
         std::println( "Achievement patch: OFF (already)" );
         return {};
      }
      if ( auto const idle = ensure_not_running( game->install.executable ); not idle )
      {
         return idle;
      }
      //
      std::vector<InstructionWrite> writes{};
      for ( auto const& site : analysis->sites )
      {
         if ( site.state == SiteState::patched )
         {
            writes.push_back( InstructionWrite{ site.file_offset, site.original_instruction } );
         }
      }
      //
      fs::path const& executable{ game->install.executable };
      if ( auto const installed = install_copy( executable, executable, writes, true ); not installed )
      {
         return installed;
      }
      if ( auto const confirmed = confirm_state( executable, PatchState::off ); not confirmed )
      {
         return confirmed;
      }
      std::println( "Achievement patch: OFF (run 'restore' to bring back Larian's original signature too)" );
      //
      return {};
   }


   [[nodiscard]] auto run_restore( Options const& options ) -> Result<>
   {
      auto const game = load_game( options );
      if ( not game )
      {
         return std::unexpected{ game.error( ) };
      }
      print_header( *game );
      //
      fs::path const& executable{ game->install.executable };
      fs::path const backup{ backup_file( options.backup_root, game->image.uuid( ), executable ) };
      if ( std::error_code error{}; not fs::is_regular_file( backup, error ) )
      {
         return fail<ErrorType::backup>( "No backup for build {} in {}.", game->image.uuid( ), options.backup_root.string( ) );
      }
      //
      // Never put back something that is not an unpatched copy of this exact build.
      auto const original = load_executable( backup );
      if ( not original )
      {
         return with_context( original.error( ), "Backup is unreadable." );
      }
      if ( original->image.uuid( ) != game->image.uuid( ) )
      {
         return fail<ErrorType::backup>( "Backup belongs to build {}, not {}.", original->image.uuid( ), game->image.uuid( ) );
      }
      if ( auto const analysis = analyze( original->image ); not analysis || analysis->state( ) != PatchState::off )
      {
         return fail<ErrorType::backup>( "Backup at {} is not an unpatched executable.", backup.string( ) );
      }
      if ( auto const idle = ensure_not_running( executable ); not idle )
      {
         return idle;
      }
      //
      if ( auto const installed = install_copy( backup, executable, {}, false ); not installed )
      {
         return installed;
      }
      if ( auto const confirmed = confirm_state( executable, PatchState::off ); not confirmed )
      {
         return confirmed;
      }
      std::println( "Restored the original executable from {}", backup.string( ) );
      std::println( "Achievement patch: OFF" );
      //
      return {};
   }


   [[nodiscard]] auto run( Options const& options ) -> Result<>
   {
      switch ( options.command )
      {
         case Command::status : return run_status( options );
         case Command::on     : return run_on( options );
         case Command::off    : return run_off( options );
         case Command::restore: return run_restore( options );
         case Command::help   : break;
      }
      print_usage( );
      return {};
   }
}


auto main( int const argc, char* argv[] ) -> int
{
   try
   {
      auto const result = parse_options( std::span{ argv, static_cast<std::size_t>( argc ) } ).and_then( run );
      return result ? ExitCode::ok : report( result.error( ) );
   }
   catch ( std::exception const& exception )
   {
      std::println( stderr, "error: {}", exception.what( ) );
      return ExitCode::error;
   }
}
