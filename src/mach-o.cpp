#include <mach-o/fat.h>
#include <mach-o/loader.h>
#include <mach-o/nlist.h>

#include "mach-o.hpp"


namespace bg3ae::macho
{
   namespace
   {
      struct Slice
      {
         std::uint64_t offset{ 0 };
         std::uint64_t size{ 0 };
      };
   }

   namespace
   {
      template <typename T> [[nodiscard]] auto load( std::span<std::byte const> const bytes, std::uint64_t const offset ) noexcept -> std::optional<T>
      {
         if ( offset > bytes.size( ) || bytes.size( ) - offset < sizeof( T ) )
         {
            return std::nullopt;
         }
         //
         T value{};
         std::memcpy( &value, bytes.data( ) + offset, sizeof( T ) );
         return value;
      }


      [[nodiscard]] auto fixed_name( char const ( &raw )[16] ) -> std::string { return std::string{ raw, ::strnlen( raw, sizeof( raw ) ) }; }


      [[nodiscard]] auto format_uuid( std::uint8_t const ( &raw )[16] ) -> std::string
      {
         std::string text{};
         //
         for ( std::size_t i{ 0 }; i < sizeof( raw ); ++i )
         {
            if ( i == 4 || i == 6 || i == 8 || i == 10 )
            {
               text += '-';
            }
            text += std::format( "{:02X}", raw[i] );
         }
         //
         return text;
      }


      [[nodiscard]] auto find_arm64_slice( std::span<std::byte const> const file ) -> Result<Slice>
      {
         auto const magic = load<std::uint32_t>( file, 0 );
         //
         if ( not magic )
         {
            return fail<ErrorType::invalid_binary>( "File is too small to be a Mach-O executable." );
         }
         //
         if ( *magic == MH_MAGIC_64 )
         {
            if ( auto const header = load<mach_header_64>( file, 0 ); not header || header->cputype != CPU_TYPE_ARM64 )
            {
               return fail<ErrorType::arch_mismatch>( "Executable is not arm64." );
            }
            return Slice{ 0, file.size( ) };
         }
         //
         // Universal headers are big-endian, so their magic reads byte-swapped on this host.
         if ( *magic != FAT_CIGAM && *magic != FAT_CIGAM_64 )
         {
            return fail<ErrorType::invalid_binary>( "File is not a Mach-O executable." );
         }
         //
         bool const is_wide{ *magic == FAT_CIGAM_64 };
         auto const header = load<fat_header>( file, 0 );
         if ( not header )
         {
            return fail<ErrorType::invalid_binary>( "Truncated universal header." );
         }
         //
         std::uint32_t const count{ std::byteswap( header->nfat_arch ) };
         std::uint64_t cursor{ sizeof( fat_header ) };
         for ( std::uint32_t i{ 0 }; i < count; ++i )
         {
            Slice slice{};
            cpu_type_t cpu_type{};
            //
            if ( is_wide )
            {
               auto const arch = load<fat_arch_64>( file, cursor );
               //
               if ( not arch )
               {
                  return fail<ErrorType::invalid_binary>( "Truncated universal header." );
               }
               //
               cpu_type = std::byteswap( arch->cputype );
               slice = { std::byteswap( arch->offset ), std::byteswap( arch->size ) };
               cursor += sizeof( fat_arch_64 );
            }
            else
            {
               auto const arch = load<fat_arch>( file, cursor );
               //
               if ( not arch )
               {
                  return fail<ErrorType::invalid_binary>( "Truncated universal header." );
               }
               //
               cpu_type = std::byteswap( arch->cputype );
               slice = { std::byteswap( arch->offset ), std::byteswap( arch->size ) };
               cursor += sizeof( fat_arch );
            }
            //
            if ( cpu_type == CPU_TYPE_ARM64 )
            {
               if ( slice.offset > file.size( ) || file.size( ) - slice.offset < slice.size )
               {
                  return fail<ErrorType::invalid_binary>( "Arm64 slice lies outside the file." );
               }
               return slice;
            }
         }
         //
         return fail<ErrorType::arch_mismatch>( "Universal binary has no arm64 slice." );
      }
   }


