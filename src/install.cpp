#include <copyfile.h>
#include <fcntl.h>
#include <libproc.h>
#include <pwd.h>
#include <spawn.h>
#include <sys/wait.h>
#include <unistd.h>

#include "install.hpp"


extern char** environ;


namespace bg3ae
{
   namespace
   {
      namespace fs = std::filesystem;

      constexpr std::string_view app_name{ "Baldur's Gate 3.app" };
      constexpr std::string_view executable_name{ "Baldur's Gate 3" };
      constexpr std::string_view codesign{ "/usr/bin/codesign" };

      /** Removes the staged file on every path that does not reach the final rename. */
      class StagingGuard final
      {
      public:
         explicit StagingGuard( fs::path path )
            : path_{ std::move( path ) }
         { }
         StagingGuard( StagingGuard const& ) = delete;
         auto operator=( StagingGuard const& ) -> StagingGuard& = delete;
         ~StagingGuard( ) noexcept
         {
            if ( armed_ )
            {
               std::error_code ignored{};
               fs::remove( path_, ignored );
            }
         }

         auto dismiss( ) noexcept -> void { armed_ = false; }

      private:
         fs::path path_{};
         bool armed_{ true };
      };
   }

   namespace
   {
      [[nodiscard]] auto home_directory( ) -> fs::path
      {
         if ( char const* home{ std::getenv( "HOME" ) }; home != nullptr && *home != '\0' )
         {
            return home;
         }
         if ( passwd const* entry{ ::getpwuid( ::getuid( ) ) }; entry != nullptr )
         {
            return entry->pw_dir;
         }
         //
         return fs::current_path( );
      }


      [[nodiscard]] auto executable_in( fs::path const& app ) -> fs::path { return app / "Contents" / "MacOS" / executable_name; }


