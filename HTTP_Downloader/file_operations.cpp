/*
	HTTP Downloader can download files through HTTP(S) and FTP(S) connections.
	Copyright (C) 2015-2019 Eric Kutcher

	This program is free software: you can redistribute it and/or modify
	it under the terms of the GNU General Public License as published by
	the Free Software Foundation, either version 3 of the License, or
	(at your option) any later version.

	This program is distributed in the hope that it will be useful,
	but WITHOUT ANY WARRANTY; without even the implied warranty of
	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
	GNU General Public License for more details.

	You should have received a copy of the GNU General Public License
	along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/

#include "globals.h"

#include "lite_shell32.h"
#include "lite_ole32.h"
#include "lite_gdi32.h"

#include "file_operations.h"
#include "utilities.h"

#include "ftp_parsing.h"
#include "connection.h"

wchar_t *UTF8StringToWideString( char *utf8_string, int string_length )
{
	int wide_val_length = MultiByteToWideChar( CP_UTF8, 0, utf8_string, string_length, NULL, 0 );	// Include the NULL terminator.
	wchar_t *wide_val = ( wchar_t * )GlobalAlloc( GMEM_FIXED, sizeof( wchar_t ) * wide_val_length );
	MultiByteToWideChar( CP_UTF8, 0, utf8_string, string_length, wide_val, wide_val_length );

	return wide_val;
}

char *WideStringToUTF8String( wchar_t *wide_string, int *utf8_string_length, int buffer_offset )
{
	*utf8_string_length = WideCharToMultiByte( CP_UTF8, 0, wide_string, -1, NULL, 0, NULL, NULL ) + buffer_offset;
	char *utf8_val = ( char * )GlobalAlloc( GPTR, sizeof( char ) * *utf8_string_length ); // Size includes the NULL character.
	*utf8_string_length = WideCharToMultiByte( CP_UTF8, 0, wide_string, -1, utf8_val + buffer_offset, *utf8_string_length - buffer_offset, NULL, NULL );

	return utf8_val;
}

char read_config()
{
	char ret_status = 0;

	_wmemcpy_s( base_directory + base_directory_length, MAX_PATH - base_directory_length, L"\\http_downloader_settings\0", 26 );
	base_directory[ base_directory_length + 25 ] = 0;	// Sanity.

	HANDLE hFile_cfg = CreateFile( base_directory, GENERIC_READ, 0, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL );
	if ( hFile_cfg != INVALID_HANDLE_VALUE )
	{
		DWORD read = 0, pos = 0;
		DWORD fz = GetFileSize( hFile_cfg, NULL );

		int reserved;

		// Our config file is going to be small. If it's something else, we're not going to read it.
		if ( fz >= 1024 && fz < 10240 )
		{
			char *cfg_buf = ( char * )GlobalAlloc( GMEM_FIXED, sizeof( char ) * fz + 1 );

			ReadFile( hFile_cfg, cfg_buf, sizeof( char ) * fz, &read, NULL );

			cfg_buf[ fz ] = 0;	// Guarantee a NULL terminated buffer.

			// Read the config. It must be in the order specified below.
			if ( read == fz && _memcmp( cfg_buf, MAGIC_ID_SETTINGS, 4 ) == 0 )
			{
				reserved = 1024 - 588;

				char *next = cfg_buf + 4;

				// Main Window

				_memcpy_s( &cfg_pos_x, sizeof( int ), next, sizeof( int ) );
				next += sizeof( int );
				_memcpy_s( &cfg_pos_y, sizeof( int ), next, sizeof( int ) );
				next += sizeof( int );
				_memcpy_s( &cfg_width, sizeof( int ), next, sizeof( int ) );
				next += sizeof( int );
				_memcpy_s( &cfg_height, sizeof( int ), next, sizeof( int ) );
				next += sizeof( int );

				_memcpy_s( &cfg_min_max, sizeof( unsigned char ), next, sizeof( unsigned char ) );
				next += sizeof( unsigned char );

				_memcpy_s( &cfg_column_width1, sizeof( int ), next, sizeof( int ) );
				next += sizeof( int );
				_memcpy_s( &cfg_column_width2, sizeof( int ), next, sizeof( int ) );
				next += sizeof( int );
				_memcpy_s( &cfg_column_width3, sizeof( int ), next, sizeof( int ) );
				next += sizeof( int );
				_memcpy_s( &cfg_column_width4, sizeof( int ), next, sizeof( int ) );
				next += sizeof( int );
				_memcpy_s( &cfg_column_width5, sizeof( int ), next, sizeof( int ) );
				next += sizeof( int );
				_memcpy_s( &cfg_column_width6, sizeof( int ), next, sizeof( int ) );
				next += sizeof( int );
				_memcpy_s( &cfg_column_width7, sizeof( int ), next, sizeof( int ) );
				next += sizeof( int );
				_memcpy_s( &cfg_column_width8, sizeof( int ), next, sizeof( int ) );
				next += sizeof( int );
				_memcpy_s( &cfg_column_width9, sizeof( int ), next, sizeof( int ) );
				next += sizeof( int );
				_memcpy_s( &cfg_column_width10, sizeof( int ), next, sizeof( int ) );
				next += sizeof( int );
				_memcpy_s( &cfg_column_width11, sizeof( int ), next, sizeof( int ) );
				next += sizeof( int );
				_memcpy_s( &cfg_column_width12, sizeof( int ), next, sizeof( int ) );
				next += sizeof( int );
				_memcpy_s( &cfg_column_width13, sizeof( int ), next, sizeof( int ) );
				next += sizeof( int );
				_memcpy_s( &cfg_column_width14, sizeof( int ), next, sizeof( int ) );
				next += sizeof( int );
				_memcpy_s( &cfg_column_width15, sizeof( int ), next, sizeof( int ) );
				next += sizeof( int );

				_memcpy_s( &cfg_column_order1, sizeof( char ), next, sizeof( char ) );
				next += sizeof( char );
				_memcpy_s( &cfg_column_order2, sizeof( char ), next, sizeof( char ) );
				next += sizeof( char );
				_memcpy_s( &cfg_column_order3, sizeof( char ), next, sizeof( char ) );
				next += sizeof( char );
				_memcpy_s( &cfg_column_order4, sizeof( char ), next, sizeof( char ) );
				next += sizeof( char );
				_memcpy_s( &cfg_column_order5, sizeof( char ), next, sizeof( char ) );
				next += sizeof( char );
				_memcpy_s( &cfg_column_order6, sizeof( char ), next, sizeof( char ) );
				next += sizeof( char );
				_memcpy_s( &cfg_column_order7, sizeof( char ), next, sizeof( char ) );
				next += sizeof( char );
				_memcpy_s( &cfg_column_order8, sizeof( char ), next, sizeof( char ) );
				next += sizeof( char );
				_memcpy_s( &cfg_column_order9, sizeof( char ), next, sizeof( char ) );
				next += sizeof( char );
				_memcpy_s( &cfg_column_order10, sizeof( char ), next, sizeof( char ) );
				next += sizeof( char );
				_memcpy_s( &cfg_column_order11, sizeof( char ), next, sizeof( char ) );
				next += sizeof( char );
				_memcpy_s( &cfg_column_order12, sizeof( char ), next, sizeof( char ) );
				next += sizeof( char );
				_memcpy_s( &cfg_column_order13, sizeof( char ), next, sizeof( char ) );
				next += sizeof( char );
				_memcpy_s( &cfg_column_order14, sizeof( char ), next, sizeof( char ) );
				next += sizeof( char );
				_memcpy_s( &cfg_column_order15, sizeof( char ), next, sizeof( char ) );
				next += sizeof( char );

				_memcpy_s( &cfg_show_column_headers, sizeof( bool ), next, sizeof( bool ) );
				next += sizeof( bool );

				_memcpy_s( &cfg_sorted_column_index, sizeof( int ), next, sizeof( int ) );
				next += sizeof( int );

				_memcpy_s( &cfg_sorted_direction, sizeof( unsigned char ), next, sizeof( unsigned char ) );
				next += sizeof( unsigned char );

				_memcpy_s( &cfg_show_toolbar, sizeof( bool ), next, sizeof( bool ) );
				next += sizeof( bool );

				_memcpy_s( &cfg_show_status_bar, sizeof( bool ), next, sizeof( bool ) );
				next += sizeof( bool );

				_memcpy_s( &cfg_t_down_speed, sizeof( unsigned char ), next, sizeof( unsigned char ) );
				next += sizeof( unsigned char );
				_memcpy_s( &cfg_t_downloaded, sizeof( unsigned char ), next, sizeof( unsigned char ) );
				next += sizeof( unsigned char );
				_memcpy_s( &cfg_t_file_size, sizeof( unsigned char ), next, sizeof( unsigned char ) );
				next += sizeof( unsigned char );
				_memcpy_s( &cfg_t_speed_limit, sizeof( unsigned char ), next, sizeof( unsigned char ) );
				next += sizeof( unsigned char );

				_memcpy_s( &cfg_t_status_downloaded, sizeof( unsigned char ), next, sizeof( unsigned char ) );
				next += sizeof( unsigned char );
				_memcpy_s( &cfg_t_status_down_speed, sizeof( unsigned char ), next, sizeof( unsigned char ) );
				next += sizeof( unsigned char );
				_memcpy_s( &cfg_t_status_speed_limit, sizeof( unsigned char ), next, sizeof( unsigned char ) );
				next += sizeof( unsigned char );

				// Global Speed Limit

				_memcpy_s( &cfg_download_speed_limit, sizeof( unsigned long long ), next, sizeof( unsigned long long ) );
				next += sizeof( unsigned long long );

				// Options General

				_memcpy_s( &cfg_tray_icon, sizeof( bool ), next, sizeof( bool ) );
				next += sizeof( bool );
				_memcpy_s( &cfg_minimize_to_tray, sizeof( bool ), next, sizeof( bool ) );
				next += sizeof( bool );
				_memcpy_s( &cfg_close_to_tray, sizeof( bool ), next, sizeof( bool ) );
				next += sizeof( bool );
				_memcpy_s( &cfg_start_in_tray, sizeof( bool ), next, sizeof( bool ) );
				next += sizeof( bool );
				_memcpy_s( &cfg_show_notification, sizeof( bool ), next, sizeof( bool ) );
				next += sizeof( bool );
				_memcpy_s( &cfg_show_tray_progress, sizeof( bool ), next, sizeof( bool ) );
				next += sizeof( bool );

				_memcpy_s( &cfg_always_on_top, sizeof( bool ), next, sizeof( bool ) );
				next += sizeof( bool );

				_memcpy_s( &cfg_enable_drop_window, sizeof( bool ), next, sizeof( bool ) );
				next += sizeof( bool );
				_memcpy_s( &cfg_drop_window_transparency, sizeof( unsigned char ), next, sizeof( unsigned char ) );
				next += sizeof( unsigned char );
				_memcpy_s( &cfg_show_drop_window_progress, sizeof( bool ), next, sizeof( bool ) );
				next += sizeof( bool );
				_memcpy_s( &cfg_drop_pos_x, sizeof( int ), next, sizeof( int ) );
				next += sizeof( int );
				_memcpy_s( &cfg_drop_pos_y, sizeof( int ), next, sizeof( int ) );
				next += sizeof( int );

				_memcpy_s( &cfg_play_sound, sizeof( bool ), next, sizeof( bool ) );
				next += sizeof( bool );

				// Options Appearance

				_memcpy_s( &cfg_show_gridlines, sizeof( bool ), next, sizeof( bool ) );
				next += sizeof( bool );
				_memcpy_s( &cfg_show_part_progress, sizeof( bool ), next, sizeof( bool ) );
				next += sizeof( bool );
				_memcpy_s( &cfg_sort_added_and_updating_items, sizeof( bool ), next, sizeof( bool ) );
				next += sizeof( bool );

				_memcpy_s( &cfg_odd_row_font_settings.font_color, sizeof( COLORREF ), next, sizeof( COLORREF ) );
				next += sizeof( COLORREF );
				_memcpy_s( &cfg_odd_row_font_settings.lf.lfHeight, sizeof( LONG ), next, sizeof( LONG ) );
				next += sizeof( LONG );
				_memcpy_s( &cfg_odd_row_font_settings.lf.lfWeight, sizeof( LONG ), next, sizeof( LONG ) );
				next += sizeof( LONG );
				_memcpy_s( &cfg_odd_row_font_settings.lf.lfItalic, sizeof( BYTE ), next, sizeof( BYTE ) );
				next += sizeof( BYTE );
				_memcpy_s( &cfg_odd_row_font_settings.lf.lfUnderline, sizeof( BYTE ), next, sizeof( BYTE ) );
				next += sizeof( BYTE );
				_memcpy_s( &cfg_odd_row_font_settings.lf.lfStrikeOut, sizeof( BYTE ), next, sizeof( BYTE ) );
				next += sizeof( BYTE );

				_memcpy_s( &cfg_odd_row_background_color, sizeof( COLORREF ), next, sizeof( COLORREF ) );
				next += sizeof( COLORREF );
				_memcpy_s( &cfg_odd_row_highlight_color, sizeof( COLORREF ), next, sizeof( COLORREF ) );
				next += sizeof( COLORREF );
				_memcpy_s( &cfg_odd_row_highlight_font_color, sizeof( COLORREF ), next, sizeof( COLORREF ) );
				next += sizeof( COLORREF );

				_memcpy_s( &cfg_even_row_font_settings.font_color, sizeof( COLORREF ), next, sizeof( COLORREF ) );
				next += sizeof( COLORREF );
				_memcpy_s( &cfg_even_row_font_settings.lf.lfHeight, sizeof( LONG ), next, sizeof( LONG ) );
				next += sizeof( LONG );
				_memcpy_s( &cfg_even_row_font_settings.lf.lfWeight, sizeof( LONG ), next, sizeof( LONG ) );
				next += sizeof( LONG );
				_memcpy_s( &cfg_even_row_font_settings.lf.lfItalic, sizeof( BYTE ), next, sizeof( BYTE ) );
				next += sizeof( BYTE );
				_memcpy_s( &cfg_even_row_font_settings.lf.lfUnderline, sizeof( BYTE ), next, sizeof( BYTE ) );
				next += sizeof( BYTE );
				_memcpy_s( &cfg_even_row_font_settings.lf.lfStrikeOut, sizeof( BYTE ), next, sizeof( BYTE ) );
				next += sizeof( BYTE );

				_memcpy_s( &cfg_even_row_background_color, sizeof( COLORREF ), next, sizeof( COLORREF ) );
				next += sizeof( COLORREF );
				_memcpy_s( &cfg_even_row_highlight_color, sizeof( COLORREF ), next, sizeof( COLORREF ) );
				next += sizeof( COLORREF );
				_memcpy_s( &cfg_even_row_highlight_font_color, sizeof( COLORREF ), next, sizeof( COLORREF ) );
				next += sizeof( COLORREF );

				for ( unsigned char i = 0; i < NUM_COLORS; ++i )
				{
					_memcpy_s( progress_colors[ i ], sizeof( COLORREF ), next, sizeof( COLORREF ) );
					next += sizeof( COLORREF );
				}

				// Options Connection

				_memcpy_s( &cfg_max_downloads, sizeof( unsigned char ), next, sizeof( unsigned char ) );
				next += sizeof( unsigned char );

				_memcpy_s( &cfg_default_download_parts, sizeof( unsigned char ), next, sizeof( unsigned char ) );
				next += sizeof( unsigned char );

				_memcpy_s( &cfg_retry_downloads_count, sizeof( unsigned char ), next, sizeof( unsigned char ) );
				next += sizeof( unsigned char );

				_memcpy_s( &cfg_retry_parts_count, sizeof( unsigned char ), next, sizeof( unsigned char ) );
				next += sizeof( unsigned char );

				_memcpy_s( &cfg_timeout, sizeof( unsigned short ), next, sizeof( unsigned short ) );
				next += sizeof( unsigned short );

				_memcpy_s( &cfg_max_redirects, sizeof( unsigned char ), next, sizeof( unsigned char ) );
				next += sizeof( unsigned char );

				_memcpy_s( &cfg_default_speed_limit, sizeof( unsigned long long ), next, sizeof( unsigned long long ) );
				next += sizeof( unsigned long long );

				_memcpy_s( &cfg_default_ssl_version, sizeof( unsigned char ), next, sizeof( unsigned char ) );
				next += sizeof( unsigned char );

				// Options FTP

				_memcpy_s( &cfg_ftp_mode_type, sizeof( unsigned char ), next, sizeof( unsigned char ) );
				next += sizeof( unsigned char );

				_memcpy_s( &cfg_ftp_enable_fallback_mode, sizeof( bool ), next, sizeof( bool ) );
				next += sizeof( bool );

				_memcpy_s( &cfg_ftp_address_type, sizeof( unsigned char ), next, sizeof( unsigned char ) );
				next += sizeof( unsigned char );

				_memcpy_s( &cfg_ftp_ip_address, sizeof( unsigned long ), next, sizeof( unsigned long ) );
				next += sizeof( unsigned long );

				_memcpy_s( &cfg_ftp_port_start, sizeof( unsigned short ), next, sizeof( unsigned short ) );
				next += sizeof( unsigned short );

				_memcpy_s( &cfg_ftp_port_end, sizeof( unsigned short ), next, sizeof( unsigned short ) );
				next += sizeof( unsigned short );

				_memcpy_s( &cfg_ftp_send_keep_alive, sizeof( bool ), next, sizeof( bool ) );
				next += sizeof( bool );

				// Options Proxy

				_memcpy_s( &cfg_enable_proxy, sizeof( bool ), next, sizeof( bool ) );
				next += sizeof( bool );

				_memcpy_s( &cfg_address_type, sizeof( unsigned char ), next, sizeof( unsigned char ) );
				next += sizeof( unsigned char );

				_memcpy_s( &cfg_ip_address, sizeof( unsigned long ), next, sizeof( unsigned long ) );
				next += sizeof( unsigned long );

				_memcpy_s( &cfg_port, sizeof( unsigned short ), next, sizeof( unsigned short ) );
				next += sizeof( unsigned short );

				//

				_memcpy_s( &cfg_enable_proxy_s, sizeof( bool ), next, sizeof( bool ) );
				next += sizeof( bool );

				_memcpy_s( &cfg_address_type_s, sizeof( unsigned char ), next, sizeof( unsigned char ) );
				next += sizeof( unsigned char );

				_memcpy_s( &cfg_ip_address_s, sizeof( unsigned long ), next, sizeof( unsigned long ) );
				next += sizeof( unsigned long );

				_memcpy_s( &cfg_port_s, sizeof( unsigned short ), next, sizeof( unsigned short ) );
				next += sizeof( unsigned short );

				//

				_memcpy_s( &cfg_enable_proxy_socks, sizeof( bool ), next, sizeof( bool ) );
				next += sizeof( bool );

				_memcpy_s( &cfg_socks_type, sizeof( unsigned char ), next, sizeof( unsigned char ) );
				next += sizeof( unsigned char );

				_memcpy_s( &cfg_address_type_socks, sizeof( unsigned char ), next, sizeof( unsigned char ) );
				next += sizeof( unsigned char );

				_memcpy_s( &cfg_ip_address_socks, sizeof( unsigned long ), next, sizeof( unsigned long ) );
				next += sizeof( unsigned long );

				_memcpy_s( &cfg_port_socks, sizeof( unsigned short ), next, sizeof( unsigned short ) );
				next += sizeof( unsigned short );

				_memcpy_s( &cfg_use_authentication_socks, sizeof( bool ), next, sizeof( bool ) );
				next += sizeof( bool );

				_memcpy_s( &cfg_resolve_domain_names_v4a, sizeof( bool ), next, sizeof( bool ) );
				next += sizeof( bool );

				_memcpy_s( &cfg_resolve_domain_names, sizeof( bool ), next, sizeof( bool ) );
				next += sizeof( bool );

				// Options Server

				_memcpy_s( &cfg_enable_server, sizeof( bool ), next, sizeof( bool ) );
				next += sizeof( bool );

				_memcpy_s( &cfg_server_address_type, sizeof( unsigned char ), next, sizeof( unsigned char ) );
				next += sizeof( unsigned char );

				_memcpy_s( &cfg_server_ip_address, sizeof( unsigned long ), next, sizeof( unsigned long ) );
				next += sizeof( unsigned long );

				_memcpy_s( &cfg_server_port, sizeof( unsigned short ), next, sizeof( unsigned short ) );
				next += sizeof( unsigned short );

				_memcpy_s( &cfg_use_authentication, sizeof( bool ), next, sizeof( bool ) );
				next += sizeof( bool );

				_memcpy_s( &cfg_authentication_type, sizeof( unsigned char ), next, sizeof( unsigned char ) );
				next += sizeof( unsigned char );

				_memcpy_s( &cfg_server_enable_ssl, sizeof( bool ), next, sizeof( bool ) );
				next += sizeof( bool );

				_memcpy_s( &cfg_certificate_type, sizeof( unsigned char ), next, sizeof( unsigned char ) );
				next += sizeof( unsigned char );

				_memcpy_s( &cfg_server_ssl_version, sizeof( unsigned char ), next, sizeof( unsigned char ) );
				next += sizeof( unsigned char );

				// Options Advanced

				_memcpy_s( &cfg_enable_download_history, sizeof( bool ), next, sizeof( bool ) );
				next += sizeof( bool );

				_memcpy_s( &cfg_enable_quick_allocation, sizeof( bool ), next, sizeof( bool ) );
				next += sizeof( bool );

				_memcpy_s( &cfg_set_filetime, sizeof( bool ), next, sizeof( bool ) );
				next += sizeof( bool );

				_memcpy_s( &cfg_use_one_instance, sizeof( bool ), next, sizeof( bool ) );
				next += sizeof( bool );

				_memcpy_s( &cfg_prevent_standby, sizeof( bool ), next, sizeof( bool ) );
				next += sizeof( bool );

				_memcpy_s( &cfg_resume_downloads, sizeof( bool ), next, sizeof( bool ) );
				next += sizeof( bool );

				_memcpy_s( &cfg_drag_and_drop_action, sizeof( unsigned char ), next, sizeof( unsigned char ) );
				next += sizeof( unsigned char );

				_memcpy_s( &cfg_prompt_last_modified, sizeof( unsigned char ), next, sizeof( unsigned char ) );
				next += sizeof( unsigned char );

				_memcpy_s( &cfg_prompt_rename, sizeof( unsigned char ), next, sizeof( unsigned char ) );
				next += sizeof( unsigned char );

				_memcpy_s( &cfg_prompt_file_size, sizeof( unsigned char ), next, sizeof( unsigned char ) );
				next += sizeof( unsigned char );
				_memcpy_s( &cfg_max_file_size, sizeof( unsigned long long ), next, sizeof( unsigned long long ) );
				next += sizeof( unsigned long long );

				_memcpy_s( &cfg_shutdown_action, sizeof( unsigned char ), next, sizeof( unsigned char ) );
				next += sizeof( unsigned char );

				_memcpy_s( &cfg_use_temp_download_directory, sizeof( bool ), next, sizeof( bool ) );
				next += sizeof( bool );

				_memcpy_s( &cfg_thread_count, sizeof( unsigned long ), next, sizeof( unsigned long ) );
				next += sizeof( unsigned long );


				//


				next += reserved;	// Skip past reserved bytes.

				int string_length = 0;
				int cfg_val_length = 0;


				//


				// Options General

				if ( ( DWORD )( next - cfg_buf ) < read )
				{
					string_length = lstrlenA( next ) + 1;

					if ( string_length > 1 )
					{
						cfg_sound_file_path = UTF8StringToWideString( next, string_length );
					}

					next += string_length;
				}

				// Options Appearance

				if ( ( DWORD )( next - cfg_buf ) < read )
				{
					string_length = lstrlenA( next ) + 1;

					if ( string_length > 1 && string_length <= LF_FACESIZE )
					{
						cfg_val_length = MultiByteToWideChar( CP_UTF8, 0, next, string_length, NULL, 0 );	// Include the NULL terminator.
						MultiByteToWideChar( CP_UTF8, 0, next, string_length, cfg_odd_row_font_settings.lf.lfFaceName, cfg_val_length );
					}

					next += string_length;
				}

				if ( ( DWORD )( next - cfg_buf ) < read )
				{
					string_length = lstrlenA( next ) + 1;

					if ( string_length > 1 && string_length <= LF_FACESIZE )
					{
						cfg_val_length = MultiByteToWideChar( CP_UTF8, 0, next, string_length, NULL, 0 );	// Include the NULL terminator.
						MultiByteToWideChar( CP_UTF8, 0, next, string_length, cfg_even_row_font_settings.lf.lfFaceName, cfg_val_length );
					}

					next += string_length;
				}

				// Options FTP

				if ( ( DWORD )( next - cfg_buf ) < read )
				{
					string_length = lstrlenA( next ) + 1;

					cfg_ftp_hostname = UTF8StringToWideString( next, string_length );

					next += string_length;
				}

				// Options Proxy

				if ( ( DWORD )( next - cfg_buf ) < read )
				{
					string_length = lstrlenA( next ) + 1;

					cfg_hostname = UTF8StringToWideString( next, string_length );

					next += string_length;
				}

				if ( ( DWORD )( next - cfg_buf ) < read )
				{
					// Length of the string - not including the NULL character.
					_memcpy_s( &string_length, sizeof( unsigned short ), next, sizeof( unsigned short ) );
					next += sizeof( unsigned short );

					if ( string_length > 0 )
					{
						if ( ( ( DWORD )( next - cfg_buf ) + string_length < read ) )
						{
							// string_length does not contain the NULL character of the string.
							char *proxy_auth_username = ( char * )GlobalAlloc( GMEM_FIXED, sizeof( char ) * ( string_length + 1 ) );
							_memcpy_s( proxy_auth_username, string_length, next, string_length );
							proxy_auth_username[ string_length ] = 0; // Sanity;

							decode_cipher( proxy_auth_username, string_length );

							// Read username.
							cfg_proxy_auth_username = UTF8StringToWideString( proxy_auth_username, string_length + 1 );

							GlobalFree( proxy_auth_username );

							next += string_length;
						}
						else
						{
							read = 0;
						}
					}
				}

				if ( ( DWORD )( next - cfg_buf ) < read )
				{
					// Length of the string - not including the NULL character.
					_memcpy_s( &string_length, sizeof( unsigned short ), next, sizeof( unsigned short ) );
					next += sizeof( unsigned short );

					if ( string_length > 0 )
					{
						if ( ( ( DWORD )( next - cfg_buf ) + string_length < read ) )
						{
							// string_length does not contain the NULL character of the string.
							char *proxy_auth_password = ( char * )GlobalAlloc( GMEM_FIXED, sizeof( char ) * ( string_length + 1 ) );
							_memcpy_s( proxy_auth_password, string_length, next, string_length );
							proxy_auth_password[ string_length ] = 0; // Sanity;

							decode_cipher( proxy_auth_password, string_length );

							// Read password.
							cfg_proxy_auth_password = UTF8StringToWideString( proxy_auth_password, string_length + 1 );

							GlobalFree( proxy_auth_password );

							next += string_length;
						}
						else
						{
							read = 0;
						}
					}
				}

				//

				if ( ( DWORD )( next - cfg_buf ) < read )
				{
					string_length = lstrlenA( next ) + 1;

					cfg_hostname_s = UTF8StringToWideString( next, string_length );

					next += string_length;
				}

				if ( ( DWORD )( next - cfg_buf ) < read )
				{
					// Length of the string - not including the NULL character.
					_memcpy_s( &string_length, sizeof( unsigned short ), next, sizeof( unsigned short ) );
					next += sizeof( unsigned short );

					if ( string_length > 0 )
					{
						if ( ( ( DWORD )( next - cfg_buf ) + string_length < read ) )
						{
							// string_length does not contain the NULL character of the string.
							char *proxy_auth_username_s = ( char * )GlobalAlloc( GMEM_FIXED, sizeof( char ) * ( string_length + 1 ) );
							_memcpy_s( proxy_auth_username_s, string_length, next, string_length );
							proxy_auth_username_s[ string_length ] = 0; // Sanity;

							decode_cipher( proxy_auth_username_s, string_length );

							// Read username.
							cfg_proxy_auth_username_s = UTF8StringToWideString( proxy_auth_username_s, string_length + 1 );

							GlobalFree( proxy_auth_username_s );

							next += string_length;
						}
						else
						{
							read = 0;
						}
					}
				}

				if ( ( DWORD )( next - cfg_buf ) < read )
				{
					// Length of the string - not including the NULL character.
					_memcpy_s( &string_length, sizeof( unsigned short ), next, sizeof( unsigned short ) );
					next += sizeof( unsigned short );

					if ( string_length > 0 )
					{
						if ( ( ( DWORD )( next - cfg_buf ) + string_length < read ) )
						{
							// string_length does not contain the NULL character of the string.
							char *proxy_auth_password_s = ( char * )GlobalAlloc( GMEM_FIXED, sizeof( char ) * ( string_length + 1 ) );
							_memcpy_s( proxy_auth_password_s, string_length, next, string_length );
							proxy_auth_password_s[ string_length ] = 0; // Sanity;

							decode_cipher( proxy_auth_password_s, string_length );

							// Read password.
							cfg_proxy_auth_password_s = UTF8StringToWideString( proxy_auth_password_s, string_length + 1 );

							GlobalFree( proxy_auth_password_s );

							next += string_length;
						}
						else
						{
							read = 0;
						}
					}
				}

				//

				if ( ( DWORD )( next - cfg_buf ) < read )
				{
					string_length = lstrlenA( next ) + 1;

					cfg_hostname_socks = UTF8StringToWideString( next, string_length );

					next += string_length;
				}

				if ( ( DWORD )( next - cfg_buf ) < read )
				{
					// Length of the string - not including the NULL character.
					_memcpy_s( &string_length, sizeof( unsigned short ), next, sizeof( unsigned short ) );
					next += sizeof( unsigned short );

					if ( string_length > 0 )
					{
						if ( ( ( DWORD )( next - cfg_buf ) + string_length < read ) )
						{
							// string_length does not contain the NULL character of the string.
							char *proxy_auth_ident_username_socks = ( char * )GlobalAlloc( GMEM_FIXED, sizeof( char ) * ( string_length + 1 ) );
							_memcpy_s( proxy_auth_ident_username_socks, string_length, next, string_length );
							proxy_auth_ident_username_socks[ string_length ] = 0; // Sanity;

							decode_cipher( proxy_auth_ident_username_socks, string_length );

							// Read username.
							cfg_proxy_auth_ident_username_socks = UTF8StringToWideString( proxy_auth_ident_username_socks, string_length + 1 );

							GlobalFree( proxy_auth_ident_username_socks );

							next += string_length;
						}
						else
						{
							read = 0;
						}
					}
				}

				if ( ( DWORD )( next - cfg_buf ) < read )
				{
					// Length of the string - not including the NULL character.
					_memcpy_s( &string_length, sizeof( unsigned short ), next, sizeof( unsigned short ) );
					next += sizeof( unsigned short );

					if ( string_length > 0 )
					{
						if ( ( ( DWORD )( next - cfg_buf ) + string_length < read ) )
						{
							// string_length does not contain the NULL character of the string.
							char *proxy_auth_username_socks = ( char * )GlobalAlloc( GMEM_FIXED, sizeof( char ) * ( string_length + 1 ) );
							_memcpy_s( proxy_auth_username_socks, string_length, next, string_length );
							proxy_auth_username_socks[ string_length ] = 0; // Sanity;

							decode_cipher( proxy_auth_username_socks, string_length );

							// Read username.
							cfg_proxy_auth_username_socks = UTF8StringToWideString( proxy_auth_username_socks, string_length + 1 );

							GlobalFree( proxy_auth_username_socks );

							next += string_length;
						}
						else
						{
							read = 0;
						}
					}
				}

				if ( ( DWORD )( next - cfg_buf ) < read )
				{
					// Length of the string - not including the NULL character.
					_memcpy_s( &string_length, sizeof( unsigned short ), next, sizeof( unsigned short ) );
					next += sizeof( unsigned short );

					if ( string_length > 0 )
					{
						if ( ( ( DWORD )( next - cfg_buf ) + string_length < read ) )
						{
							// string_length does not contain the NULL character of the string.
							char *proxy_auth_password_socks = ( char * )GlobalAlloc( GMEM_FIXED, sizeof( char ) * ( string_length + 1 ) );
							_memcpy_s( proxy_auth_password_socks, string_length, next, string_length );
							proxy_auth_password_socks[ string_length ] = 0; // Sanity;

							decode_cipher( proxy_auth_password_socks, string_length );

							// Read password.
							cfg_proxy_auth_password_socks = UTF8StringToWideString( proxy_auth_password_socks, string_length + 1 );

							GlobalFree( proxy_auth_password_socks );

							next += string_length;
						}
						else
						{
							read = 0;
						}
					}
				}

				// Options Server

				if ( ( DWORD )( next - cfg_buf ) < read )
				{
					string_length = lstrlenA( next ) + 1;

					cfg_server_hostname = UTF8StringToWideString( next, string_length );

					next += string_length;
				}

				if ( ( DWORD )( next - cfg_buf ) < read )
				{
					// Length of the string - not including the NULL character.
					_memcpy_s( &string_length, sizeof( unsigned short ), next, sizeof( unsigned short ) );
					next += sizeof( unsigned short );

					if ( string_length > 0 )
					{
						if ( ( ( DWORD )( next - cfg_buf ) + string_length < read ) )
						{
							// string_length does not contain the NULL character of the string.
							char *authentication_username = ( char * )GlobalAlloc( GMEM_FIXED, sizeof( char ) * ( string_length + 1 ) );
							_memcpy_s( authentication_username, string_length, next, string_length );
							authentication_username[ string_length ] = 0; // Sanity;

							decode_cipher( authentication_username, string_length );

							// Read username.
							cfg_authentication_username = UTF8StringToWideString( authentication_username, string_length + 1 );

							GlobalFree( authentication_username );

							next += string_length;
						}
						else
						{
							read = 0;
						}
					}
				}

				if ( ( DWORD )( next - cfg_buf ) < read )
				{
					// Length of the string - not including the NULL character.
					_memcpy_s( &string_length, sizeof( unsigned short ), next, sizeof( unsigned short ) );
					next += sizeof( unsigned short );

					if ( string_length > 0 )
					{
						if ( ( ( DWORD )( next - cfg_buf ) + string_length < read ) )
						{
							// string_length does not contain the NULL character of the string.
							char *authentication_password = ( char * )GlobalAlloc( GMEM_FIXED, sizeof( char ) * ( string_length + 1 ) );
							_memcpy_s( authentication_password, string_length, next, string_length );
							authentication_password[ string_length ] = 0; // Sanity;

							decode_cipher( authentication_password, string_length );

							// Read password.
							cfg_authentication_password = UTF8StringToWideString( authentication_password, string_length + 1 );

							GlobalFree( authentication_password );

							next += string_length;
						}
						else
						{
							read = 0;
						}
					}
				}

				if ( ( DWORD )( next - cfg_buf ) < read )
				{
					// Length of the string - not including the NULL character.
					_memcpy_s( &string_length, sizeof( unsigned short ), next, sizeof( unsigned short ) );
					next += sizeof( unsigned short );

					if ( string_length > 0 )
					{
						if ( ( ( DWORD )( next - cfg_buf ) + string_length < read ) )
						{
							// string_length does not contain the NULL character of the string.
							char *certificate_password = ( char * )GlobalAlloc( GMEM_FIXED, sizeof( char ) * ( string_length + 1 ) );
							_memcpy_s( certificate_password, string_length, next, string_length );
							certificate_password[ string_length ] = 0; // Sanity;

							decode_cipher( certificate_password, string_length );

							// Read password.
							cfg_certificate_pkcs_password = UTF8StringToWideString( certificate_password, string_length + 1 );

							GlobalFree( certificate_password );

							next += string_length;
						}
						else
						{
							read = 0;
						}
					}
				}

				if ( ( DWORD )( next - cfg_buf ) < read )
				{
					string_length = lstrlenA( next ) + 1;

					cfg_certificate_pkcs_file_name = UTF8StringToWideString( next, string_length );

					next += string_length;
				}

				if ( ( DWORD )( next - cfg_buf ) < read )
				{
					string_length = lstrlenA( next ) + 1;

					cfg_certificate_cer_file_name = UTF8StringToWideString( next, string_length );

					next += string_length;
				}

				if ( ( DWORD )( next - cfg_buf ) < read )
				{
					string_length = lstrlenA( next ) + 1;

					cfg_certificate_key_file_name = UTF8StringToWideString( next, string_length );

					next += string_length;
				}

				// Options Advanced

				if ( ( DWORD )( next - cfg_buf ) < read )
				{
					string_length = lstrlenA( next ) + 1;

					if ( string_length > 1 )
					{
						cfg_val_length = MultiByteToWideChar( CP_UTF8, 0, next, string_length, NULL, 0 );	// Include the NULL terminator.
						cfg_default_download_directory = ( wchar_t * )GlobalAlloc( GMEM_FIXED, sizeof( wchar_t ) * cfg_val_length );
						MultiByteToWideChar( CP_UTF8, 0, next, string_length, cfg_default_download_directory, cfg_val_length );

						g_default_download_directory_length = cfg_val_length - 1;	// Store the base directory length.
					}

					next += string_length;
				}

				if ( ( DWORD )( next - cfg_buf ) < read )
				{
					string_length = lstrlenA( next ) + 1;

					if ( string_length > 1 )
					{
						cfg_val_length = MultiByteToWideChar( CP_UTF8, 0, next, string_length, NULL, 0 );	// Include the NULL terminator.
						cfg_temp_download_directory = ( wchar_t * )GlobalAlloc( GMEM_FIXED, sizeof( wchar_t ) * cfg_val_length );
						MultiByteToWideChar( CP_UTF8, 0, next, string_length, cfg_temp_download_directory, cfg_val_length );

						g_temp_download_directory_length = cfg_val_length - 1;	// Store the base directory length.
					}

					next += string_length;
				}


				// Set the default values for bad configuration values.

				CheckColumnOrders( download_columns, NUM_COLUMNS );

				// Revert column widths if they exceed our limits.
				CheckColumnWidths();

				if ( cfg_t_down_speed > SIZE_FORMAT_AUTO ) { cfg_t_down_speed = SIZE_FORMAT_AUTO; }
				if ( cfg_t_downloaded > SIZE_FORMAT_AUTO ) { cfg_t_downloaded = SIZE_FORMAT_AUTO; }
				if ( cfg_t_file_size > SIZE_FORMAT_AUTO ) { cfg_t_file_size = SIZE_FORMAT_AUTO; }
				if ( cfg_t_speed_limit > SIZE_FORMAT_AUTO ) { cfg_t_speed_limit = SIZE_FORMAT_AUTO; }
				if ( cfg_t_status_downloaded > SIZE_FORMAT_AUTO ) { cfg_t_status_downloaded = SIZE_FORMAT_AUTO; }
				if ( cfg_t_status_down_speed > SIZE_FORMAT_AUTO ) { cfg_t_status_down_speed = SIZE_FORMAT_AUTO; }
				if ( cfg_t_status_speed_limit > SIZE_FORMAT_AUTO ) { cfg_t_status_speed_limit = SIZE_FORMAT_AUTO; }

				if ( cfg_max_downloads > 100 )
				{
					cfg_max_downloads = 100;
				}
				/*else if ( cfg_max_downloads == 0 )
				{
					cfg_max_downloads = 1;
				}*/

				if ( cfg_retry_downloads_count > 100 ) { cfg_retry_downloads_count = 100; }
				if ( cfg_retry_parts_count > 100 ) { cfg_retry_parts_count = 100; }
				if ( cfg_timeout > 300 || ( cfg_timeout > 0 && cfg_timeout < 10 ) ) { cfg_timeout = 60; }

				if ( cfg_default_download_parts > 100 )
				{
					cfg_default_download_parts = 100;
				}
				else if ( cfg_default_download_parts == 0 )
				{
					cfg_default_download_parts = 1;
				}

				if ( cfg_max_redirects > 100 )
				{
					cfg_max_redirects = 100;
				}

				if ( cfg_thread_count > g_max_threads )
				{
					cfg_thread_count = max( ( g_max_threads / 2 ), 1 );
				}
				else if ( cfg_thread_count == 0 )
				{
					cfg_thread_count = 1;
				}

				if ( cfg_default_ssl_version > 4 ) { cfg_default_ssl_version = 4; }	// TLS 1.2.

				if ( cfg_server_port == 0 ) { cfg_server_port = 1; }
				if ( cfg_server_ssl_version > 4 ) { cfg_server_ssl_version = 4; } // TLS 1.2.
				if ( cfg_authentication_type != AUTH_TYPE_BASIC && cfg_authentication_type != AUTH_TYPE_DIGEST ) { cfg_authentication_type = AUTH_TYPE_BASIC; }

				if ( cfg_port == 0 ) { cfg_port = 1; }
				if ( cfg_port_s == 0 ) { cfg_port_s = 1; }
				if ( cfg_port_socks == 0 ) { cfg_port_socks = 1; }

				if ( cfg_max_file_size == 0 ) { cfg_max_file_size = MAX_FILE_SIZE; }

				if ( cfg_shutdown_action == SHUTDOWN_ACTION_HYBRID_SHUT_DOWN && !g_is_windows_8_or_higher )
				{
					cfg_shutdown_action = SHUTDOWN_ACTION_NONE;
				}
			}
			else
			{
				ret_status = -2;	// Bad file format.
			}

			GlobalFree( cfg_buf );
		}
		else
		{
			ret_status = -3;	// Incorrect file size.
		}

		CloseHandle( hFile_cfg );
	}
	else
	{
		ret_status = -1;	// Can't open file for reading.
	}

	if ( ret_status != 0 )
	{
		cfg_odd_row_font_settings.font = _CreateFontIndirectW( &g_default_log_font );
		//cfg_odd_row_font_settings.lf = g_default_log_font;
		_memcpy_s( &cfg_odd_row_font_settings.lf, sizeof( LOGFONT ), &g_default_log_font, sizeof( LOGFONT ) );

		cfg_even_row_font_settings.font = _CreateFontIndirectW( &g_default_log_font );
		//cfg_even_row_font_settings.lf = g_default_log_font;
		_memcpy_s( &cfg_even_row_font_settings.lf, sizeof( LOGFONT ), &g_default_log_font, sizeof( LOGFONT ) );
	}
	else
	{
		cfg_odd_row_font_settings.font = _CreateFontIndirectW( &cfg_odd_row_font_settings.lf );

		cfg_even_row_font_settings.font = _CreateFontIndirectW( &cfg_even_row_font_settings.lf );
	}

	if ( cfg_default_download_directory == NULL )
	{
		cfg_default_download_directory = ( wchar_t * )GlobalAlloc( GMEM_FIXED, sizeof( wchar_t ) * MAX_PATH );

		_SHGetFolderPathW( NULL, CSIDL_MYDOCUMENTS, NULL, 0, cfg_default_download_directory );

		g_default_download_directory_length = lstrlenW( cfg_default_download_directory );
		_wmemcpy_s( cfg_default_download_directory + g_default_download_directory_length, MAX_PATH - g_default_download_directory_length, L"\\Downloads\0", 11 );
		g_default_download_directory_length += 10;
		cfg_default_download_directory[ g_default_download_directory_length ] = 0;	// Sanity.

		// Check to see if the new path exists and create it if it doesn't.
		if ( GetFileAttributesW( cfg_default_download_directory ) == INVALID_FILE_ATTRIBUTES )
		{
			CreateDirectoryW( cfg_default_download_directory, NULL );
		}
	}

	if ( cfg_server_hostname == NULL )
	{
		cfg_server_hostname = ( wchar_t * )GlobalAlloc( GMEM_FIXED, sizeof( wchar_t ) * 10 );
		_wmemcpy_s( cfg_server_hostname, 10, L"localhost\0", 10 );
		cfg_server_hostname[ 9 ] = 0;	// Sanity.
	}

	if ( cfg_hostname == NULL )
	{
		cfg_hostname = ( wchar_t * )GlobalAlloc( GMEM_FIXED, sizeof( wchar_t ) * 10 );
		_wmemcpy_s( cfg_hostname, 10, L"localhost\0", 10 );
		cfg_hostname[ 9 ] = 0;	// Sanity.
	}

	if ( cfg_hostname_s == NULL )
	{
		cfg_hostname_s = ( wchar_t * )GlobalAlloc( GMEM_FIXED, sizeof( wchar_t ) * 10 );
		_wmemcpy_s( cfg_hostname_s, 10, L"localhost\0", 10 );
		cfg_hostname_s[ 9 ] = 0;	// Sanity.
	}

	if ( cfg_hostname_socks == NULL )
	{
		cfg_hostname_socks = ( wchar_t * )GlobalAlloc( GMEM_FIXED, sizeof( wchar_t ) * 10 );
		_wmemcpy_s( cfg_hostname_socks, 10, L"localhost\0", 10 );
		cfg_hostname_socks[ 9 ] = 0;	// Sanity.
	}

	if ( cfg_ftp_hostname == NULL )
	{
		cfg_ftp_hostname = ( wchar_t * )GlobalAlloc( GMEM_FIXED, sizeof( wchar_t ) * 10 );
		_wmemcpy_s( cfg_ftp_hostname, 10, L"localhost\0", 10 );
		cfg_ftp_hostname[ 9 ] = 0;	// Sanity.
	}

	if ( cfg_temp_download_directory == NULL )
	{
		cfg_temp_download_directory = ( wchar_t * )GlobalAlloc( GMEM_FIXED, sizeof( wchar_t ) * MAX_PATH );

		_wmemcpy_s( cfg_temp_download_directory, base_directory_length, base_directory, base_directory_length );
		_wmemcpy_s( cfg_temp_download_directory + base_directory_length, MAX_PATH - base_directory_length, L"\\incomplete\0", 12 );
		g_temp_download_directory_length = base_directory_length + 11;
		cfg_temp_download_directory[ g_temp_download_directory_length ] = 0;	// Sanity.

		// Check to see if the new path exists and create it if it doesn't.
		if ( GetFileAttributesW( cfg_temp_download_directory ) == INVALID_FILE_ATTRIBUTES )
		{
			CreateDirectoryW( cfg_temp_download_directory, NULL );
		}
	}

	return ret_status;
}