   auto Arm64Image::parse( std::span<std::byte const> const file ) -> Result<Arm64Image>
   {
      auto const slice = find_arm64_slice( file );
      //
      if ( not slice )
      {
         return std::unexpected{ slice.error( ) };
      }
      //
      Arm64Image image{};
      image.slice_ = file.subspan( slice->offset, slice->size );
      image.slice_offset_ = slice->offset;
      //
      auto const header = load<mach_header_64>( image.slice_, 0 );
      if ( not header || header->magic != MH_MAGIC_64 || header->cputype != CPU_TYPE_ARM64 )
      {
         return fail<ErrorType::invalid_binary>( "Arm64 slice has an invalid Mach-O header." );
      }
      //
      std::optional<symtab_command> sym_tab{};
      std::uint64_t cursor{ sizeof( mach_header_64 ) };
      std::uint64_t const commands_end{ cursor + header->sizeofcmds };
      //
      for ( std::uint32_t i{ 0 }; i < header->ncmds; ++i )
      {
         auto const command = load<load_command>( image.slice_, cursor );
         if ( not command || command->cmdsize < sizeof( load_command ) || cursor + command->cmdsize > commands_end )
         {
            return fail<ErrorType::invalid_binary>( "Malformed load commands." );
         }
         //
         switch ( command->cmd )
         {
            case LC_SEGMENT_64:
            {
               auto const segment = load<segment_command_64>( image.slice_, cursor );
               if ( not segment )
               {
                  return fail<ErrorType::invalid_binary>( "Truncated segment command." );
               }
               image.segments_.push_back(
                 Segment{ fixed_name( segment->segname ), segment->vmaddr, segment->vmsize, segment->fileoff, segment->filesize } );
               //
               for ( std::uint32_t s{ 0 }; s < segment->nsects; ++s )
               {
                  auto const section = load<section_64>( image.slice_, cursor + sizeof( segment_command_64 ) + s * sizeof( section_64 ) );
                  if ( not section )
                  {
                     return fail<ErrorType::invalid_binary>( "Truncated section header." );
                  }
                  if ( fixed_name( section->segname ) == SEG_TEXT && fixed_name( section->sectname ) == SECT_TEXT )
                  {
                     image.text_ = { section->addr, section->addr + section->size };
                  }
               }
               break;
            }
            case LC_SYMTAB: sym_tab = load<symtab_command>( image.slice_, cursor ); break;
            case LC_UUID:
               if ( auto const uuid = load<uuid_command>( image.slice_, cursor ) )
               {
                  image.uuid_ = format_uuid( uuid->uuid );
               }
               break;
            default: break;
         }
         cursor += command->cmdsize;
      }
      //
      if ( image.text_.begin == image.text_.end )
      {
         return fail<ErrorType::invalid_binary>( "No __TEXT,__text section." );
      }
      // Valid Mach-O files may lack these, but without them there is nothing to identify the build or find the patch sites by.
      if ( image.uuid_.empty( ) )
      {
         return fail<ErrorType::unsupported_build>( "Executable has no LC_UUID load command." );
      }
      if ( not sym_tab || sym_tab->nsyms == 0 )
      {
         return fail<ErrorType::unsupported_build>( "Executable has no symbol table." );
      }
      if ( std::uint64_t{ sym_tab->symoff } + std::uint64_t{ sym_tab->nsyms } * sizeof( nlist_64 ) > image.slice_.size( ) ||
           std::uint64_t{ sym_tab->stroff } + sym_tab->strsize > image.slice_.size( ) )
      {
         return fail<ErrorType::invalid_binary>( "Symbol table lies outside the arm64 slice." );
      }
      //
      auto const strings = image.slice_.subspan( sym_tab->stroff, sym_tab->strsize );
      for ( std::uint32_t i{ 0 }; i < sym_tab->nsyms; ++i )
      {
         auto const entry = load<nlist_64>( image.slice_, sym_tab->symoff + std::uint64_t{ i } * sizeof( nlist_64 ) );
         if ( ( entry->n_type & N_STAB ) != 0 || ( entry->n_type & N_TYPE ) != N_SECT || not image.text_.contains( entry->n_value ) ||
              entry->n_un.n_strx >= strings.size( ) )
         {
            continue;
         }
         //
         auto const* name = reinterpret_cast<char const*>( strings.data( ) + entry->n_un.n_strx );
         std::size_t const length{ ::strnlen( name, strings.size( ) - entry->n_un.n_strx ) };
         image.text_symbols_.push_back(
           Symbol{
              std::string_view{ name, length },
              entry->n_value
         } );
      }
      std::ranges::sort( image.text_symbols_, {}, &Symbol::address );
      //
      return image;
   }


   auto Arm64Image::find_function( std::string_view const mangled_name ) const -> std::optional<Range>
   {
      auto const symbol = std::ranges::find( text_symbols_, mangled_name, &Symbol::name );
      if ( symbol == text_symbols_.end( ) )
      {
         return std::nullopt;
      }
      //
      auto const next = std::ranges::upper_bound( text_symbols_, symbol->address, {}, &Symbol::address );
      return Range{ symbol->address, next == text_symbols_.end( ) ? text_.end : next->address };
   }


   auto Arm64Image::file_offset( std::uint64_t const vm_addr ) const noexcept -> std::optional<std::uint64_t>
   {
      auto const offset = slice_offset_of( vm_addr );
      return offset ? std::optional{ slice_offset_ + *offset } : std::nullopt;
   }


   auto Arm64Image::read_u32( std::uint64_t const vm_addr ) const noexcept -> std::optional<std::uint32_t>
   {
      auto const offset = slice_offset_of( vm_addr );
      return offset ? load<std::uint32_t>( slice_, *offset ) : std::nullopt;
   }


   auto Arm64Image::slice_offset_of( std::uint64_t const vm_addr ) const noexcept -> std::optional<std::uint64_t>
   {
      for ( auto const& segment : segments_ )
      {
         if ( vm_addr >= segment.vm_addr && vm_addr - segment.vm_addr < segment.file_size )
         {
            return segment.file_offset + ( vm_addr - segment.vm_addr );
         }
      }
      return std::nullopt;
   }
}