      [[nodiscard]] auto steam_libraries( ) -> std::vector<fs::path>
      {
         fs::path const steam{ home_directory( ) / "Library" / "Application Support" / "Steam" };
         std::vector<fs::path> libraries{ steam };
         //
         std::ifstream file{ steam / "steamapps" / "libraryfolders.vdf" };
         std::stringstream contents{};
         contents << file.rdbuf( );
         std::string const text{ contents.str( ) };
         //
         std::regex const path_entry{ R"re("path"\s+"([^"]+)")re" };
         for ( auto it = std::sregex_iterator{ text.begin( ), text.end( ), path_entry }; it != std::sregex_iterator{}; ++it )
         {
            fs::path library{ ( *it )[1].str( ) };
            if ( std::ranges::find( libraries, library ) == libraries.end( ) )
            {
               libraries.push_back( std::move( library ) );
            }
         }
         //
         return libraries;
      }


      [[nodiscard]] auto run( std::vector<std::string> const& arguments ) -> Result<>
      {
         std::vector<char*> argv{};
         for ( auto const& argument : arguments )
         {
            argv.push_back( const_cast<char*>( argument.c_str( ) ) );
         }
         argv.push_back( nullptr );
         //
         // Keep our own output ahead of the child's.
         std::fflush( stdout );
         pid_t pid{};
         if ( int const error{ ::posix_spawn( &pid, argv.front( ), nullptr, nullptr, argv.data( ), environ ) }; error != 0 )
         {
            return fail<ErrorType::signing>( "Cannot run {}: {}.", arguments.front( ), std::strerror( error ) );
         }
         //
         int status{};
         while ( ::waitpid( pid, &status, 0 ) < 0 )
         {
            if ( errno != EINTR )
            {
               return fail<ErrorType::signing>( "Cannot wait for {}: {}.", arguments.front( ), std::strerror( errno ) );
            }
         }
         if ( not WIFEXITED( status ) || WEXITSTATUS( status ) != 0 )
         {
            return fail<ErrorType::signing>( "'{} {}' failed.", arguments.front( ), arguments.size( ) > 1 ? arguments[1] : "" );
         }
         //
         return {};
      }


      [[nodiscard]] auto clone_file( fs::path const& source, fs::path const& target ) -> Result<>
      {
         std::error_code ignored{};
         fs::remove( target, ignored );
         //
         // Clones on APFS (instant, copy-on-write) and falls back to a full copy elsewhere. Keeps mode and extended attributes.
         if ( ::copyfile( source.c_str( ), target.c_str( ), nullptr, COPYFILE_CLONE ) != 0 )
         {
            return fail<ErrorType::io>( "Cannot copy '{}' to '{}': {}.", source.string( ), target.string( ), std::strerror( errno ) );
         }
         //
         return {};
      }


      [[nodiscard]] auto write_instructions( fs::path const& file, std::span<InstructionWrite const> const writes ) -> Result<>
      {
         int const fd{ ::open( file.c_str( ), O_WRONLY | O_CLOEXEC ) };
         if ( fd < 0 )
         {
            return fail<ErrorType::io>( "Cannot open '{}' for writing: {}.", file.string( ), std::strerror( errno ) );
         }
         //
         for ( auto const& write : writes )
         {
            // arm64 instructions are little-endian, like the host.
            auto const bytes = std::bit_cast<std::array<std::byte, 4>>( write.instruction );
            if ( ::pwrite( fd, bytes.data( ), bytes.size( ), static_cast<off_t>( write.file_offset ) ) != static_cast<ssize_t>( bytes.size( ) ) )
            {
               int const error{ errno };
               ::close( fd );
               return fail<ErrorType::io>( "Cannot write at offset {:#x}: {}.", write.file_offset, std::strerror( error ) );
            }
         }
         //
         if ( ::fsync( fd ) != 0 )
         {
            int const error{ errno };
            ::close( fd );
            return fail<ErrorType::io>( "Cannot flush '{}': {}.", file.string( ), std::strerror( error ) );
         }
         ::close( fd );
         //
         return {};
      }


      [[nodiscard]] auto looks_like_uuid( std::string const& name ) -> bool
      {
         static std::regex const pattern{ "^[0-9A-F]{8}-[0-9A-F]{4}-[0-9A-F]{4}-[0-9A-F]{4}-[0-9A-F]{12}$" };
         return std::regex_match( name, pattern );
      }
   }


   auto locate_game( std::optional<fs::path> const& app, std::optional<fs::path> const& executable ) -> Result<GameInstall>
   {
      auto const checked = []( std::string store, fs::path path ) -> Result<GameInstall> {
         std::error_code error{};
         if ( not fs::is_regular_file( path, error ) )
         {
            return fail<ErrorType::game_not_found>( "Game executable not found at '{}'.", path.string( ) );
         }
         return GameInstall{ std::move( store ), std::move( path ) };
      };
      //
      if ( executable )
      {
         return checked( "custom", *executable );
      }
      if ( app )
      {
         return checked( "custom", executable_in( *app ) );
      }
      //
      std::vector<GameInstall> candidates{};
      for ( auto const& library : steam_libraries( ) )
      {
         candidates.push_back( GameInstall{ "Steam", executable_in( library / "steamapps" / "common" / "Baldurs Gate 3" / app_name ) } );
      }
      candidates.push_back( GameInstall{ "GOG", executable_in( fs::path{ "/Applications" } / app_name ) } );
      candidates.push_back( GameInstall{ "GOG", executable_in( home_directory( ) / "Applications" / app_name ) } );
      //
      for ( auto& candidate : candidates )
      {
         std::error_code error{};
         if ( fs::is_regular_file( candidate.executable, error ) )
         {
            return std::move( candidate );
         }
      }
      //
      return fail<ErrorType::game_not_found>( "No Steam or GOG installation found; pass --app <path to Baldur's Gate 3.app>." );
   }


   auto is_running( fs::path const& executable ) -> bool
   {
      std::error_code error{};
      fs::path const wanted{ fs::weakly_canonical( executable, error ) };
      //
      std::vector<pid_t> pids{};
      pids.resize( static_cast<std::size_t>( std::max( ::proc_listallpids( nullptr, 0 ), 0 ) ) + 64 );
      int const count{ ::proc_listallpids( pids.data( ), static_cast<int>( pids.size( ) * sizeof( pid_t ) ) ) };
      //
      std::array<char, PROC_PIDPATHINFO_MAXSIZE> buffer{};
      for ( int i{ 0 }; i < count; ++i )
      {
         if ( ::proc_pidpath( pids[static_cast<std::size_t>( i )], buffer.data( ), static_cast<std::uint32_t>( buffer.size( ) ) ) > 0 &&
              fs::weakly_canonical( fs::path{ buffer.data( ) }, error ) == wanted )
         {
            return true;
         }
      }
      //
      return false;
   }


   auto default_backup_root( ) -> fs::path
   {
      return home_directory( ) / "Library" / "Application Support" / "bg3-achievement-enabler-macos" / "backups";
   }


   auto backup_file( fs::path const& root, std::string_view const uuid, fs::path const& executable ) -> fs::path
   {
      return root / uuid / executable.filename( );
   }


   auto ensure_backup( fs::path const& executable, fs::path const& root, std::string_view const uuid ) -> Result<bool>
   {
      fs::path const backup{ backup_file( root, uuid, executable ) };
      std::error_code error{};
      if ( fs::is_regular_file( backup, error ) )
      {
         return false;
      }
      //
      fs::create_directories( backup.parent_path( ), error );
      if ( error )
      {
         return fail<ErrorType::io>( "Cannot create '{}': {}.", backup.parent_path( ).string( ), error.message( ) );
      }
      //
      for ( auto const& entry : fs::directory_iterator{ root, error } )
      {
         std::string const name{ entry.path( ).filename( ).string( ) };
         if ( entry.is_directory( ) && name != uuid && looks_like_uuid( name ) )
         {
            fs::remove_all( entry.path( ), error );
         }
      }
      //
      fs::path partial{ backup };
      partial += ".partial";
      if ( auto const copied = clone_file( executable, partial ); not copied )
      {
         return std::unexpected{ copied.error( ) };
      }
      fs::rename( partial, backup, error );
      if ( error )
      {
         return fail<ErrorType::io>( "Cannot finish backup '{}': {}.", backup.string( ), error.message( ) );
      }
      //
      return true;
   }


   auto install_copy( fs::path const& source, fs::path const& target, std::span<InstructionWrite const> const writes, bool const sign ) -> Result<>
   {
      // Staged under a different name than CFBundleExecutable, so codesign signs it as a standalone Mach-O instead of resealing the whole
      // bundle (which would hash the ~150 GB Data folder).
      fs::path const staging{ target.parent_path( ) / std::format( ".{}.bg3ae-staging", target.filename( ).string( ) ) };
      StagingGuard guard{ staging };
      //
      if ( auto const copied = clone_file( source, staging ); not copied )
      {
         return copied;
      }
      if ( not writes.empty( ) )
      {
         if ( auto const written = write_instructions( staging, writes ); not written )
         {
            return written;
         }
      }
      if ( sign )
      {
         // Larian's Developer ID signature cannot survive a byte change; an ad-hoc one keeps the identifier and is all the kernel needs
         // to run arm64 code. The game has no hardened runtime, so its Larian-signed libraries still load.
         if ( auto const signed_copy =
                run( { std::string{ codesign }, "--force", "--sign", "-", "--preserve-metadata=identifier,entitlements,flags", staging.string( ) } );
              not signed_copy )
         {
            return signed_copy;
         }
         if ( auto const verified = run( { std::string{ codesign }, "--verify", "--strict", staging.string( ) } ); not verified )
         {
            return verified;
         }
      }
      //
      std::error_code error{};
      fs::rename( staging, target, error );
      if ( error )
      {
         return fail<ErrorType::io>( "Cannot replace '{}': {}.", target.string( ), error.message( ) );
      }
      guard.dismiss( );
      //
      return {};
   }
}