char save_config()
{
	char ret_status = 0;

	_wmemcpy_s( base_directory + base_directory_length, MAX_PATH - base_directory_length, L"\\http_downloader_settings\0", 26 );
	base_directory[ base_directory_length + 25 ] = 0;	// Sanity.

	HANDLE hFile_cfg = CreateFile( base_directory, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL );
	if ( hFile_cfg != INVALID_HANDLE_VALUE )
	{
		int reserved = 1024 - 588;
		int size = ( sizeof( int ) * 22 ) +
				   ( sizeof( unsigned short ) * 7 ) +
				   ( sizeof( char ) * 50 ) +
				   ( sizeof( bool ) * 34 ) +
				   ( sizeof( unsigned long ) * 6 ) +
				   ( sizeof( LONG ) * 4 ) +
				   ( sizeof( BYTE ) * 6 ) +
				   ( sizeof( COLORREF ) * ( NUM_COLORS + 8 ) ) +
				   ( sizeof( unsigned long long ) * 3 ) + reserved;
		int pos = 0;

		char *write_buf = ( char * )GlobalAlloc( GMEM_FIXED, sizeof( char ) * size );

		_memcpy_s( write_buf + pos, size - pos, MAGIC_ID_SETTINGS, sizeof( char ) * 4 );	// Magic identifier for the main program's settings.
		pos += ( sizeof( char ) * 4 );

		// Main window.

		_memcpy_s( write_buf + pos, size - pos, &cfg_pos_x, sizeof( int ) );
		pos += sizeof( int );
		_memcpy_s( write_buf + pos, size - pos, &cfg_pos_y, sizeof( int ) );
		pos += sizeof( int );
		_memcpy_s( write_buf + pos, size - pos, &cfg_width, sizeof( int ) );
		pos += sizeof( int );
		_memcpy_s( write_buf + pos, size - pos, &cfg_height, sizeof( int ) );
		pos += sizeof( int );

		_memcpy_s( write_buf + pos, size - pos, &cfg_min_max, sizeof( unsigned char ) );
		pos += sizeof( unsigned char );

		_memcpy_s( write_buf + pos, size - pos, &cfg_column_width1, sizeof( int ) );
		pos += sizeof( int );
		_memcpy_s( write_buf + pos, size - pos, &cfg_column_width2, sizeof( int ) );
		pos += sizeof( int );
		_memcpy_s( write_buf + pos, size - pos, &cfg_column_width3, sizeof( int ) );
		pos += sizeof( int );
		_memcpy_s( write_buf + pos, size - pos, &cfg_column_width4, sizeof( int ) );
		pos += sizeof( int );
		_memcpy_s( write_buf + pos, size - pos, &cfg_column_width5, sizeof( int ) );
		pos += sizeof( int );
		_memcpy_s( write_buf + pos, size - pos, &cfg_column_width6, sizeof( int ) );
		pos += sizeof( int );
		_memcpy_s( write_buf + pos, size - pos, &cfg_column_width7, sizeof( int ) );
		pos += sizeof( int );
		_memcpy_s( write_buf + pos, size - pos, &cfg_column_width8, sizeof( int ) );
		pos += sizeof( int );
		_memcpy_s( write_buf + pos, size - pos, &cfg_column_width9, sizeof( int ) );
		pos += sizeof( int );
		_memcpy_s( write_buf + pos, size - pos, &cfg_column_width10, sizeof( int ) );
		pos += sizeof( int );
		_memcpy_s( write_buf + pos, size - pos, &cfg_column_width11, sizeof( int ) );
		pos += sizeof( int );
		_memcpy_s( write_buf + pos, size - pos, &cfg_column_width12, sizeof( int ) );
		pos += sizeof( int );
		_memcpy_s( write_buf + pos, size - pos, &cfg_column_width13, sizeof( int ) );
		pos += sizeof( int );
		_memcpy_s( write_buf + pos, size - pos, &cfg_column_width14, sizeof( int ) );
		pos += sizeof( int );
		_memcpy_s( write_buf + pos, size - pos, &cfg_column_width15, sizeof( int ) );
		pos += sizeof( int );

		_memcpy_s( write_buf + pos, size - pos, &cfg_column_order1, sizeof( char ) );
		pos += sizeof( char );
		_memcpy_s( write_buf + pos, size - pos, &cfg_column_order2, sizeof( char ) );
		pos += sizeof( char );
		_memcpy_s( write_buf + pos, size - pos, &cfg_column_order3, sizeof( char ) );
		pos += sizeof( char );
		_memcpy_s( write_buf + pos, size - pos, &cfg_column_order4, sizeof( char ) );
		pos += sizeof( char );
		_memcpy_s( write_buf + pos, size - pos, &cfg_column_order5, sizeof( char ) );
		pos += sizeof( char );
		_memcpy_s( write_buf + pos, size - pos, &cfg_column_order6, sizeof( char ) );
		pos += sizeof( char );
		_memcpy_s( write_buf + pos, size - pos, &cfg_column_order7, sizeof( char ) );
		pos += sizeof( char );
		_memcpy_s( write_buf + pos, size - pos, &cfg_column_order8, sizeof( char ) );
		pos += sizeof( char );
		_memcpy_s( write_buf + pos, size - pos, &cfg_column_order9, sizeof( char ) );
		pos += sizeof( char );
		_memcpy_s( write_buf + pos, size - pos, &cfg_column_order10, sizeof( char ) );
		pos += sizeof( char );
		_memcpy_s( write_buf + pos, size - pos, &cfg_column_order11, sizeof( char ) );
		pos += sizeof( char );
		_memcpy_s( write_buf + pos, size - pos, &cfg_column_order12, sizeof( char ) );
		pos += sizeof( char );
		_memcpy_s( write_buf + pos, size - pos, &cfg_column_order13, sizeof( char ) );
		pos += sizeof( char );
		_memcpy_s( write_buf + pos, size - pos, &cfg_column_order14, sizeof( char ) );
		pos += sizeof( char );
		_memcpy_s( write_buf + pos, size - pos, &cfg_column_order15, sizeof( char ) );
		pos += sizeof( char );

		_memcpy_s( write_buf + pos, size - pos, &cfg_show_column_headers, sizeof( bool ) );
		pos += sizeof( bool );

		_memcpy_s( write_buf + pos, size - pos, &cfg_sorted_column_index, sizeof( int ) );
		pos += sizeof( int );

		_memcpy_s( write_buf + pos, size - pos, &cfg_sorted_direction, sizeof( unsigned char ) );
		pos += sizeof( unsigned char );

		_memcpy_s( write_buf + pos, size - pos, &cfg_show_toolbar, sizeof( bool ) );
		pos += sizeof( bool );

		_memcpy_s( write_buf + pos, size - pos, &cfg_show_status_bar, sizeof( bool ) );
		pos += sizeof( bool );

		_memcpy_s( write_buf + pos, size - pos, &cfg_t_down_speed, sizeof( unsigned char ) );
		pos += sizeof( unsigned char );
		_memcpy_s( write_buf + pos, size - pos, &cfg_t_downloaded, sizeof( unsigned char ) );
		pos += sizeof( unsigned char );
		_memcpy_s( write_buf + pos, size - pos, &cfg_t_file_size, sizeof( unsigned char ) );
		pos += sizeof( unsigned char );
		_memcpy_s( write_buf + pos, size - pos, &cfg_t_speed_limit, sizeof( unsigned char ) );
		pos += sizeof( unsigned char );

		_memcpy_s( write_buf + pos, size - pos, &cfg_t_status_downloaded, sizeof( unsigned char ) );
		pos += sizeof( unsigned char );
		_memcpy_s( write_buf + pos, size - pos, &cfg_t_status_down_speed, sizeof( unsigned char ) );
		pos += sizeof( unsigned char );
		_memcpy_s( write_buf + pos, size - pos, &cfg_t_status_speed_limit, sizeof( unsigned char ) );
		pos += sizeof( unsigned char );

		// Global Speed Limit

		_memcpy_s( write_buf + pos, size - pos, &cfg_download_speed_limit, sizeof( unsigned long long ) );
		pos += sizeof( unsigned long long );

		// Options General

		_memcpy_s( write_buf + pos, size - pos, &cfg_tray_icon, sizeof( bool ) );
		pos += sizeof( bool );
		_memcpy_s( write_buf + pos, size - pos, &cfg_minimize_to_tray, sizeof( bool ) );
		pos += sizeof( bool );
		_memcpy_s( write_buf + pos, size - pos, &cfg_close_to_tray, sizeof( bool ) );
		pos += sizeof( bool );
		_memcpy_s( write_buf + pos, size - pos, &cfg_start_in_tray, sizeof( bool ) );
		pos += sizeof( bool );
		_memcpy_s( write_buf + pos, size - pos, &cfg_show_notification, sizeof( bool ) );
		pos += sizeof( bool );
		_memcpy_s( write_buf + pos, size - pos, &cfg_show_tray_progress, sizeof( bool ) );
		pos += sizeof( bool );

		_memcpy_s( write_buf + pos, size - pos, &cfg_always_on_top, sizeof( bool ) );
		pos += sizeof( bool );

		_memcpy_s( write_buf + pos, size - pos, &cfg_enable_drop_window, sizeof( bool ) );
		pos += sizeof( bool );
		_memcpy_s( write_buf + pos, size - pos, &cfg_drop_window_transparency, sizeof( unsigned char ) );
		pos += sizeof( unsigned char );
		_memcpy_s( write_buf + pos, size - pos, &cfg_show_drop_window_progress, sizeof( bool ) );
		pos += sizeof( bool );
		_memcpy_s( write_buf + pos, size - pos, &cfg_drop_pos_x, sizeof( int ) );
		pos += sizeof( int );
		_memcpy_s( write_buf + pos, size - pos, &cfg_drop_pos_y, sizeof( int ) );
		pos += sizeof( int );

		_memcpy_s( write_buf + pos, size - pos, &cfg_play_sound, sizeof( bool ) );
		pos += sizeof( bool );

		// Options Appearance

		_memcpy_s( write_buf + pos, size - pos, &cfg_show_gridlines, sizeof( bool ) );
		pos += sizeof( bool );
		_memcpy_s( write_buf + pos, size - pos, &cfg_show_part_progress, sizeof( bool ) );
		pos += sizeof( bool );
		_memcpy_s( write_buf + pos, size - pos, &cfg_sort_added_and_updating_items, sizeof( bool ) );
		pos += sizeof( bool );

		_memcpy_s( write_buf + pos, size - pos, &cfg_odd_row_font_settings.font_color, sizeof( COLORREF ) );
		pos += sizeof( COLORREF );
		_memcpy_s( write_buf + pos, size - pos, &cfg_odd_row_font_settings.lf.lfHeight, sizeof( LONG ) );
		pos += sizeof( LONG );
		_memcpy_s( write_buf + pos, size - pos, &cfg_odd_row_font_settings.lf.lfWeight, sizeof( LONG ) );
		pos += sizeof( LONG );
		_memcpy_s( write_buf + pos, size - pos, &cfg_odd_row_font_settings.lf.lfItalic, sizeof( BYTE ) );
		pos += sizeof( BYTE );
		_memcpy_s( write_buf + pos, size - pos, &cfg_odd_row_font_settings.lf.lfUnderline, sizeof( BYTE ) );
		pos += sizeof( BYTE );
		_memcpy_s( write_buf + pos, size - pos, &cfg_odd_row_font_settings.lf.lfStrikeOut, sizeof( BYTE ) );
		pos += sizeof( BYTE );

		_memcpy_s( write_buf + pos, size - pos, &cfg_odd_row_background_color, sizeof( COLORREF ) );
		pos += sizeof( COLORREF );
		_memcpy_s( write_buf + pos, size - pos, &cfg_odd_row_highlight_color, sizeof( COLORREF ) );
		pos += sizeof( COLORREF );
		_memcpy_s( write_buf + pos, size - pos, &cfg_odd_row_highlight_font_color, sizeof( COLORREF ) );
		pos += sizeof( COLORREF );

		_memcpy_s( write_buf + pos, size - pos, &cfg_even_row_font_settings.font_color, sizeof( COLORREF ) );
		pos += sizeof( COLORREF );
		_memcpy_s( write_buf + pos, size - pos, &cfg_even_row_font_settings.lf.lfHeight, sizeof( LONG ) );
		pos += sizeof( LONG );
		_memcpy_s( write_buf + pos, size - pos, &cfg_even_row_font_settings.lf.lfWeight, sizeof( LONG ) );
		pos += sizeof( LONG );
		_memcpy_s( write_buf + pos, size - pos, &cfg_even_row_font_settings.lf.lfItalic, sizeof( BYTE ) );
		pos += sizeof( BYTE );
		_memcpy_s( write_buf + pos, size - pos, &cfg_even_row_font_settings.lf.lfUnderline, sizeof( BYTE ) );
		pos += sizeof( BYTE );
		_memcpy_s( write_buf + pos, size - pos, &cfg_even_row_font_settings.lf.lfStrikeOut, sizeof( BYTE ) );
		pos += sizeof( BYTE );

		_memcpy_s( write_buf + pos, size - pos, &cfg_even_row_background_color, sizeof( COLORREF ) );
		pos += sizeof( COLORREF );
		_memcpy_s( write_buf + pos, size - pos, &cfg_even_row_highlight_color, sizeof( COLORREF ) );
		pos += sizeof( COLORREF );
		_memcpy_s( write_buf + pos, size - pos, &cfg_even_row_highlight_font_color, sizeof( COLORREF ) );
		pos += sizeof( COLORREF );

		for ( unsigned char i = 0; i < NUM_COLORS; ++i )
		{
			_memcpy_s( write_buf + pos, size - pos, progress_colors[ i ], sizeof( COLORREF ) );
			pos += sizeof( COLORREF );
		}

		// Options Connection

		_memcpy_s( write_buf + pos, size - pos, &cfg_max_downloads, sizeof( unsigned char ) );
		pos += sizeof( unsigned char );

		_memcpy_s( write_buf + pos, size - pos, &cfg_default_download_parts, sizeof( unsigned char ) );
		pos += sizeof( unsigned char );

		_memcpy_s( write_buf + pos, size - pos, &cfg_retry_downloads_count, sizeof( unsigned char ) );
		pos += sizeof( unsigned char );

		_memcpy_s( write_buf + pos, size - pos, &cfg_retry_parts_count, sizeof( unsigned char ) );
		pos += sizeof( unsigned char );

		_memcpy_s( write_buf + pos, size - pos, &cfg_timeout, sizeof( unsigned short ) );
		pos += sizeof( unsigned short );

		_memcpy_s( write_buf + pos, size - pos, &cfg_max_redirects, sizeof( unsigned char ) );
		pos += sizeof( unsigned char );

		_memcpy_s( write_buf + pos, size - pos, &cfg_default_speed_limit, sizeof( unsigned long long ) );
		pos += sizeof( unsigned long long );

		_memcpy_s( write_buf + pos, size - pos, &cfg_default_ssl_version, sizeof( unsigned char ) );
		pos += sizeof( unsigned char );

		// Options FTP

		_memcpy_s( write_buf + pos, size - pos, &cfg_ftp_mode_type, sizeof( unsigned char ) );
		pos += sizeof( unsigned char );

		_memcpy_s( write_buf + pos, size - pos, &cfg_ftp_enable_fallback_mode, sizeof( bool ) );
		pos += sizeof( bool );

		_memcpy_s( write_buf + pos, size - pos, &cfg_ftp_address_type, sizeof( unsigned char ) );
		pos += sizeof( unsigned char );

		_memcpy_s( write_buf + pos, size - pos, &cfg_ftp_ip_address, sizeof( unsigned long ) );
		pos += sizeof( unsigned long );

		_memcpy_s( write_buf + pos, size - pos, &cfg_ftp_port_start, sizeof( unsigned short ) );
		pos += sizeof( unsigned short );

		_memcpy_s( write_buf + pos, size - pos, &cfg_ftp_port_end, sizeof( unsigned short ) );
		pos += sizeof( unsigned short );

		_memcpy_s( write_buf + pos, size - pos, &cfg_ftp_send_keep_alive, sizeof( bool ) );
		pos += sizeof( bool );

		// Options Proxy

		_memcpy_s( write_buf + pos, size - pos, &cfg_enable_proxy, sizeof( bool ) );
		pos += sizeof( bool );

		_memcpy_s( write_buf + pos, size - pos, &cfg_address_type, sizeof( unsigned char ) );
		pos += sizeof( unsigned char );

		_memcpy_s( write_buf + pos, size - pos, &cfg_ip_address, sizeof( unsigned long ) );
		pos += sizeof( unsigned long );

		_memcpy_s( write_buf + pos, size - pos, &cfg_port, sizeof( unsigned short ) );
		pos += sizeof( unsigned short );

		//

		_memcpy_s( write_buf + pos, size - pos, &cfg_enable_proxy_s, sizeof( bool ) );
		pos += sizeof( bool );

		_memcpy_s( write_buf + pos, size - pos, &cfg_address_type_s, sizeof( unsigned char ) );
		pos += sizeof( unsigned char );

		_memcpy_s( write_buf + pos, size - pos, &cfg_ip_address_s, sizeof( unsigned long ) );
		pos += sizeof( unsigned long );

		_memcpy_s( write_buf + pos, size - pos, &cfg_port_s, sizeof( unsigned short ) );
		pos += sizeof( unsigned short );

		//

		_memcpy_s( write_buf + pos, size - pos, &cfg_enable_proxy_socks, sizeof( bool ) );
		pos += sizeof( bool );

		_memcpy_s( write_buf + pos, size - pos, &cfg_socks_type, sizeof( unsigned char ) );
		pos += sizeof( unsigned char );

		_memcpy_s( write_buf + pos, size - pos, &cfg_address_type_socks, sizeof( unsigned char ) );
		pos += sizeof( unsigned char );

		_memcpy_s( write_buf + pos, size - pos, &cfg_ip_address_socks, sizeof( unsigned long ) );
		pos += sizeof( unsigned long );

		_memcpy_s( write_buf + pos, size - pos, &cfg_port_socks, sizeof( unsigned short ) );
		pos += sizeof( unsigned short );

		_memcpy_s( write_buf + pos, size - pos, &cfg_use_authentication_socks, sizeof( bool ) );
		pos += sizeof( bool );

		_memcpy_s( write_buf + pos, size - pos, &cfg_resolve_domain_names_v4a, sizeof( bool ) );
		pos += sizeof( bool );

		_memcpy_s( write_buf + pos, size - pos, &cfg_resolve_domain_names, sizeof( bool ) );
		pos += sizeof( bool );

		// Options Server

		_memcpy_s( write_buf + pos, size - pos, &cfg_enable_server, sizeof( bool ) );
		pos += sizeof( bool );

		_memcpy_s( write_buf + pos, size - pos, &cfg_server_address_type, sizeof( unsigned char ) );
		pos += sizeof( unsigned char );

		_memcpy_s( write_buf + pos, size - pos, &cfg_server_ip_address, sizeof( unsigned long ) );
		pos += sizeof( unsigned long );

		_memcpy_s( write_buf + pos, size - pos, &cfg_server_port, sizeof( unsigned short ) );
		pos += sizeof( unsigned short );

		_memcpy_s( write_buf + pos, size - pos, &cfg_use_authentication, sizeof( bool ) );
		pos += sizeof( bool );

		_memcpy_s( write_buf + pos, size - pos, &cfg_authentication_type, sizeof( unsigned char ) );
		pos += sizeof( unsigned char );

		_memcpy_s( write_buf + pos, size - pos, &cfg_server_enable_ssl, sizeof( bool ) );
		pos += sizeof( bool );

		_memcpy_s( write_buf + pos, size - pos, &cfg_certificate_type, sizeof( unsigned char ) );
		pos += sizeof( unsigned char );

		_memcpy_s( write_buf + pos, size - pos, &cfg_server_ssl_version, sizeof( unsigned char ) );
		pos += sizeof( unsigned char );

		// Options Advanced

		_memcpy_s( write_buf + pos, size - pos, &cfg_enable_download_history, sizeof( bool ) );
		pos += sizeof( bool );

		_memcpy_s( write_buf + pos, size - pos, &cfg_enable_quick_allocation, sizeof( bool ) );
		pos += sizeof( bool );

		_memcpy_s( write_buf + pos, size - pos, &cfg_set_filetime, sizeof( bool ) );
		pos += sizeof( bool );

		_memcpy_s( write_buf + pos, size - pos, &cfg_use_one_instance, sizeof( bool ) );
		pos += sizeof( bool );

		_memcpy_s( write_buf + pos, size - pos, &cfg_prevent_standby, sizeof( bool ) );
		pos += sizeof( bool );

		_memcpy_s( write_buf + pos, size - pos, &cfg_resume_downloads, sizeof( bool ) );
		pos += sizeof( bool );

		_memcpy_s( write_buf + pos, size - pos, &cfg_drag_and_drop_action, sizeof( unsigned char ) );
		pos += sizeof( unsigned char );

		_memcpy_s( write_buf + pos, size - pos, &cfg_prompt_last_modified, sizeof( unsigned char ) );
		pos += sizeof( unsigned char );

		_memcpy_s( write_buf + pos, size - pos, &cfg_prompt_rename, sizeof( unsigned char ) );
		pos += sizeof( unsigned char );

		_memcpy_s( write_buf + pos, size - pos, &cfg_prompt_file_size, sizeof( unsigned char ) );
		pos += sizeof( unsigned char );
		_memcpy_s( write_buf + pos, size - pos, &cfg_max_file_size, sizeof( unsigned long long ) );
		pos += sizeof( unsigned long long );

		_memcpy_s( write_buf + pos, size - pos, &cfg_shutdown_action, sizeof( unsigned char ) );
		pos += sizeof( unsigned char );

		_memcpy_s( write_buf + pos, size - pos, &cfg_use_temp_download_directory, sizeof( bool ) );
		pos += sizeof( bool );

		_memcpy_s( write_buf + pos, size - pos, &cfg_thread_count, sizeof( unsigned long ) );
		pos += sizeof( unsigned long );


		//


		// Write Reserved bytes.
		_memzero( write_buf + pos, size - pos );

		DWORD write = 0;
		WriteFile( hFile_cfg, write_buf, size, &write, NULL );

		GlobalFree( write_buf );

		int cfg_val_length = 0;
		char *utf8_cfg_val = NULL;


		//


		// Options General

		if ( cfg_sound_file_path != NULL )
		{
			utf8_cfg_val = WideStringToUTF8String( cfg_sound_file_path, &cfg_val_length );
			WriteFile( hFile_cfg, utf8_cfg_val, cfg_val_length, &write, NULL );
			GlobalFree( utf8_cfg_val );
		}
		else
		{
			WriteFile( hFile_cfg, "\0", 1, &write, NULL );
		}

		// Options Appearance

		utf8_cfg_val = WideStringToUTF8String( cfg_odd_row_font_settings.lf.lfFaceName, &cfg_val_length );
		WriteFile( hFile_cfg, utf8_cfg_val, cfg_val_length, &write, NULL );
		GlobalFree( utf8_cfg_val );

		utf8_cfg_val = WideStringToUTF8String( cfg_even_row_font_settings.lf.lfFaceName, &cfg_val_length );
		WriteFile( hFile_cfg, utf8_cfg_val, cfg_val_length, &write, NULL );
		GlobalFree( utf8_cfg_val );

		// Options FTP

		if ( cfg_ftp_hostname != NULL )
		{
			utf8_cfg_val = WideStringToUTF8String( cfg_ftp_hostname, &cfg_val_length );
			WriteFile( hFile_cfg, utf8_cfg_val, cfg_val_length, &write, NULL );
			GlobalFree( utf8_cfg_val );
		}
		else
		{
			WriteFile( hFile_cfg, "\0", 1, &write, NULL );
		}

		// Options Proxy

		if ( cfg_hostname != NULL )
		{
			utf8_cfg_val = WideStringToUTF8String( cfg_hostname, &cfg_val_length );
			WriteFile( hFile_cfg, utf8_cfg_val, cfg_val_length, &write, NULL );
			GlobalFree( utf8_cfg_val );
		}
		else
		{
			WriteFile( hFile_cfg, "\0", 1, &write, NULL );
		}

		if ( cfg_proxy_auth_username != NULL )
		{
			utf8_cfg_val = WideStringToUTF8String( cfg_proxy_auth_username, &cfg_val_length, sizeof( unsigned short ) );	// Add 2 bytes for our encoded length.

			int length = cfg_val_length - 1;	// Exclude the NULL terminator.
			_memcpy_s( utf8_cfg_val, cfg_val_length, &length, sizeof( unsigned short ) );

			encode_cipher( utf8_cfg_val + sizeof( unsigned short ), length );

			WriteFile( hFile_cfg, utf8_cfg_val, length + sizeof( unsigned short ), &write, NULL );	// Do not write the NULL terminator.
			GlobalFree( utf8_cfg_val );
		}
		else
		{
			WriteFile( hFile_cfg, "\0\0", 2, &write, NULL );
		}

		if ( cfg_proxy_auth_password != NULL )
		{
			utf8_cfg_val = WideStringToUTF8String( cfg_proxy_auth_password, &cfg_val_length, sizeof( unsigned short ) );	// Add 2 bytes for our encoded length.

			int length = cfg_val_length - 1;	// Exclude the NULL terminator.
			_memcpy_s( utf8_cfg_val, cfg_val_length, &length, sizeof( unsigned short ) );

			encode_cipher( utf8_cfg_val + sizeof( unsigned short ), length );

			WriteFile( hFile_cfg, utf8_cfg_val, length + sizeof( unsigned short ), &write, NULL );	// Do not write the NULL terminator.
			GlobalFree( utf8_cfg_val );
		}
		else
		{
			WriteFile( hFile_cfg, "\0\0", 2, &write, NULL );
		}

		//

		if ( cfg_hostname_s != NULL )
		{
			utf8_cfg_val = WideStringToUTF8String( cfg_hostname_s, &cfg_val_length );
			WriteFile( hFile_cfg, utf8_cfg_val, cfg_val_length, &write, NULL );
			GlobalFree( utf8_cfg_val );
		}
		else
		{
			WriteFile( hFile_cfg, "\0", 1, &write, NULL );
		}

		if ( cfg_proxy_auth_username_s != NULL )
		{
			utf8_cfg_val = WideStringToUTF8String( cfg_proxy_auth_username_s, &cfg_val_length, sizeof( unsigned short ) );	// Add 2 bytes for our encoded length.

			int length = cfg_val_length - 1;	// Exclude the NULL terminator.
			_memcpy_s( utf8_cfg_val, cfg_val_length, &length, sizeof( unsigned short ) );

			encode_cipher( utf8_cfg_val + sizeof( unsigned short ), length );

			WriteFile( hFile_cfg, utf8_cfg_val, length + sizeof( unsigned short ), &write, NULL );	// Do not write the NULL terminator.
			GlobalFree( utf8_cfg_val );
		}
		else
		{
			WriteFile( hFile_cfg, "\0\0", 2, &write, NULL );
		}

		if ( cfg_proxy_auth_password_s != NULL )
		{
			utf8_cfg_val = WideStringToUTF8String( cfg_proxy_auth_password_s, &cfg_val_length, sizeof( unsigned short ) );	// Add 2 bytes for our encoded length.

			int length = cfg_val_length - 1;	// Exclude the NULL terminator.
			_memcpy_s( utf8_cfg_val, cfg_val_length, &length, sizeof( unsigned short ) );

			encode_cipher( utf8_cfg_val + sizeof( unsigned short ), length );

			WriteFile( hFile_cfg, utf8_cfg_val, length + sizeof( unsigned short ), &write, NULL );	// Do not write the NULL terminator.
			GlobalFree( utf8_cfg_val );
		}
		else
		{
			WriteFile( hFile_cfg, "\0\0", 2, &write, NULL );
		}

		//

		if ( cfg_hostname_socks != NULL )
		{
			utf8_cfg_val = WideStringToUTF8String( cfg_hostname_socks, &cfg_val_length );
			WriteFile( hFile_cfg, utf8_cfg_val, cfg_val_length, &write, NULL );
			GlobalFree( utf8_cfg_val );
		}
		else
		{
			WriteFile( hFile_cfg, "\0", 1, &write, NULL );
		}

		if ( cfg_proxy_auth_ident_username_socks != NULL )
		{
			utf8_cfg_val = WideStringToUTF8String( cfg_proxy_auth_ident_username_socks, &cfg_val_length, sizeof( unsigned short ) );	// Add 2 bytes for our encoded length.

			int length = cfg_val_length - 1;	// Exclude the NULL terminator.
			_memcpy_s( utf8_cfg_val, cfg_val_length, &length, sizeof( unsigned short ) );

			encode_cipher( utf8_cfg_val + sizeof( unsigned short ), length );

			WriteFile( hFile_cfg, utf8_cfg_val, length + sizeof( unsigned short ), &write, NULL );	// Do not write the NULL terminator.
			GlobalFree( utf8_cfg_val );
		}
		else
		{
			WriteFile( hFile_cfg, "\0\0", 2, &write, NULL );
		}

		if ( cfg_proxy_auth_username_socks != NULL )
		{
			utf8_cfg_val = WideStringToUTF8String( cfg_proxy_auth_username_socks, &cfg_val_length, sizeof( unsigned short ) );	// Add 2 bytes for our encoded length.

			int length = cfg_val_length - 1;	// Exclude the NULL terminator.
			_memcpy_s( utf8_cfg_val, cfg_val_length, &length, sizeof( unsigned short ) );

			encode_cipher( utf8_cfg_val + sizeof( unsigned short ), length );

			WriteFile( hFile_cfg, utf8_cfg_val, length + sizeof( unsigned short ), &write, NULL );	// Do not write the NULL terminator.
			GlobalFree( utf8_cfg_val );
		}
		else
		{
			WriteFile( hFile_cfg, "\0\0", 2, &write, NULL );
		}

		if ( cfg_proxy_auth_password_socks != NULL )
		{
			utf8_cfg_val = WideStringToUTF8String( cfg_proxy_auth_password_socks, &cfg_val_length, sizeof( unsigned short ) );	// Add 2 bytes for our encoded length.

			int length = cfg_val_length - 1;	// Exclude the NULL terminator.
			_memcpy_s( utf8_cfg_val, cfg_val_length, &length, sizeof( unsigned short ) );

			encode_cipher( utf8_cfg_val + sizeof( unsigned short ), length );

			WriteFile( hFile_cfg, utf8_cfg_val, length + sizeof( unsigned short ), &write, NULL );	// Do not write the NULL terminator.
			GlobalFree( utf8_cfg_val );
		}
		else
		{
			WriteFile( hFile_cfg, "\0\0", 2, &write, NULL );
		}

		// Options Server

		if ( cfg_server_hostname != NULL )
		{
			utf8_cfg_val = WideStringToUTF8String( cfg_server_hostname, &cfg_val_length );
			WriteFile( hFile_cfg, utf8_cfg_val, cfg_val_length, &write, NULL );
			GlobalFree( utf8_cfg_val );
		}
		else
		{
			WriteFile( hFile_cfg, "\0", 1, &write, NULL );
		}

		if ( cfg_authentication_username != NULL )
		{
			utf8_cfg_val = WideStringToUTF8String( cfg_authentication_username, &cfg_val_length, sizeof( unsigned short ) );	// Add 2 bytes for our encoded length.

			int length = cfg_val_length - 1;	// Exclude the NULL terminator.
			_memcpy_s( utf8_cfg_val, cfg_val_length, &length, sizeof( unsigned short ) );

			encode_cipher( utf8_cfg_val + sizeof( unsigned short ), length );

			WriteFile( hFile_cfg, utf8_cfg_val, length + sizeof( unsigned short ), &write, NULL );	// Do not write the NULL terminator.
			GlobalFree( utf8_cfg_val );
		}
		else
		{
			WriteFile( hFile_cfg, "\0\0", 2, &write, NULL );
		}

		if ( cfg_authentication_password != NULL )
		{
			utf8_cfg_val = WideStringToUTF8String( cfg_authentication_password, &cfg_val_length, sizeof( unsigned short ) );	// Add 2 bytes for our encoded length.

			int length = cfg_val_length - 1;	// Exclude the NULL terminator.
			_memcpy_s( utf8_cfg_val, cfg_val_length, &length, sizeof( unsigned short ) );

			encode_cipher( utf8_cfg_val + sizeof( unsigned short ), length );

			WriteFile( hFile_cfg, utf8_cfg_val, length + sizeof( unsigned short ), &write, NULL );	// Do not write the NULL terminator.
			GlobalFree( utf8_cfg_val );
		}
		else
		{
			WriteFile( hFile_cfg, "\0\0", 2, &write, NULL );
		}

		if ( cfg_certificate_pkcs_password != NULL )
		{
			utf8_cfg_val = WideStringToUTF8String( cfg_certificate_pkcs_password, &cfg_val_length, sizeof( unsigned short ) );	// Add 2 bytes for our encoded length.

			int length = cfg_val_length - 1;	// Exclude the NULL terminator.
			_memcpy_s( utf8_cfg_val, cfg_val_length, &length, sizeof( unsigned short ) );

			encode_cipher( utf8_cfg_val + sizeof( unsigned short ), length );

			WriteFile( hFile_cfg, utf8_cfg_val, length + sizeof( unsigned short ), &write, NULL );	// Do not write the NULL terminator.
			GlobalFree( utf8_cfg_val );
		}
		else
		{
			WriteFile( hFile_cfg, "\0\0", 2, &write, NULL );
		}

		if ( cfg_certificate_pkcs_file_name != NULL )
		{
			utf8_cfg_val = WideStringToUTF8String( cfg_certificate_pkcs_file_name, &cfg_val_length );
			WriteFile( hFile_cfg, utf8_cfg_val, cfg_val_length, &write, NULL );
			GlobalFree( utf8_cfg_val );
		}
		else
		{
			WriteFile( hFile_cfg, "\0", 1, &write, NULL );
		}

		if ( cfg_certificate_cer_file_name != NULL )
		{
			utf8_cfg_val = WideStringToUTF8String( cfg_certificate_cer_file_name, &cfg_val_length );
			WriteFile( hFile_cfg, utf8_cfg_val, cfg_val_length, &write, NULL );
			GlobalFree( utf8_cfg_val );
		}
		else
		{
			WriteFile( hFile_cfg, "\0", 1, &write, NULL );
		}

		if ( cfg_certificate_key_file_name != NULL )
		{
			utf8_cfg_val = WideStringToUTF8String( cfg_certificate_key_file_name, &cfg_val_length );
			WriteFile( hFile_cfg, utf8_cfg_val, cfg_val_length, &write, NULL );
			GlobalFree( utf8_cfg_val );
		}
		else
		{
			WriteFile( hFile_cfg, "\0", 1, &write, NULL );
		}

		// Options Advanced

		if ( cfg_default_download_directory != NULL )
		{
			utf8_cfg_val = WideStringToUTF8String( cfg_default_download_directory, &cfg_val_length );
			WriteFile( hFile_cfg, utf8_cfg_val, cfg_val_length, &write, NULL );
			GlobalFree( utf8_cfg_val );
		}
		else
		{
			WriteFile( hFile_cfg, "\0", 1, &write, NULL );
		}

		if ( cfg_temp_download_directory != NULL )
		{
			utf8_cfg_val = WideStringToUTF8String( cfg_temp_download_directory, &cfg_val_length );
			WriteFile( hFile_cfg, utf8_cfg_val, cfg_val_length, &write, NULL );
			GlobalFree( utf8_cfg_val );
		}
		else
		{
			WriteFile( hFile_cfg, "\0", 1, &write, NULL );
		}

		CloseHandle( hFile_cfg );
	}
	else
	{
		ret_status = -1;	// Can't open file for writing.
	}

	return ret_status;
}

