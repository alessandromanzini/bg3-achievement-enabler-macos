#ifndef BG3_AE_MACOS_ERROR_HPP
#define BG3_AE_MACOS_ERROR_HPP

#include "pch.hpp"


namespace bg3ae
{
   /**
    * Broad category of a failure. The message carries the details; the category decides the exit code and how the failure is reported.
    */
   enum class ErrorType : std::uint8_t {
      usage,             // Bad command line.
      game_not_found,    // No game executable at the given or auto-detected paths.
      game_running,      // The game is open, so its executable must not be replaced.
      io,                // A file system or system call failed.
      invalid_binary,    // Not a Mach-O executable, or a malformed one.
      arch_mismatch,     // The executable has no arm64 code.
      unsupported_build, // The executable does not look like a build the patch understands. Nothing was written.
      backup,            // No backup for this build, or one that does not match it.
      signing,           // codesign could not run, sign or verify.
      verification       // The executable did not reach the expected state after writing.
   };
}

namespace bg3ae
{
   struct Error
   {
      ErrorType type{};
      std::string message{};
   };

   template <typename T = void> using Result = std::expected<T, Error>;

   template <ErrorType err, typename... Args> [[nodiscard]] auto fail( std::format_string<Args...> fmt, Args&&... args ) -> std::unexpected<Error>
   {
      return std::unexpected{
         Error{ .type = err, .message = std::format( fmt, std::forward<Args>( args )... ) }
      };
   }

   /**
    * Keeps the category of an error and puts some context in front of its message.
    */
   template <typename... Args>
   [[nodiscard]] auto with_context( Error const& error, std::format_string<Args...> fmt, Args&&... args ) -> std::unexpected<Error>
   {
      return std::unexpected{
         Error{ .type = error.type, .message = std::format( "{} {}", std::format( fmt, std::forward<Args>( args )... ), error.message ) }
      };
   }
}


#endif //!BG3_AE_MACOS_ERROR_HPP