char read_download_history( wchar_t *file_path )
{
	char ret_status = 0;

	HANDLE hFile_read = CreateFile( file_path, GENERIC_READ, 0, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL );
	if ( hFile_read != INVALID_HANDLE_VALUE )
	{
		DWORD read = 0, total_read = 0, offset = 0, last_entry = 0, last_total = 0;

		char *p = NULL;

		ULARGE_INTEGER		add_time;
		unsigned long long	downloaded;
		unsigned long long	file_size;
		unsigned long long	download_speed_limit;

		char				*download_directory;
		unsigned int		download_directory_length;
		char				*filename;
		unsigned int		filename_length;

		wchar_t				*url;
		DoublyLinkedList	*range_list;
		unsigned char		parts;
		unsigned char		parts_limit;
		unsigned int		status;

		char				*cookies;
		char				*headers;
		char				*data;

		char				*username;
		char				*password;

		char				ssl_version;

		bool				processed_header;
		unsigned char		download_operations;
		unsigned char		method;

		ULARGE_INTEGER		last_modified;

		unsigned char range_count;

		char magic_identifier[ 4 ];
		ReadFile( hFile_read, magic_identifier, sizeof( char ) * 4, &read, NULL );
		if ( read == 4 && _memcmp( magic_identifier, MAGIC_ID_DOWNLOADS, 4 ) == 0 )
		{
			DWORD fz = GetFileSize( hFile_read, NULL ) - 4;

			char *history_buf = ( char * )GlobalAlloc( GMEM_FIXED, sizeof( char ) * ( 524288 + 1 ) );	// 512 KB buffer.

			SHFILEINFO *sfi = ( SHFILEINFO * )GlobalAlloc( GMEM_FIXED, sizeof( SHFILEINFO ) );

			while ( total_read < fz )
			{
				ReadFile( hFile_read, history_buf, sizeof( char ) * 524288, &read, NULL );

				history_buf[ read ] = 0;	// Guarantee a NULL terminated buffer.

				// Make sure that we have at least part of the entry. This is the minimum size an entry could be.
				// Include 3 wide NULL strings and 3 char NULL strings.
				// Include 2 ints for username and password lengths.
				// Include 1 unsigned char for range info.
				if ( read < ( ( ( sizeof( ULONGLONG ) * 2 ) + ( sizeof( unsigned long long ) * 3 ) + ( sizeof( unsigned char ) * 5 ) + sizeof( unsigned int ) + sizeof( bool ) ) +
							  ( ( sizeof( wchar_t ) * 3 ) + ( sizeof( char ) * 3 ) ) +
								( sizeof( int ) * 2 ) + 
								  sizeof( unsigned char ) ) )
				{
					break;
				}

				total_read += read;

				// Prevent an infinite loop if a really really long entry causes us to jump back to the same point in the file.
				// If it's larger than our buffer, then the file is probably invalid/corrupt.
				if ( total_read == last_total )
				{
					break;
				}

				last_total = total_read;

				p = history_buf;
				offset = last_entry = 0;

				while ( offset < read )
				{
					download_directory = NULL;
					download_directory_length = 0;
					filename = NULL;
					filename_length = 0;
					url = NULL;
					cookies = NULL;
					headers = NULL;
					data = NULL;
					username = NULL;
					password = NULL;
					range_list = NULL;

					// Add Time.
					offset += sizeof( ULONGLONG );
					if ( offset >= read ) { goto CLEANUP; }
					_memcpy_s( &add_time.QuadPart, sizeof( ULONGLONG ), p, sizeof( ULONGLONG ) );
					p += sizeof( ULONGLONG );

					// Downloaded
					offset += sizeof( unsigned long long );
					if ( offset >= read ) { goto CLEANUP; }
					_memcpy_s( &downloaded, sizeof( unsigned long long ), p, sizeof( unsigned long long ) );
					p += sizeof( unsigned long long );

					// File Size
					offset += sizeof( unsigned long long );
					if ( offset >= read ) { goto CLEANUP; }
					_memcpy_s( &file_size, sizeof( unsigned long long ), p, sizeof( unsigned long long ) );
					p += sizeof( unsigned long long );

					// Download Speed Limit
					offset += sizeof( unsigned long long );
					if ( offset >= read ) { goto CLEANUP; }
					_memcpy_s( &download_speed_limit, sizeof( unsigned long long ), p, sizeof( unsigned long long ) );
					p += sizeof( unsigned long long );

					// Parts
					offset += sizeof( unsigned char );
					if ( offset >= read ) { goto CLEANUP; }
					_memcpy_s( &parts, sizeof( unsigned char ), p, sizeof( unsigned char ) );
					p += sizeof( unsigned char );

					// Parts Limit
					offset += sizeof( unsigned char );
					if ( offset >= read ) { goto CLEANUP; }
					_memcpy_s( &parts_limit, sizeof( unsigned char ), p, sizeof( unsigned char ) );
					p += sizeof( unsigned char );

					// Status
					offset += sizeof( unsigned int );
					if ( offset >= read ) { goto CLEANUP; }
					_memcpy_s( &status, sizeof( unsigned int ), p, sizeof( unsigned int ) );
					p += sizeof( unsigned int );

					// SSL Version
					offset += sizeof( char );
					if ( offset >= read ) { goto CLEANUP; }
					_memcpy_s( &ssl_version, sizeof( char ), p, sizeof( char ) );
					p += sizeof( char );

					// Create Range
					offset += sizeof( bool );
					if ( offset >= read ) { goto CLEANUP; }
					_memcpy_s( &processed_header, sizeof( bool ), p, sizeof( bool ) );
					p += sizeof( bool );

					// Download Operations
					offset += sizeof( unsigned char );
					if ( offset >= read ) { goto CLEANUP; }
					_memcpy_s( &download_operations, sizeof( unsigned char ), p, sizeof( unsigned char ) );
					p += sizeof( unsigned char );

					// Method
					offset += sizeof( unsigned char );
					if ( offset >= read ) { goto CLEANUP; }
					_memcpy_s( &method, sizeof( unsigned char ), p, sizeof( unsigned char ) );
					p += sizeof( unsigned char );

					// Last Modified
					offset += sizeof( ULONGLONG );
					if ( offset >= read ) { goto CLEANUP; }
					_memcpy_s( &last_modified.QuadPart, sizeof( ULONGLONG ), p, sizeof( ULONGLONG ) );
					p += sizeof( ULONGLONG );

					// Download Directory
					int string_length = lstrlenW( ( wchar_t * )p ) + 1;

					offset += ( string_length * sizeof( wchar_t ) );
					if ( offset >= read ) { goto CLEANUP; }

					download_directory = p;
					download_directory_length = string_length;

					p += ( string_length * sizeof( wchar_t ) );

					// Filename
					string_length = lstrlenW( ( wchar_t * )p ) + 1;

					offset += ( string_length * sizeof( wchar_t ) );
					if ( offset >= read ) { goto CLEANUP; }

					filename = p;
					filename_length = string_length - 1;

					p += ( string_length * sizeof( wchar_t ) );

					// URL
					string_length = lstrlenW( ( wchar_t * )p ) + 1;

					offset += ( string_length * sizeof( wchar_t ) );
					if ( offset >= read ) { goto CLEANUP; }

					url = ( wchar_t * )GlobalAlloc( GMEM_FIXED, sizeof( wchar_t ) * string_length );
					_wmemcpy_s( url, string_length, p, string_length );
					*( url + ( string_length - 1 ) ) = 0;	// Sanity

					p += ( string_length * sizeof( wchar_t ) );

					// Cookies
					string_length = lstrlenA( ( char * )p ) + 1;

					offset += string_length;
					if ( offset >= read ) { goto CLEANUP; }

					// Let's not allocate an empty string.
					if ( string_length > 1 )
					{
						cookies = ( char * )GlobalAlloc( GMEM_FIXED, sizeof( char ) * string_length );
						_memcpy_s( cookies, string_length, p, string_length );
						*( cookies + ( string_length - 1 ) ) = 0;	// Sanity
					}

					p += string_length;

					// Headers
					string_length = lstrlenA( ( char * )p ) + 1;

					offset += string_length;
					if ( offset >= read ) { goto CLEANUP; }

					// Let's not allocate an empty string.
					if ( string_length > 1 )
					{
						headers = ( char * )GlobalAlloc( GMEM_FIXED, sizeof( char ) * string_length );
						_memcpy_s( headers, string_length, p, string_length );
						*( headers + ( string_length - 1 ) ) = 0;	// Sanity
					}

					p += string_length;

					// Data
					string_length = lstrlenA( ( char * )p ) + 1;

					offset += string_length;
					if ( offset >= read ) { goto CLEANUP; }

					// Let's not allocate an empty string.
					if ( string_length > 1 )
					{
						data = ( char * )GlobalAlloc( GMEM_FIXED, sizeof( char ) * string_length );
						_memcpy_s( data, string_length, p, string_length );
						*( data + ( string_length - 1 ) ) = 0;	// Sanity
					}

					p += string_length;

					// Username
					offset += sizeof( int );
					if ( offset >= read ) { goto CLEANUP; }

					// Length of the string - not including the NULL character.
					_memcpy_s( &string_length, sizeof( int ), p, sizeof( int ) );
					p += sizeof( int );

					offset += string_length;
					if ( offset >= read ) { goto CLEANUP; }
					if ( string_length > 0 )
					{
						// string_length does not contain the NULL character of the string.
						username = ( char * )GlobalAlloc( GMEM_FIXED, sizeof( char ) * ( string_length + 1 ) );
						_memcpy_s( username, string_length, p, string_length );
						username[ string_length ] = 0; // Sanity;

						decode_cipher( username, string_length );

						p += string_length;
					}

					// Password
					offset += sizeof( int );
					if ( offset >= read ) { goto CLEANUP; }

					// Length of the string - not including the NULL character.
					_memcpy_s( &string_length, sizeof( int ), p, sizeof( int ) );
					p += sizeof( int );

					offset += string_length;
					if ( offset >= read ) { goto CLEANUP; }
					if ( string_length > 0 )
					{
						// string_length does not contain the NULL character of the string.
						password = ( char * )GlobalAlloc( GMEM_FIXED, sizeof( char ) * ( string_length + 1 ) );
						_memcpy_s( password, string_length, p, string_length );
						password[ string_length ] = 0; // Sanity;

						decode_cipher( password, string_length );

						p += string_length;
					}

					// Range Info.
					offset += sizeof( unsigned char );
					if ( offset <= read )
					{
						range_count = *p;
						p += sizeof( unsigned char );

						for ( unsigned char i = 0; i < range_count; ++i )
						{
							offset += ( sizeof( unsigned long long ) * 5 );
							if ( offset > read ) { goto CLEANUP; }

							RANGE_INFO *ri = ( RANGE_INFO * )GlobalAlloc( GPTR, sizeof( RANGE_INFO ) );

							_memcpy_s( &ri->range_start, sizeof( unsigned long long ), p, sizeof( unsigned long long ) );
							p += sizeof( unsigned long long );

							_memcpy_s( &ri->range_end, sizeof( unsigned long long ), p, sizeof( unsigned long long ) );
							p += sizeof( unsigned long long );

							_memcpy_s( &ri->content_length, sizeof( unsigned long long ), p, sizeof( unsigned long long ) );
							p += sizeof( unsigned long long );

							_memcpy_s( &ri->content_offset, sizeof( unsigned long long ), p, sizeof( unsigned long long ) );
							p += sizeof( unsigned long long );

							_memcpy_s( &ri->file_write_offset, sizeof( unsigned long long ), p, sizeof( unsigned long long ) );
							p += sizeof( unsigned long long );

							DoublyLinkedList *range_node = DLL_CreateNode( ( void * )ri );
							DLL_AddNode( &range_list, range_node, -1 );
						}
					}

					last_entry = offset;	// This value is the ending offset of the last valid entry.

					DOWNLOAD_INFO *di = ( DOWNLOAD_INFO * )GlobalAlloc( GPTR, sizeof( DOWNLOAD_INFO ) );

					di->hFile = INVALID_HANDLE_VALUE;

					di->add_time.QuadPart = add_time.QuadPart;
					di->downloaded = downloaded;
					di->last_downloaded = downloaded;
					di->file_size = file_size;
					di->download_speed_limit = download_speed_limit;
					di->parts = parts;
					di->parts_limit = parts_limit;
					di->status = status;
					di->ssl_version = ssl_version;
					di->processed_header = processed_header;
					di->download_operations = download_operations;
					di->method = method;
					di->last_modified.QuadPart = last_modified.QuadPart;
					di->url = url;
					di->cookies = cookies;
					di->headers = headers;
					di->data = data;
					di->auth_info.username = username;
					di->auth_info.password = password;

					di->range_list = range_list;
					di->print_range_list = di->range_list;

					_wmemcpy_s( di->file_path, MAX_PATH, download_directory, download_directory_length );
					di->file_path[ download_directory_length ] = 0;	// Sanity.

					di->filename_offset = download_directory_length;	// Includes the NULL terminator.

					_wmemcpy_s( di->file_path + di->filename_offset, MAX_PATH - di->filename_offset, filename, filename_length + 1 );
					di->file_path[ di->filename_offset + filename_length + 1 ] = 0;	// Sanity.

					di->file_extension_offset = di->filename_offset + get_file_extension_offset( di->file_path + di->filename_offset, filename_length );

					// Cache our file's icon.
					ICON_INFO *ii = CacheIcon( di, sfi );

					if ( ii != NULL )
					{
						di->icon = &ii->icon;
					}

					InitializeCriticalSection( &di->shared_cs );

					SYSTEMTIME st;
					FILETIME ft;
					ft.dwHighDateTime = di->add_time.HighPart;
					ft.dwLowDateTime = di->add_time.LowPart;
					FileTimeToSystemTime( &ft, &st );

					int buffer_length = 0;

					#ifndef NTDLL_USE_STATIC_LIB
						//buffer_length = 64;	// Should be enough to hold most translated values.
						buffer_length = __snwprintf( NULL, 0, L"%s, %s %d, %04d %d:%02d:%02d %s", GetDay( st.wDayOfWeek ), GetMonth( st.wMonth ), st.wDay, st.wYear, ( st.wHour > 12 ? st.wHour - 12 : ( st.wHour != 0 ? st.wHour : 12 ) ), st.wMinute, st.wSecond, ( st.wHour >= 12 ? L"PM" : L"AM" ) ) + 1;	// Include the NULL character.
					#else
						buffer_length = _scwprintf( L"%s, %s %d, %04d %d:%02d:%02d %s", GetDay( st.wDayOfWeek ), GetMonth( st.wMonth ), st.wDay, st.wYear, ( st.wHour > 12 ? st.wHour - 12 : ( st.wHour != 0 ? st.wHour : 12 ) ), st.wMinute, st.wSecond, ( st.wHour >= 12 ? L"PM" : L"AM" ) ) + 1;	// Include the NULL character.
					#endif

					di->w_add_time = ( wchar_t * )GlobalAlloc( GMEM_FIXED, sizeof( wchar_t ) * buffer_length );

					__snwprintf( di->w_add_time, buffer_length, L"%s, %s %d, %04d %d:%02d:%02d %s", GetDay( st.wDayOfWeek ), GetMonth( st.wMonth ), st.wDay, st.wYear, ( st.wHour > 12 ? st.wHour - 12 : ( st.wHour != 0 ? st.wHour : 12 ) ), st.wMinute, st.wSecond, ( st.wHour >= 12 ? L"PM" : L"AM" ) );


					LVITEM lvi;
					_memzero( &lvi, sizeof( LVITEM ) );
					lvi.mask = LVIF_PARAM | LVIF_TEXT;
					lvi.iItem = ( int )_SendMessageW( g_hWnd_files, LVM_GETITEMCOUNT, 0, 0 );
					lvi.lParam = ( LPARAM )di;
					lvi.pszText = di->file_path + di->filename_offset;
					_SendMessageW( g_hWnd_files, LVM_INSERTITEM, 0, ( LPARAM )&lvi );

					if ( IS_STATUS( di->status, STATUS_PAUSED ) )	// Paused
					{
						di->status = STATUS_STOPPED;	// Stopped
					}
					else if ( IS_STATUS( di->status,
								 STATUS_CONNECTING |
								 STATUS_DOWNLOADING |
								 STATUS_RESTART ) )	// Connecting, Downloading, Queued, or Restarting
					{
						if ( cfg_resume_downloads )
						{
							download_history_changed = true;

							StartDownload( di, false );
						}
						else
						{
							di->status = STATUS_STOPPED;	// Stopped
						}
					}
					else if ( di->status == STATUS_ALLOCATING_FILE )	// If we were allocating the file, then set it to a File IO Error.
					{
						di->status = STATUS_FILE_IO_ERROR;
					}

					continue;

	CLEANUP:
					GlobalFree( url );
					GlobalFree( cookies );
					GlobalFree( headers );
					GlobalFree( data );
					GlobalFree( username );
					GlobalFree( password );

					DoublyLinkedList *range_node;
					while ( range_list != NULL )
					{
						range_node = range_list;
						range_list = range_list->next;

						GlobalFree( range_node->data );
						GlobalFree( range_node );
					}

					// Go back to the last valid entry.
					if ( total_read < fz )
					{
						total_read -= ( read - last_entry );
						SetFilePointer( hFile_read, total_read + 4, NULL, FILE_BEGIN );	// Offset past the magic identifier.
					}

					break;
				}
			}

			GlobalFree( sfi );

			GlobalFree( history_buf );

			if ( cfg_sorted_column_index != COLUMN_NUM )		// #
			{
				SORT_INFO si;
				si.column = GetColumnIndexFromVirtualIndex( cfg_sorted_column_index, download_columns, NUM_COLUMNS );
				si.hWnd = g_hWnd_files;
				si.direction = cfg_sorted_direction;

				_SendMessageW( g_hWnd_files, LVM_SORTITEMS, ( WPARAM )&si, ( LPARAM )( PFNLVCOMPARE )DMCompareFunc );
			}
		}
		else
		{
			ret_status = -2;	// Bad file format.
		}

		CloseHandle( hFile_read );	
	}
	else
	{
		ret_status = -1;	// Can't open file for reading.
	}

	return ret_status;
}

char save_download_history( wchar_t *file_path )
{
	char ret_status = 0;

	HANDLE hFile_downloads = CreateFile( file_path, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL );
	if ( hFile_downloads != INVALID_HANDLE_VALUE )
	{
		//int size = ( 32768 + 1 );
		int size = ( 524288 + 1 );
		int pos = 0;
		DWORD write = 0;

		char *write_buf = ( char * )GlobalAlloc( GMEM_FIXED, sizeof( char ) * size );

		_memcpy_s( write_buf + pos, size - pos, MAGIC_ID_DOWNLOADS, sizeof( char ) * 4 );	// Magic identifier for the call log history.
		pos += ( sizeof( char ) * 4 );

		int item_count = ( int )_SendMessageW( g_hWnd_files, LVM_GETITEMCOUNT, 0, 0 );

		LVITEM lvi;
		_memzero( &lvi, sizeof( LVITEM ) );
		lvi.mask = LVIF_PARAM;

		for ( lvi.iItem = 0; lvi.iItem < item_count; ++lvi.iItem )
		{
			_SendMessageW( g_hWnd_files, LVM_GETITEM, 0, ( LPARAM )&lvi );

			DOWNLOAD_INFO *di = ( DOWNLOAD_INFO * )lvi.lParam;

			// lstrlen is safe for NULL values.
			int download_directory_length = di->filename_offset * sizeof( wchar_t );	// Includes the NULL terminator.
			int filename_length = ( lstrlenW( di->file_path + di->filename_offset ) + 1 ) * sizeof( wchar_t );
			int url_length = ( lstrlenW( di->url ) + 1 ) * sizeof( wchar_t );

			int cookies_length = lstrlenA( di->cookies ) + 1;
			int headers_length = lstrlenA( di->headers ) + 1;
			int data_length = lstrlenA( di->data ) + 1;

			int username_length = lstrlenA( di->auth_info.username );
			int password_length = lstrlenA( di->auth_info.password );

			// See if the next entry can fit in the buffer. If it can't, then we dump the buffer.
			if ( ( signed )( pos + filename_length + download_directory_length + url_length + cookies_length + headers_length + data_length + username_length + password_length +
						   ( sizeof( int ) * 2 ) + ( sizeof( ULONGLONG ) * 2 ) + ( sizeof( unsigned long long ) * 3 ) + ( sizeof( unsigned char ) * 5 ) + sizeof( unsigned int ) + sizeof( bool ) ) > size )
			{
				// Dump the buffer.
				WriteFile( hFile_downloads, write_buf, pos, &write, NULL );
				pos = 0;
			}

			_memcpy_s( write_buf + pos, size - pos, &di->add_time.QuadPart, sizeof( ULONGLONG ) );
			pos += sizeof( ULONGLONG );

			_memcpy_s( write_buf + pos, size - pos, &di->downloaded, sizeof( unsigned long long ) );
			pos += sizeof( unsigned long long );

			_memcpy_s( write_buf + pos, size - pos, &di->file_size, sizeof( unsigned long long ) );
			pos += sizeof( unsigned long long );

			_memcpy_s( write_buf + pos, size - pos, &di->download_speed_limit, sizeof( unsigned long long ) );
			pos += sizeof( unsigned long long );

			_memcpy_s( write_buf + pos, size - pos, &di->parts, sizeof( unsigned char ) );
			pos += sizeof( unsigned char );

			_memcpy_s( write_buf + pos, size - pos, &di->parts_limit, sizeof( unsigned char ) );
			pos += sizeof( unsigned char );

			_memcpy_s( write_buf + pos, size - pos, &di->status, sizeof( unsigned int ) );
			pos += sizeof( unsigned int );

			_memcpy_s( write_buf + pos, size - pos, &di->ssl_version, sizeof( char ) );
			pos += sizeof( char );

			_memcpy_s( write_buf + pos, size - pos, &di->processed_header, sizeof( bool ) );
			pos += sizeof( bool );

			_memcpy_s( write_buf + pos, size - pos, &di->download_operations, sizeof( unsigned char ) );
			pos += sizeof( unsigned char );

			_memcpy_s( write_buf + pos, size - pos, &di->method, sizeof( unsigned char ) );
			pos += sizeof( unsigned char );

			_memcpy_s( write_buf + pos, size - pos, &di->last_modified.QuadPart, sizeof( ULONGLONG ) );
			pos += sizeof( ULONGLONG );

			_memcpy_s( write_buf + pos, size - pos, di->file_path, download_directory_length );
			pos += download_directory_length;

			_memcpy_s( write_buf + pos, size - pos, di->file_path + di->filename_offset, filename_length );
			pos += filename_length;

			_memcpy_s( write_buf + pos, size - pos, di->url, url_length );
			pos += url_length;

			_memcpy_s( write_buf + pos, size - pos, di->cookies, cookies_length );
			pos += cookies_length;

			_memcpy_s( write_buf + pos, size - pos, di->headers, headers_length );
			pos += headers_length;

			_memcpy_s( write_buf + pos, size - pos, di->data, data_length );
			pos += data_length;

			if ( di->auth_info.username != NULL )
			{
				_memcpy_s( write_buf + pos, size - pos, &username_length, sizeof( int ) );
				pos += sizeof( int );

				_memcpy_s( write_buf + pos, size - pos, di->auth_info.username, username_length );
				encode_cipher( write_buf + pos, username_length );
				pos += username_length;
			}
			else
			{
				_memset( write_buf + pos, 0, sizeof( int ) );
				pos += sizeof( int );
			}

			if ( di->auth_info.password != NULL )
			{
				_memcpy_s( write_buf + pos, size - pos, &password_length, sizeof( int ) );
				pos += sizeof( int );

				_memcpy_s( write_buf + pos, size - pos, di->auth_info.password, password_length );
				encode_cipher( write_buf + pos, password_length );
				pos += password_length;
			}
			else
			{
				_memset( write_buf + pos, 0, sizeof( int ) );
				pos += sizeof( int );
			}

			unsigned char range_count = 0;
			DoublyLinkedList *range_node = di->range_list;
			while ( range_node != NULL )
			{
				++range_count;

				range_node = range_node->next;
			}

			// See if the next entry can fit in the buffer. If it can't, then we dump the buffer.
			if ( ( signed )( pos + sizeof( unsigned char ) + ( sizeof( unsigned long long ) * 5 ) ) > size )
			{
				// Dump the buffer.
				WriteFile( hFile_downloads, write_buf, pos, &write, NULL );
				pos = 0;
			}

			_memcpy_s( write_buf + pos, size - pos, &range_count, sizeof( unsigned char ) );
			pos += sizeof( unsigned char );

			range_node = di->range_list;
			while ( range_node != NULL )
			{
				RANGE_INFO *ri = ( RANGE_INFO * )range_node->data;

				//_memcpy_s( write_buf + pos, size - pos, ri, sizeof( RANGE_INFO ) );
				//pos += sizeof( RANGE_INFO );

				_memcpy_s( write_buf + pos, size - pos, &ri->range_start, sizeof( unsigned long long ) );
				pos += sizeof( unsigned long long );

				_memcpy_s( write_buf + pos, size - pos, &ri->range_end, sizeof( unsigned long long ) );
				pos += sizeof( unsigned long long );

				_memcpy_s( write_buf + pos, size - pos, &ri->content_length, sizeof( unsigned long long ) );
				pos += sizeof( unsigned long long );

				_memcpy_s( write_buf + pos, size - pos, &ri->content_offset, sizeof( unsigned long long ) );
				pos += sizeof( unsigned long long );

				_memcpy_s( write_buf + pos, size - pos, &ri->file_write_offset, sizeof( unsigned long long ) );
				pos += sizeof( unsigned long long );

				range_node = range_node->next;
			}
		}

		// If there's anything remaining in the buffer, then write it to the file.
		if ( pos > 0 )
		{
			WriteFile( hFile_downloads, write_buf, pos, &write, NULL );
		}

		GlobalFree( write_buf );

		CloseHandle( hFile_downloads );
	}
	else
	{
		ret_status = -1;	// Can't open file for writing.
	}

	return ret_status;
}

char save_download_history_csv_file( wchar_t *file_path )
{
	char ret_status = 0;

	HANDLE hFile_download_history = CreateFile( file_path, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL );
	if ( hFile_download_history != INVALID_HANDLE_VALUE )
	{
		int size = ( 32768 + 1 );
		int pos = 0;
		DWORD write = 0;
		char unix_timestamp[ 21 ];
		_memzero( unix_timestamp, 21 );
		char file_size[ 21 ];
		_memzero( file_size, 21 );
		char downloaded[ 21 ];
		_memzero( downloaded, 21 );

		char *write_buf = ( char * )GlobalAlloc( GMEM_FIXED, sizeof( char ) * size );

		// Write the UTF-8 BOM and CSV column titles.
		WriteFile( hFile_download_history, "\xEF\xBB\xBF\"Filename\",\"Download Directory\",\"Date and Time Added\",\"Unix Timestamp\",\"Downloaded (bytes)\",\"File Size (bytes)\",\"URL\"", 120, &write, NULL );

		int item_count = ( int )_SendMessageW( g_hWnd_files, LVM_GETITEMCOUNT, 0, 0 );

		LVITEM lvi;
		_memzero( &lvi, sizeof( LVITEM ) );
		lvi.mask = LVIF_PARAM;

		for ( lvi.iItem = 0; lvi.iItem < item_count; ++lvi.iItem )
		{
			_SendMessageW( g_hWnd_files, LVM_GETITEM, 0, ( LPARAM )&lvi );

			DOWNLOAD_INFO *di = ( DOWNLOAD_INFO * )lvi.lParam;

			int download_directory_length = WideCharToMultiByte( CP_UTF8, 0, di->file_path, -1, NULL, 0, NULL, NULL );
			char *utf8_download_directory = ( char * )GlobalAlloc( GMEM_FIXED, sizeof( char ) * download_directory_length ); // Size includes the null character.
			download_directory_length = WideCharToMultiByte( CP_UTF8, 0, di->file_path, -1, utf8_download_directory, download_directory_length, NULL, NULL ) - 1;

			int filename_length = WideCharToMultiByte( CP_UTF8, 0, di->file_path + di->filename_offset, -1, NULL, 0, NULL, NULL );
			char *utf8_filename = ( char * )GlobalAlloc( GMEM_FIXED, sizeof( char ) * filename_length ); // Size includes the null character.
			filename_length = WideCharToMultiByte( CP_UTF8, 0, di->file_path + di->filename_offset, -1, utf8_filename, filename_length, NULL, NULL ) - 1;

			int time_length = WideCharToMultiByte( CP_UTF8, 0, di->w_add_time, -1, NULL, 0, NULL, NULL );
			char *utf8_time = ( char * )GlobalAlloc( GMEM_FIXED, sizeof( char ) * time_length ); // Size includes the null character.
			time_length = WideCharToMultiByte( CP_UTF8, 0, di->w_add_time, -1, utf8_time, time_length, NULL, NULL ) - 1;

			int url_length = WideCharToMultiByte( CP_UTF8, 0, di->url, -1, NULL, 0, NULL, NULL );
			char *utf8_url = ( char * )GlobalAlloc( GMEM_FIXED, sizeof( char ) * url_length ); // Size includes the null character.
			url_length = WideCharToMultiByte( CP_UTF8, 0, di->url, -1, utf8_url, url_length, NULL, NULL ) - 1;

			char *escaped_url = escape_csv( utf8_url );
			if ( escaped_url != NULL )
			{
				GlobalFree( utf8_url );

				utf8_url = escaped_url;
				url_length = lstrlenA( utf8_url );
			}

			// Convert the time into a 32bit Unix timestamp.
			ULARGE_INTEGER date;
			date.HighPart = di->add_time.HighPart;
			date.LowPart = di->add_time.LowPart;

			date.QuadPart -= ( 11644473600000 * 10000 );
#ifdef _WIN64
			date.QuadPart /= FILETIME_TICKS_PER_SECOND;
#else
			// Divide the 64bit value.
			__asm
			{
				xor edx, edx;				//; Zero out the register so we don't divide a full 64bit value.
				mov eax, date.HighPart;		//; We'll divide the high order bits first.
				mov ecx, FILETIME_TICKS_PER_SECOND;
				div ecx;
				mov date.HighPart, eax;		//; Store the high order quotient.
				mov eax, date.LowPart;		//; Now we'll divide the low order bits.
				div ecx;
				mov date.LowPart, eax;		//; Store the low order quotient.
				//; Any remainder will be stored in edx. We're not interested in it though.
			}
#endif
			int timestamp_length = __snprintf( unix_timestamp, 21, "%I64u", date.QuadPart );

			int downloaded_length = __snprintf( downloaded, 21, "%I64u", di->downloaded );
			int file_size_length = __snprintf( file_size, 21, "%I64u", di->file_size );

			// See if the next entry can fit in the buffer. If it can't, then we dump the buffer.
			if ( pos + filename_length + download_directory_length + time_length + timestamp_length + downloaded_length + file_size_length + url_length + 16 > size )
			{
				// Dump the buffer.
				WriteFile( hFile_download_history, write_buf, pos, &write, NULL );
				pos = 0;
			}

			// Add to the buffer.
			write_buf[ pos++ ] = '\r';
			write_buf[ pos++ ] = '\n';

			write_buf[ pos++ ] = '\"';
			_memcpy_s( write_buf + pos, size - pos, utf8_filename, filename_length );
			pos += filename_length;
			write_buf[ pos++ ] = '\"';
			write_buf[ pos++ ] = ',';

			write_buf[ pos++ ] = '\"';
			_memcpy_s( write_buf + pos, size - pos, utf8_download_directory, download_directory_length );
			pos += download_directory_length;
			write_buf[ pos++ ] = '\"';
			write_buf[ pos++ ] = ',';

			write_buf[ pos++ ] = '\"';
			_memcpy_s( write_buf + pos, size - pos, utf8_time, time_length );
			pos += time_length;
			write_buf[ pos++ ] = '\"';
			write_buf[ pos++ ] = ',';

			_memcpy_s( write_buf + pos, size - pos, unix_timestamp, timestamp_length );
			pos += timestamp_length;
			write_buf[ pos++ ] = ',';

			_memcpy_s( write_buf + pos, size - pos, downloaded, downloaded_length );
			pos += downloaded_length;
			write_buf[ pos++ ] = ',';

			_memcpy_s( write_buf + pos, size - pos, file_size, file_size_length );
			pos += file_size_length;
			write_buf[ pos++ ] = ',';

			write_buf[ pos++ ] = '\"';
			_memcpy_s( write_buf + pos, size - pos, utf8_url, url_length );
			pos += url_length;
			write_buf[ pos++ ] = '\"';

			GlobalFree( utf8_download_directory );
			GlobalFree( utf8_filename );
			GlobalFree( utf8_time );
			GlobalFree( utf8_url );
		}

		// If there's anything remaining in the buffer, then write it to the file.
		if ( pos > 0 )
		{
			WriteFile( hFile_download_history, write_buf, pos, &write, NULL );
		}

		GlobalFree( write_buf );

		CloseHandle( hFile_download_history );
	}
	else
	{
		ret_status = -1;	// Can't open file for writing.
	}

	return ret_status;
}

wchar_t *read_url_list_file( wchar_t *file_path, unsigned int &url_list_length )
{
	wchar_t *urls = NULL;

	HANDLE hFile_url_list = CreateFile( file_path, GENERIC_READ, 0, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL );
	if ( hFile_url_list != INVALID_HANDLE_VALUE )
	{
		DWORD read = 0, pos = 0;
		DWORD fz = GetFileSize( hFile_url_list, NULL );

		// http://a.b
		if ( fz >= 10 )
		{
			char *url_list_buf = ( char * )GlobalAlloc( GMEM_FIXED, sizeof( char ) * fz + 1 );

			ReadFile( hFile_url_list, url_list_buf, sizeof( char ) * fz, &read, NULL );

			url_list_buf[ fz ] = 0;	// Guarantee a NULL terminated buffer.

			int length = MultiByteToWideChar( CP_UTF8, 0, url_list_buf, fz + 1, NULL, 0 );
			if ( length > 0 )
			{
				url_list_length = length - 1;
				urls = ( wchar_t * )GlobalAlloc( GMEM_FIXED, sizeof( wchar_t ) * length );
				MultiByteToWideChar( CP_UTF8, 0, url_list_buf, fz + 1, urls, length );
			}

			GlobalFree( url_list_buf );
		}

		CloseHandle( hFile_url_list );
	}

	return urls;
}
