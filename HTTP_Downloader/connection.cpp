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
#include "connection.h"

#include "lite_comdlg32.h"
#include "lite_ole32.h"
#include "lite_shell32.h"
#include "lite_zlib1.h"
#include "lite_normaliz.h"

#include "http_parsing.h"
#include "ftp_parsing.h"

#include "utilities.h"
#include "login_manager_utilities.h"
#include "list_operations.h"

#include "string_tables.h"
#include "cmessagebox.h"

#include "menus.h"

#include "doublylinkedlist.h"

HANDLE g_hIOCP = NULL;

WSAEVENT g_cleanup_event[ 1 ];

bool g_end_program = false;

DoublyLinkedList *g_context_list = NULL;

PCCERT_CONTEXT g_pCertContext = NULL;

SOCKET g_listen_socket = INVALID_SOCKET;
SOCKET_CONTEXT *g_listen_context = NULL;

// Server

char *g_server_domain = NULL;
unsigned short g_server_port = 80;
bool g_server_use_ipv6 = false;

char *g_authentication_username = NULL;
char *g_authentication_password = NULL;
unsigned int g_authentication_username_length = 0;
unsigned int g_authentication_password_length = 0;

char *g_encoded_authentication = NULL;
DWORD g_encoded_authentication_length = 0;

wchar_t *g_server_punycode_hostname = NULL;

extern char *g_nonce = NULL;
unsigned long g_nonce_length = 0;
extern char *g_opaque = NULL;
unsigned long g_opaque_length = 0;

// HTTP Proxy

wchar_t *g_punycode_hostname = NULL;

char *g_proxy_auth_username = NULL;
char *g_proxy_auth_password = NULL;
char *g_proxy_auth_key = NULL;
unsigned long g_proxy_auth_key_length = 0;

// HTTPS Proxy

wchar_t *g_punycode_hostname_s = NULL;

char *g_proxy_auth_username_s = NULL;
char *g_proxy_auth_password_s = NULL;
char *g_proxy_auth_key_s = NULL;
unsigned long g_proxy_auth_key_length_s = 0;

// SOCKS5 Proxy

wchar_t *g_punycode_hostname_socks = NULL;

char *g_proxy_auth_username_socks = NULL;
char *g_proxy_auth_password_socks = NULL;

char *g_proxy_auth_ident_username_socks = NULL;

////////////////////

unsigned long total_downloading = 0;
DoublyLinkedList *download_queue = NULL;

DoublyLinkedList *active_download_list = NULL;		// List of active DOWNLOAD_INFO objects.

DoublyLinkedList *file_size_prompt_list = NULL;		// List of downloads that need to be prompted to continue.
DoublyLinkedList *rename_file_prompt_list = NULL;	// List of downloads that need to be prompted to continue.
DoublyLinkedList *last_modified_prompt_list = NULL;	// List of downloads that need to be prompted to continue.

DoublyLinkedList *move_file_queue = NULL;			// List of downloads that need to be moved to a new folder.

HANDLE g_timeout_semaphore = NULL;

CRITICAL_SECTION context_list_cs;				// Guard access to the global context list.
CRITICAL_SECTION active_download_list_cs;		// Guard access to the global active download list.
CRITICAL_SECTION download_queue_cs;				// Guard access to the download queue.
CRITICAL_SECTION file_size_prompt_list_cs;		// Guard access to the file size prompt list.
CRITICAL_SECTION rename_file_prompt_list_cs;	// Guard access to the rename file prompt list.
CRITICAL_SECTION last_modified_prompt_list_cs;	// Guard access to the last modified prompt list.
CRITICAL_SECTION move_file_queue_cs;			// Guard access to the move file queue.
CRITICAL_SECTION cleanup_cs;

LPFN_ACCEPTEX _AcceptEx = NULL;
LPFN_CONNECTEX _ConnectEx = NULL;

bool file_size_prompt_active = false;
int g_file_size_cmb_ret = 0;	// Message box prompt for large files sizes.

bool rename_file_prompt_active = false;
int g_rename_file_cmb_ret = 0;	// Message box prompt to rename files.
int g_rename_file_cmb_ret2 = 0;	// Message box prompt to rename files.

bool last_modified_prompt_active = false;
int g_last_modified_cmb_ret = 0;	// Message box prompt for modified files.

bool move_file_process_active = false;

unsigned int g_session_status_count[ 8 ] = { 0 };	// 8 states that can be considered finished (Completed, Stopped, Failed, etc.)

bool g_timers_running = false;

void SetSessionStatusCount( unsigned int status )
{
	switch ( status )
	{
		case STATUS_COMPLETED:				{ ++g_session_status_count[ 0 ]; } break;
		case STATUS_STOPPED:				{ ++g_session_status_count[ 1 ]; } break;
		case STATUS_TIMED_OUT:				{ ++g_session_status_count[ 2 ]; } break;
		case STATUS_FAILED:					{ ++g_session_status_count[ 3 ]; } break;
		case STATUS_FILE_IO_ERROR:			{ ++g_session_status_count[ 4 ]; } break;
		case STATUS_SKIPPED:				{ ++g_session_status_count[ 5 ]; } break;
		case STATUS_AUTH_REQUIRED:			{ ++g_session_status_count[ 6 ]; } break;
		case STATUS_PROXY_AUTH_REQUIRED:	{ ++g_session_status_count[ 7 ]; } break;
	}
}

// This should be done in a critical section.
void EnableTimers( bool timer_state )
{
	// Trigger the timers out of their infinite wait.
	if ( timer_state )
	{
		if ( !g_timers_running )
		{
			g_timers_running = true;

			if ( g_timeout_semaphore != NULL )
			{
				ReleaseSemaphore( g_timeout_semaphore, 1, NULL );
			}

			if ( g_timer_semaphore != NULL )
			{
				ReleaseSemaphore( g_timer_semaphore, 1, NULL );
			}
		}
	}
	else	// Let the timers complete their current task and then wait indefinitely.
	{
		UpdateMenus( true );

		g_timers_running = false;
	}
}

DWORD WINAPI Timeout( LPVOID WorkThreadContext )
{
	bool run_timer = g_timers_running;

	while ( !g_end_program )
	{
		// Check the timeout counter every second, or wait indefinitely if we're using the system default.
		WaitForSingleObject( g_timeout_semaphore, ( run_timer ? ( cfg_timeout > 0 ? 1000 : INFINITE ) : INFINITE ) );

		if ( g_end_program )
		{
			break;
		}

		// This will allow the timer to go through at least one loop after it's been disabled (g_timers_running == false).
		run_timer = g_timers_running;

		if ( TryEnterCriticalSection( &context_list_cs ) == TRUE )
		{
			DoublyLinkedList *context_node = g_context_list;

			// Go through the list of active connections.
			while ( context_node != NULL && context_node->data != NULL )
			{
				if ( g_end_program )
				{
					break;
				}

				SOCKET_CONTEXT *context = ( SOCKET_CONTEXT * )context_node->data;

				if ( TryEnterCriticalSection( &context->context_cs ) == TRUE )
				{
					if ( context->cleanup == 0 && context->status != STATUS_ALLOCATING_FILE )
					{
						// Don't increment the Control connection's timeout value.
						// It'll be forced to time out if the Data connection times out.
						if ( context->ftp_context != NULL && context->ftp_connection_type & FTP_CONNECTION_TYPE_CONTROL )
						{
							if ( cfg_ftp_send_keep_alive && context->ftp_connection_type == FTP_CONNECTION_TYPE_CONTROL )
							{
								InterlockedIncrement( &context->keep_alive_timeout );	// Increment the timeout counter.

								if ( context->keep_alive_timeout >= 30 )
								{
									InterlockedExchange( &context->keep_alive_timeout, 0 );	// Reset timeout counter.

									SendFTPKeepAlive( context );
								}
							}
						}
						else
						{
							InterlockedIncrement( &context->timeout );	// Increment the timeout counter.

							// See if we've reached the timeout limit.
							if ( ( context->timeout >= cfg_timeout ) && ( cfg_timeout > 0 ) )
							{
								// Ignore paused and queued downloads.
								if ( IS_STATUS( context->status, STATUS_PAUSED | STATUS_QUEUED ) )
								{
									InterlockedExchange( &context->timeout, 0 );	// Reset timeout counter.
								}
								else
								{
									context->timed_out = TIME_OUT_TRUE;

									context->cleanup = 2;	// Force the cleanup.

									InterlockedIncrement( &context->pending_operations );

									context->overlapped_close.current_operation = ( context->ssl != NULL ? IO_Shutdown : IO_Close );

									PostQueuedCompletionStatus( g_hIOCP, 0, ( ULONG_PTR )context, ( OVERLAPPED * )&context->overlapped_close );
								}
							}
						}
					}

					LeaveCriticalSection( &context->context_cs );
				}

				context_node = context_node->next;
			}

			LeaveCriticalSection( &context_list_cs );
		}
	}

	CloseHandle( g_timeout_semaphore );
	g_timeout_semaphore = NULL;

	_ExitThread( 0 );
	return 0;
}

void InitializeServerInfo()
{
	if ( cfg_server_enable_ssl )
	{
		if ( cfg_certificate_type == 1 )	// Public/Private Key Pair.
		{
			g_pCertContext = LoadPublicPrivateKeyPair( cfg_certificate_cer_file_name, cfg_certificate_key_file_name );
		}
		else	// PKCS #12 File.
		{
			g_pCertContext = LoadPKCS12( cfg_certificate_pkcs_file_name, cfg_certificate_pkcs_password );
		}
	}

	if ( cfg_use_authentication )
	{
		if ( cfg_authentication_username != NULL )
		{
			g_authentication_username_length = WideCharToMultiByte( CP_UTF8, 0, cfg_authentication_username, -1, NULL, 0, NULL, NULL );
			g_authentication_username = ( char * )GlobalAlloc( GMEM_FIXED, sizeof( char ) * g_authentication_username_length ); // Size includes the null character.
			g_authentication_username_length = WideCharToMultiByte( CP_UTF8, 0, cfg_authentication_username, -1, g_authentication_username, g_authentication_username_length, NULL, NULL ) - 1;
		}

		if ( cfg_authentication_password != NULL )
		{
			g_authentication_password_length = WideCharToMultiByte( CP_UTF8, 0, cfg_authentication_password, -1, NULL, 0, NULL, NULL );
			g_authentication_password = ( char * )GlobalAlloc( GMEM_FIXED, sizeof( char ) * g_authentication_password_length ); // Size includes the null character.
			g_authentication_password_length = WideCharToMultiByte( CP_UTF8, 0, cfg_authentication_password, -1, g_authentication_password, g_authentication_password_length, NULL, NULL ) - 1;
		}

		if ( cfg_authentication_type == AUTH_TYPE_DIGEST )
		{
			CreateDigestAuthorizationInfo( &g_nonce, g_nonce_length, &g_opaque, g_opaque_length );
		}
		else
		{
			CreateBasicAuthorizationKey( g_authentication_username, g_authentication_username_length, g_authentication_password, g_authentication_password_length, &g_encoded_authentication, &g_encoded_authentication_length );
		}
	}

	if ( normaliz_state == NORMALIZ_STATE_RUNNING )
	{
		if ( cfg_server_address_type == 0 )	// Hostname.
		{
			int hostname_length = lstrlenW( cfg_server_hostname ) + 1;	// Include the NULL terminator.
			int punycode_length = _IdnToAscii( 0, cfg_server_hostname, hostname_length, NULL, 0 );

			if ( punycode_length > hostname_length )
			{
				g_server_punycode_hostname = ( wchar_t * )GlobalAlloc( GMEM_FIXED, sizeof( wchar_t ) * punycode_length );
				_IdnToAscii( 0, cfg_server_hostname, hostname_length, g_server_punycode_hostname, punycode_length );
			}
		}
	}
}

void CleanupServerInfo()
{
	if ( g_authentication_username != NULL )
	{
		GlobalFree( g_authentication_username );
		g_authentication_username = NULL;
	}

	g_authentication_username_length = 0;

	if ( g_authentication_password != NULL )
	{
		GlobalFree( g_authentication_password );
		g_authentication_password = NULL;
	}

	g_authentication_password_length = 0;

	if ( g_encoded_authentication != NULL )
	{
		GlobalFree( g_encoded_authentication );
		g_encoded_authentication = NULL;
	}

	if ( g_nonce != NULL )
	{
		GlobalFree( g_nonce );
		g_nonce = NULL;
	}

	g_nonce_length = 0;

	if ( g_opaque != NULL )
	{
		GlobalFree( g_opaque );
		g_opaque = NULL;
	}

	g_opaque_length = 0;

	if ( g_server_punycode_hostname != NULL )
	{
		GlobalFree( g_server_punycode_hostname );
		g_server_punycode_hostname = NULL;
	}

	if ( g_server_domain != NULL )
	{
		GlobalFree( g_server_domain );
		g_server_domain = NULL;
	}

	if ( cfg_server_enable_ssl )
	{
		if ( g_pCertContext != NULL )
		{
			_CertFreeCertificateContext( g_pCertContext );
			g_pCertContext = NULL;
		}
	}
}

void StartServer()
{
	g_listen_socket = CreateListenSocket();

	// Create the accept socket.
	if ( g_listen_socket != INVALID_SOCKET )
	{
		CreateAcceptSocket( g_listen_socket, g_server_use_ipv6 );
	}
}

void CleanupServer()
{
	// When we shutdown/close g_listen_socket, g_listen_context will complete with a status of FALSE and we can then clean it up.
	if ( g_listen_socket != INVALID_SOCKET )
	{
		SOCKET del_listen_socket = g_listen_socket;
		g_listen_socket = INVALID_SOCKET;

		g_listen_context = NULL;	// Freed in CleanupConnection.

		_shutdown( del_listen_socket, SD_BOTH );
		_closesocket( del_listen_socket );
	}
}

DWORD WINAPI IOCPDownloader( LPVOID pArgs )
{
	HANDLE *g_ThreadHandles = NULL;

	DWORD dwThreadCount = cfg_thread_count;

	if ( ws2_32_state == WS2_32_STATE_SHUTDOWN )
	{
		#ifndef WS2_32_USE_STATIC_LIB
			if ( !InitializeWS2_32() ){ goto HARD_CLEANUP; }
		#else
			StartWS2_32();
		#endif
	}

	// Loads the CreateEx function pointer. Required for overlapped connects.
	if ( !LoadConnectEx() )
	{
		goto HARD_CLEANUP;
	}

	// Load our SSL functions.
	if ( ssl_state == SSL_STATE_SHUTDOWN )
	{
		if ( SSL_library_init() == 0 )
		{
			goto HARD_CLEANUP;
		}
	}

	if ( cfg_enable_server )
	{
		InitializeServerInfo();
	}

	g_ThreadHandles = ( HANDLE * )GlobalAlloc( GMEM_FIXED, sizeof( HANDLE ) * dwThreadCount );
	for ( unsigned int i = 0; i < dwThreadCount; ++i )
	{
		g_ThreadHandles[ i ] = INVALID_HANDLE_VALUE;
	}

	g_cleanup_event[ 0 ] = _WSACreateEvent();
	if ( g_cleanup_event[ 0 ] == WSA_INVALID_EVENT )
	{
		goto CLEANUP;
	}

	g_hIOCP = CreateIoCompletionPort( INVALID_HANDLE_VALUE, NULL, 0, 0 );
    if ( g_hIOCP == NULL )
	{
		goto CLEANUP;
	}

	_WSAResetEvent( g_cleanup_event[ 0 ] );

	// Spawn our IOCP worker threads.
	for ( DWORD dwCPU = 0; dwCPU < dwThreadCount; ++dwCPU )
	{
		HANDLE hThread;
		DWORD dwThreadId;

		// Create worker threads to service the overlapped I/O requests.
		hThread = _CreateThread( NULL, 0, IOCPConnection, g_hIOCP, 0, &dwThreadId );
		if ( hThread == NULL )
		{
			break;
		}

		g_ThreadHandles[ dwCPU ] = hThread;
		hThread = INVALID_HANDLE_VALUE;
	}

	if ( cfg_enable_server )
	{
		StartServer();
	}

	if ( downloader_ready_semaphore != NULL )
	{
		ReleaseSemaphore( downloader_ready_semaphore, 1, NULL );
	}

	g_timeout_semaphore = CreateSemaphore( NULL, 0, 1, NULL );

	//CloseHandle( _CreateThread( NULL, 0, Timeout, NULL, 0, NULL ) );
	HANDLE timeout_handle = _CreateThread( NULL, 0, Timeout, NULL, 0, NULL );
	SetThreadPriority( timeout_handle, THREAD_PRIORITY_LOWEST );
	CloseHandle( timeout_handle );

	_WSAWaitForMultipleEvents( 1, g_cleanup_event, TRUE, WSA_INFINITE, FALSE );

	g_end_program = true;

	// Causes the IOCP worker threads to exit.
	if ( g_hIOCP != NULL )
	{
		for ( DWORD i = 0; i < dwThreadCount; ++i )
		{
			PostQueuedCompletionStatus( g_hIOCP, 0, 0, NULL );
		}
	}

	// Make sure IOCP worker threads have exited.
	if ( WaitForMultipleObjects( dwThreadCount, g_ThreadHandles, TRUE, 1000 ) == WAIT_OBJECT_0 )
	{
		for ( DWORD i = 0; i < dwThreadCount; ++i )
		{
			if ( g_ThreadHandles[ i ] != INVALID_HANDLE_VALUE )
			{
				CloseHandle( g_ThreadHandles[ i ] );
				g_ThreadHandles[ i ] = INVALID_HANDLE_VALUE;
			}
		}
	}

	if ( g_timeout_semaphore != NULL )
	{
		ReleaseSemaphore( g_timeout_semaphore, 1, NULL );
	}

	if ( g_listen_socket != INVALID_SOCKET )
	{
		_shutdown( g_listen_socket, SD_BOTH );
		_closesocket( g_listen_socket );
		g_listen_socket = INVALID_SOCKET;
	}

	// Clean up our listen context.
	FreeListenContext();

	// Clean up our context list.
	FreeContexts();

	download_queue = NULL;
	total_downloading = 0;

	if ( g_hIOCP != NULL )
	{
		CloseHandle( g_hIOCP );
		g_hIOCP = NULL;
	}

CLEANUP:

	if ( g_ThreadHandles != NULL )
	{
		GlobalFree( g_ThreadHandles );
		g_ThreadHandles = NULL;
	}

	if ( g_cleanup_event[ 0 ] != WSA_INVALID_EVENT )
	{
		_WSACloseEvent( g_cleanup_event[ 0 ] );
		g_cleanup_event[ 0 ] = WSA_INVALID_EVENT;
	}

HARD_CLEANUP:

	if ( downloader_ready_semaphore != NULL )
	{
		ReleaseSemaphore( downloader_ready_semaphore, 1, NULL );
	}

	_ExitThread( 0 );
	return 0;
}

bool LoadConnectEx()
{
	bool ret = false;

	DWORD bytes = 0;
	GUID connectex_guid = WSAID_CONNECTEX;

	if ( _ConnectEx == NULL )
	{
		SOCKET tmp_socket = CreateSocket();
		if ( tmp_socket != INVALID_SOCKET )
		{
			// Load the ConnectEx extension function from the provider for this socket.
			ret = ( _WSAIoctl( tmp_socket, SIO_GET_EXTENSION_FUNCTION_POINTER, &connectex_guid, sizeof( connectex_guid ), &_ConnectEx, sizeof( _ConnectEx ), &bytes, NULL, NULL ) == SOCKET_ERROR ? false : true );

			_closesocket( tmp_socket );
		}
	}
	else
	{
		ret = true;
	}

	return ret;
}

SOCKET_CONTEXT *UpdateCompletionPort( SOCKET socket, bool use_ssl, unsigned char ssl_version, bool add_context, bool is_server )
{
	SOCKET_CONTEXT *context = CreateSocketContext();
	if ( context )
	{
		context->socket = socket;

		context->overlapped.current_operation = IO_Accept;

		// Create an SSL/TLS object for incoming SSL/TLS connections, but not for SSL/TLS tunnel connections.
		if ( use_ssl )
		{
			DWORD protocol = 0;
			switch ( ssl_version )
			{
				case 4:	protocol |= SP_PROT_TLS1_2;
				case 3:	protocol |= SP_PROT_TLS1_1;
				case 2:	protocol |= SP_PROT_TLS1;
				case 1:	protocol |= SP_PROT_SSL3;
				case 0:	{ if ( ssl_version < 4 ) { protocol |= SP_PROT_SSL2; } }
			}

			SSL *ssl = SSL_new( protocol, is_server );
			if ( ssl == NULL )
			{
				DeleteCriticalSection( &context->context_cs );

				if ( context->buffer != NULL ) { GlobalFree( context->buffer ); }

				GlobalFree( context );
				context = NULL;

				return NULL;
			}

			ssl->s = socket;

			context->ssl = ssl;
		}

		g_hIOCP = CreateIoCompletionPort( ( HANDLE )socket, g_hIOCP, ( DWORD_PTR )context, 0 );
		if ( g_hIOCP != NULL )
		{
			if ( add_context )
			{
				RANGE_INFO *ri = ( RANGE_INFO * )GlobalAlloc( GPTR, sizeof( RANGE_INFO ) );
				context->header_info.range_info = ri;

				context->context_node.data = context;

				EnterCriticalSection( &context_list_cs );

				// Add to the global download list.
				DLL_AddNode( &g_context_list, &context->context_node, 0 );

				LeaveCriticalSection( &context_list_cs );
			}
		}
		else
		{
			if ( context->ssl != NULL ) { SSL_free( context->ssl ); }

			DeleteCriticalSection( &context->context_cs );

			if ( context->buffer != NULL ) { GlobalFree( context->buffer ); }

			GlobalFree( context );
			context = NULL;
		}
	}

	return context;
}

SOCKET CreateListenSocket()
{
	int nRet = 0;

	DWORD bytes = 0;
	GUID acceptex_guid = WSAID_ACCEPTEX;	// GUID to Microsoft specific extensions

	struct addrinfoW hints;
	struct addrinfoW *addrlocal = NULL;

	// Resolve the interface
	_memzero( &hints, sizeof( addrinfoW ) );
	hints.ai_flags = AI_PASSIVE;
	hints.ai_family = AF_INET;
	hints.ai_socktype = SOCK_STREAM;
	hints.ai_protocol = IPPROTO_IP;

	SOCKET listen_socket = INVALID_SOCKET;

	wchar_t cport[ 6 ];
	__snwprintf( cport, 6, L"%hu", cfg_server_port );

	g_server_use_ipv6 = false;

	g_server_port = cfg_server_port;

	if ( g_server_domain != NULL )
	{
		GlobalFree( g_server_domain );
		g_server_domain = NULL;
	}

	// Use Hostname or IPv6 Address.
	if ( cfg_server_address_type == 0 )
	{
		wchar_t *hostname = ( g_server_punycode_hostname != NULL ? g_server_punycode_hostname : cfg_server_hostname );

		g_server_domain = GetUTF8Domain( hostname );

		nRet = _GetAddrInfoW( hostname, cport, &hints, &addrlocal );
		if ( nRet == WSAHOST_NOT_FOUND )
		{
			g_server_use_ipv6 = true;

			hints.ai_family = AF_INET6;	// Try IPv6
			nRet = _GetAddrInfoW( hostname, cport, &hints, &addrlocal );
		}

		if ( nRet != 0 )
		{
			goto CLEANUP;
		}

		// Check the IPv6 address' formatting. It should be surrounded by brackets.
		// GetAddrInfoW supports it with or without, but we want it to have it.
		if ( g_server_use_ipv6 )
		{
			if ( g_server_domain != NULL && *g_server_domain != '[' )
			{
				int g_server_domain_length = lstrlenA( g_server_domain );
				char *new_g_server_domain = ( char * )GlobalAlloc( GMEM_FIXED, sizeof( char ) * ( g_server_domain_length + 3 ) );	// 2 brackets and the NULL character.
				new_g_server_domain[ 0 ] = '[';
				_memcpy_s( new_g_server_domain + 1, g_server_domain_length + 2, g_server_domain, g_server_domain_length );
				new_g_server_domain[ g_server_domain_length + 1 ] = ']';
				new_g_server_domain[ g_server_domain_length + 2 ] = 0;	// Sanity.

				GlobalFree( g_server_domain );
				g_server_domain = new_g_server_domain;
			}
		}
	}
	else	// Use IPv4 Address.
	{
		struct sockaddr_in src_addr;
		_memzero( &src_addr, sizeof( sockaddr_in ) );

		src_addr.sin_family = AF_INET;
		src_addr.sin_addr.s_addr = _htonl( cfg_server_ip_address );

		wchar_t wcs_ip[ 16 ];
		DWORD wcs_ip_length = 16;
		_WSAAddressToStringW( ( SOCKADDR * )&src_addr, sizeof( struct sockaddr_in ), NULL, wcs_ip, &wcs_ip_length );

		g_server_domain = GetUTF8Domain( wcs_ip );

		if ( _GetAddrInfoW( wcs_ip, cport, &hints, &addrlocal ) != 0 )
		{
			goto CLEANUP;
		}
	}

	if ( addrlocal == NULL )
	{
		goto CLEANUP;
	}

	SOCKET socket = CreateSocket( g_server_use_ipv6 );
	if ( socket == INVALID_SOCKET )
	{
		goto CLEANUP;
	}

	nRet = _bind( socket, addrlocal->ai_addr, ( int )addrlocal->ai_addrlen );
	if ( nRet == SOCKET_ERROR )
	{
		goto CLEANUP;
	}

	nRet = _listen( socket, SOMAXCONN );
	if ( nRet == SOCKET_ERROR )
	{
		goto CLEANUP;
	}

	// We need only do this once.
	if ( _AcceptEx == NULL )
	{
		// Load the AcceptEx extension function from the provider.
		// It doesn't matter what socket we use so long as it's valid.
		nRet = _WSAIoctl( socket, SIO_GET_EXTENSION_FUNCTION_POINTER, &acceptex_guid, sizeof( acceptex_guid ), &_AcceptEx, sizeof( _AcceptEx ), &bytes, NULL, NULL );
		if ( nRet == SOCKET_ERROR )
		{
			goto CLEANUP;
		}
	}

	listen_socket = socket;
	socket = INVALID_SOCKET;

CLEANUP:

	if ( socket != INVALID_SOCKET )
	{
		_closesocket( socket );
	}

	if ( addrlocal != NULL )
	{
		_FreeAddrInfoW( addrlocal );
	}

	return listen_socket;
}

char CreateAcceptSocket( SOCKET listen_socket, bool use_ipv6 )
{
	int nRet = 0;
	DWORD dwRecvNumBytes = 0;

	if ( g_listen_context == NULL )
	{
		g_listen_context = UpdateCompletionPort( listen_socket, cfg_server_enable_ssl, cfg_server_ssl_version, false, true );
		if ( g_listen_context == NULL )
		{
			return LA_STATUS_FAILED;
		}
	}

	// The accept socket will inherit the listen socket's properties when it completes. IPv6 doesn't actually have to be set here.
	g_listen_context->socket = CreateSocket( use_ipv6 );
	if ( g_listen_context->socket == INVALID_SOCKET )
	{
		return LA_STATUS_FAILED;
	}

	InterlockedIncrement( &g_listen_context->pending_operations );

	// Accept a connection without waiting for any data. (dwReceiveDataLength = 0)
	nRet = _AcceptEx( listen_socket, g_listen_context->socket, ( LPVOID )( g_listen_context->buffer ), 0, sizeof( SOCKADDR_STORAGE ) + 16, sizeof( SOCKADDR_STORAGE ) + 16, &dwRecvNumBytes, ( OVERLAPPED * )&g_listen_context->overlapped );
	if ( nRet == SOCKET_ERROR && ( _WSAGetLastError() != ERROR_IO_PENDING ) )
	{
		InterlockedDecrement( &g_listen_context->pending_operations );

		return LA_STATUS_FAILED;
	}

	return LA_STATUS_OK;
}

SOCKET CreateSocket( bool IPv6 )
{
	int nZero = 0;
	SOCKET socket = INVALID_SOCKET;

	socket = _WSASocketW( ( IPv6 ? AF_INET6 : AF_INET ), SOCK_STREAM, IPPROTO_IP, NULL, 0, WSA_FLAG_OVERLAPPED ); 
	if ( socket != INVALID_SOCKET )
	{
		// Disable send buffering on the socket.
		_setsockopt( socket, SOL_SOCKET, SO_SNDBUF, ( char * )&nZero, sizeof( nZero ) );
	}

	return socket;
}

SECURITY_STATUS DecryptRecv( SOCKET_CONTEXT *context, DWORD &io_size )
{
	SECURITY_STATUS scRet = SEC_E_INTERNAL_ERROR;

	WSABUF wsa_decrypt;

	DWORD bytes_decrypted = 0;

	if ( context->ssl->rd.scRet == SEC_E_INCOMPLETE_MESSAGE )
	{
		context->ssl->cbIoBuffer += io_size;
	}
	else
	{
		context->ssl->cbIoBuffer = io_size;
	}

	io_size = 0;
	
	context->ssl->continue_decrypt = false;

	wsa_decrypt = context->wsabuf;

	// Decrypt our buffer.
	while ( context->ssl->pbIoBuffer != NULL /*&& context->ssl->cbIoBuffer > 0*/ )
	{
		scRet = SSL_WSARecv_Decrypt( context->ssl, &wsa_decrypt, bytes_decrypted );

		io_size += bytes_decrypted;

		wsa_decrypt.buf += bytes_decrypted;
		wsa_decrypt.len -= bytes_decrypted;

		switch ( scRet )
		{
			// We've successfully decrypted a portion of the buffer.
			case SEC_E_OK:
			{
				// Decrypt more records if there are any.
				continue;
			}
			break;

			// The message was decrypted, but not all of it was copied to our wsabuf.
			// There may be incomplete records left to decrypt. DecryptRecv must be called again after processing wsabuf.
			case SEC_I_CONTINUE_NEEDED:
			{
				context->ssl->continue_decrypt = true;

				return scRet;
			}
			break;

			case SEC_E_INCOMPLETE_MESSAGE:	// The message was incomplete. Request more data from the server.
			case SEC_I_RENEGOTIATE:			// Client wants us to perform another handshake.
			{
				return scRet;
			}
			break;

			//case SEC_I_CONTEXT_EXPIRED:
			default:
			{
				context->ssl->cbIoBuffer = 0;

				return scRet;
			}
			break;
		}
	}

	context->ssl->cbIoBuffer = 0;

	return scRet;
}

THREAD_RETURN RenameFilePrompt( void *pArguments )
{
	SOCKET_CONTEXT *context = NULL;

	unsigned char rename_only = ( unsigned char )pArguments;

	bool skip_processing = false;

	do
	{
		EnterCriticalSection( &rename_file_prompt_list_cs );

		DoublyLinkedList *context_node = rename_file_prompt_list;

		DLL_RemoveNode( &rename_file_prompt_list, context_node );

		if ( context_node != NULL )
		{
			context = ( SOCKET_CONTEXT * )context_node->data;

			GlobalFree( context_node );
		}

		LeaveCriticalSection( &rename_file_prompt_list_cs );

		DOWNLOAD_INFO *di = context->download_info;
		if ( di != NULL )
		{
			wchar_t prompt_message[ MAX_PATH + 512 ];
			wchar_t file_path[ MAX_PATH ];

			int filename_offset;
			int file_extension_offset;

			if ( cfg_use_temp_download_directory )
			{
				int filename_length = GetTemporaryFilePath( di, file_path );

				filename_offset = g_temp_download_directory_length + 1;
				file_extension_offset = filename_offset + get_file_extension_offset( di->file_path + di->filename_offset, filename_length );
			}
			else
			{
				filename_offset = di->filename_offset;
				file_extension_offset = di->file_extension_offset;

				GetDownloadFilePath( di, file_path );
			}

			if ( rename_only == 0 )
			{
				// If the last return value was not set to remember our choice, then prompt again.
				if ( g_rename_file_cmb_ret != CMBIDRENAMEALL && g_rename_file_cmb_ret != CMBIDOVERWRITEALL && g_rename_file_cmb_ret != CMBIDSKIPALL )
				{
					__snwprintf( prompt_message, MAX_PATH + 512, ST_V_PROMPT___already_exists, file_path );

					g_rename_file_cmb_ret = CMessageBoxW( g_hWnd_main, prompt_message, PROGRAM_CAPTION, CMB_ICONWARNING | CMB_RENAMEOVERWRITESKIPALL );
				}
			}

			// Rename the file and try again.
			if ( rename_only == 1 || g_rename_file_cmb_ret == CMBIDRENAME || g_rename_file_cmb_ret == CMBIDRENAMEALL )
			{
				// Creates a tree of active and queued downloads.
				dllrbt_tree *add_files_tree = CreateFilenameTree();

				bool rename_succeeded;

				EnterCriticalSection( &di->shared_cs );

				rename_succeeded = RenameFile( di, add_files_tree, file_path, filename_offset, file_extension_offset );

				LeaveCriticalSection( &di->shared_cs );

				// The tree is only used to determine duplicate filenames.
				DestroyFilenameTree( add_files_tree );

				if ( !rename_succeeded )
				{
					if ( g_rename_file_cmb_ret2 != CMBIDOKALL && !( di->download_operations & DOWNLOAD_OPERATION_OVERRIDE_PROMPTS ) )
					{
						__snwprintf( prompt_message, MAX_PATH + 512, ST_V_PROMPT___could_not_be_renamed, file_path );

						g_rename_file_cmb_ret2 = CMessageBoxW( g_hWnd_main, prompt_message, PROGRAM_CAPTION, CMB_ICONWARNING | CMB_OKALL );
					}

					EnterCriticalSection( &context->context_cs );

					context->status = STATUS_FILE_IO_ERROR;

					if ( context->cleanup == 0 )
					{
						context->cleanup = 1;	// Auto cleanup.

						InterlockedIncrement( &context->pending_operations );

						context->overlapped_close.current_operation = ( context->ssl != NULL ? IO_Shutdown : IO_Close );

						PostQueuedCompletionStatus( g_hIOCP, 0, ( ULONG_PTR )context, ( OVERLAPPED * )&context->overlapped_close );
					}

					LeaveCriticalSection( &context->context_cs );
				}
				else	// Continue where we left off when getting the content.
				{
					EnterCriticalSection( &context->context_cs );

					InterlockedIncrement( &context->pending_operations );

					context->overlapped.current_operation = IO_ResumeGetContent;

					PostQueuedCompletionStatus( g_hIOCP, context->current_bytes_read, ( ULONG_PTR )context, ( OVERLAPPED * )&context->overlapped );

					LeaveCriticalSection( &context->context_cs );
				}
			}
			else if ( g_rename_file_cmb_ret == CMBIDFAIL || g_rename_file_cmb_ret == CMBIDSKIP || g_rename_file_cmb_ret == CMBIDSKIPALL ) // Skip the rename or overwrite if the return value fails, or the user selected skip.
			{
				EnterCriticalSection( &context->context_cs );

				context->status = STATUS_SKIPPED;

				if ( context->cleanup == 0 )
				{
					context->cleanup = 1;	// Auto cleanup.

					InterlockedIncrement( &context->pending_operations );

					context->overlapped_close.current_operation = ( context->ssl != NULL ? IO_Shutdown : IO_Close );

					PostQueuedCompletionStatus( g_hIOCP, 0, ( ULONG_PTR )context, ( OVERLAPPED * )&context->overlapped_close );
				}

				LeaveCriticalSection( &context->context_cs );
			}
			else	// Continue where we left off when getting the content.
			{
				EnterCriticalSection( &context->context_cs );

				InterlockedIncrement( &context->pending_operations );

				context->overlapped.current_operation = IO_ResumeGetContent;

				PostQueuedCompletionStatus( g_hIOCP, context->current_bytes_read, ( ULONG_PTR )context, ( OVERLAPPED * )&context->overlapped );

				LeaveCriticalSection( &context->context_cs );
			}
		}

		EnterCriticalSection( &rename_file_prompt_list_cs );

		if ( rename_file_prompt_list == NULL )
		{
			skip_processing = true;

			rename_file_prompt_active = false;
		}

		LeaveCriticalSection( &rename_file_prompt_list_cs );
	}
	while ( !skip_processing );

	_ExitThread( 0 );
	return 0;
}

THREAD_RETURN FileSizePrompt( void *pArguments )
{
	SOCKET_CONTEXT *context = NULL;

	bool skip_processing = false;

	do
	{
		EnterCriticalSection( &file_size_prompt_list_cs );

		DoublyLinkedList *context_node = file_size_prompt_list;

		DLL_RemoveNode( &file_size_prompt_list, context_node );

		if ( context_node != NULL )
		{
			context = ( SOCKET_CONTEXT * )context_node->data;

			GlobalFree( context_node );
		}

		LeaveCriticalSection( &file_size_prompt_list_cs );

		DOWNLOAD_INFO *di = context->download_info;
		if ( di != NULL )
		{
			// If we don't want to prevent all large downloads, then prompt the user.
			if ( g_file_size_cmb_ret != CMBIDNOALL && g_file_size_cmb_ret != CMBIDYESALL )
			{
				wchar_t file_path[ MAX_PATH ];
				if ( cfg_use_temp_download_directory )
				{
					GetTemporaryFilePath( di, file_path );
				}
				else
				{
					GetDownloadFilePath( di, file_path );
				}

				wchar_t prompt_message[ MAX_PATH + 512 ];
				__snwprintf( prompt_message, MAX_PATH + 512, ST_V_PROMPT___will_be___size, file_path, di->file_size );
				g_file_size_cmb_ret = CMessageBoxW( g_hWnd_main, prompt_message, PROGRAM_CAPTION, CMB_ICONWARNING | CMB_YESNOALL );
			}

			EnterCriticalSection( &context->context_cs );

			// Close all large downloads.
			if ( g_file_size_cmb_ret == CMBIDNO || g_file_size_cmb_ret == CMBIDNOALL )
			{
				context->header_info.range_info->content_length = 0;
				context->header_info.range_info->range_start = 0;
				context->header_info.range_info->range_end = 0;
				context->header_info.range_info->content_offset = 0;
				context->header_info.range_info->file_write_offset = 0;

				context->status = STATUS_SKIPPED;

				if ( context->cleanup == 0 )
				{
					context->cleanup = 1;	// Auto cleanup.

					InterlockedIncrement( &context->pending_operations );

					context->overlapped_close.current_operation = ( context->ssl != NULL ? IO_Shutdown : IO_Close );

					PostQueuedCompletionStatus( g_hIOCP, 0, ( ULONG_PTR )context, ( OVERLAPPED * )&context->overlapped_close );
				}
			}
			else	// Continue where we left off when getting the content.
			{
				InterlockedIncrement( &context->pending_operations );

				context->overlapped.current_operation = IO_ResumeGetContent;

				PostQueuedCompletionStatus( g_hIOCP, context->current_bytes_read, ( ULONG_PTR )context, ( OVERLAPPED * )&context->overlapped );
			}

			LeaveCriticalSection( &context->context_cs );
		}

		EnterCriticalSection( &file_size_prompt_list_cs );

		if ( file_size_prompt_list == NULL )
		{
			skip_processing = true;

			file_size_prompt_active = false;
		}

		LeaveCriticalSection( &file_size_prompt_list_cs );
	}
	while ( !skip_processing );

	_ExitThread( 0 );
	return 0;
}

THREAD_RETURN LastModifiedPrompt( void *pArguments )
{
	SOCKET_CONTEXT *context = NULL;

	unsigned char restart_only = ( unsigned char )pArguments;

	bool skip_processing = false;

	do
	{
		EnterCriticalSection( &last_modified_prompt_list_cs );

		DoublyLinkedList *context_node = last_modified_prompt_list;

		DLL_RemoveNode( &last_modified_prompt_list, context_node );

		if ( context_node != NULL )
		{
			context = ( SOCKET_CONTEXT * )context_node->data;

			GlobalFree( context_node );
		}

		LeaveCriticalSection( &last_modified_prompt_list_cs );

		DOWNLOAD_INFO *di = context->download_info;
		if ( di != NULL )
		{
			wchar_t prompt_message[ MAX_PATH + 512 ];

			wchar_t file_path[ MAX_PATH ];
			if ( cfg_use_temp_download_directory )
			{
				GetTemporaryFilePath( di, file_path );
			}
			else
			{
				GetDownloadFilePath( di, file_path );
			}

			if ( restart_only == 0 )
			{
				// If the last return value was not set to remember our choice, then prompt again.
				if ( g_last_modified_cmb_ret != CMBIDCONTINUEALL && g_last_modified_cmb_ret != CMBIDRESTARTALL && g_last_modified_cmb_ret != CMBIDSKIPALL )
				{
					__snwprintf( prompt_message, MAX_PATH + 512, ST_V_PROMPT___has_been_modified, file_path );

					g_last_modified_cmb_ret = CMessageBoxW( g_hWnd_main, prompt_message, PROGRAM_CAPTION, CMB_ICONWARNING | CMB_CONTINUERESTARTSKIPALL );
				}
			}

			// Restart the download.
			if ( restart_only == 1 || g_last_modified_cmb_ret == CMBIDRESTART || g_last_modified_cmb_ret == CMBIDRESTARTALL )
			{
				EnterCriticalSection( &di->shared_cs );

				di->status = STATUS_STOPPED | STATUS_RESTART;

				LeaveCriticalSection( &di->shared_cs );

				EnterCriticalSection( &context->context_cs );

				context->status = STATUS_STOPPED | STATUS_RESTART;

				if ( context->cleanup == 0 )
				{
					context->cleanup = 1;	// Auto cleanup.

					InterlockedIncrement( &context->pending_operations );

					context->overlapped_close.current_operation = ( context->ssl != NULL ? IO_Shutdown : IO_Close );

					PostQueuedCompletionStatus( g_hIOCP, 0, ( ULONG_PTR )context, ( OVERLAPPED * )&context->overlapped_close );
				}

				LeaveCriticalSection( &context->context_cs );
			}
			else if ( g_last_modified_cmb_ret == CMBIDFAIL || g_last_modified_cmb_ret == CMBIDSKIP || g_last_modified_cmb_ret == CMBIDSKIPALL ) // Skip the download if the return value fails, or the user selected skip.
			{
				EnterCriticalSection( &context->context_cs );

				context->status = STATUS_SKIPPED;

				if ( context->cleanup == 0 )
				{
					context->cleanup = 1;	// Auto cleanup.

					InterlockedIncrement( &context->pending_operations );

					context->overlapped_close.current_operation = ( context->ssl != NULL ? IO_Shutdown : IO_Close );

					PostQueuedCompletionStatus( g_hIOCP, 0, ( ULONG_PTR )context, ( OVERLAPPED * )&context->overlapped_close );
				}

				LeaveCriticalSection( &context->context_cs );
			}
			else	// Continue where we left off when getting the content.
			{
				EnterCriticalSection( &di->shared_cs );

				di->last_modified.HighPart = context->header_info.last_modified.dwHighDateTime;
				di->last_modified.LowPart = context->header_info.last_modified.dwLowDateTime;

				LeaveCriticalSection( &di->shared_cs );

				EnterCriticalSection( &context->context_cs );

				InterlockedIncrement( &context->pending_operations );

				context->overlapped.current_operation = IO_ResumeGetContent;

				PostQueuedCompletionStatus( g_hIOCP, context->current_bytes_read, ( ULONG_PTR )context, ( OVERLAPPED * )&context->overlapped );

				LeaveCriticalSection( &context->context_cs );
			}
		}

		EnterCriticalSection( &last_modified_prompt_list_cs );

		if ( last_modified_prompt_list == NULL )
		{
			skip_processing = true;

			last_modified_prompt_active = false;
		}

		LeaveCriticalSection( &last_modified_prompt_list_cs );
	}
	while ( !skip_processing );

	_ExitThread( 0 );
	return 0;
}

DWORD WINAPI IOCPConnection( LPVOID WorkThreadContext )
{
	HANDLE hIOCP = ( HANDLE )WorkThreadContext;
	OVERLAPPEDEX *overlapped = NULL;
	DWORD io_size = 0;
	SOCKET_CONTEXT *context = NULL;
	IO_OPERATION *current_operation = NULL;
	IO_OPERATION *next_operation = NULL;

	BOOL completion_status = TRUE;

	bool use_ssl = false;

	SECURITY_STATUS scRet = SEC_E_INTERNAL_ERROR;
	bool sent = false;
	int nRet = 0;
	DWORD dwFlags = 0;

	while ( true )
	{
		completion_status = GetQueuedCompletionStatus( hIOCP, &io_size, ( ULONG_PTR * )&context, ( OVERLAPPED ** )&overlapped, INFINITE );

		if ( g_end_program )
		{
			break;
		}

		if ( overlapped != NULL && overlapped->context != NULL )
		{
			context = overlapped->context;

			current_operation = &overlapped->current_operation;
			next_operation = &overlapped->next_operation;
		}
		else
		{
			continue;
		}

		InterlockedExchange( &context->timeout, 0 );	// Reset timeout counter.

		InterlockedDecrement( &context->pending_operations );

		use_ssl = ( context->ssl != NULL ? true : false );

		if ( completion_status == FALSE )
		{
			EnterCriticalSection( &context->context_cs );

			context->cleanup = 1;	// Auto cleanup.

			LeaveCriticalSection( &context->context_cs );

			if ( context->pending_operations > 0 )
			{
				continue;
			}
			else// if ( *current_operation != IO_Shutdown && *current_operation != IO_Close )
			{
				if ( *current_operation == IO_Connect )	// We couldn't establish a connection.
				{
					bool skip_process = false;

					EnterCriticalSection( &context->context_cs );

					if ( IS_STATUS_NOT( context->status,
							STATUS_STOPPED |
							STATUS_REMOVE |
							STATUS_RESTART |
							STATUS_UPDATING ) )	// Stop, Stop and Remove, Restart, or Updating.
					{
						context->timed_out = TIME_OUT_RETRY;

						if ( IS_STATUS( context->status, STATUS_PAUSED ) )
						{
							context->is_paused = true;	// Tells us how to stop the download if it's pausing/paused.

							skip_process = true;
						}
					}

					LeaveCriticalSection( &context->context_cs );

					if ( skip_process )
					{
						continue;
					}
				}

				*current_operation = IO_Close;//( use_ssl ? IO_Shutdown : IO_Close );
			}
		}
		else
		{
			if ( *current_operation == IO_GetContent ||
				 *current_operation == IO_GetCONNECTResponse ||
				 *current_operation == IO_SOCKSResponse )
			{
				// If there's no more data that was read.
				// Can occur when no file size has been set and the connection header is set to close.
				if ( io_size == 0 )
				{
					if ( *current_operation != IO_GetContent )
					{
						// We don't need to shutdown the SSL/TLS connection since it will not have been established yet.
						*current_operation = IO_Close;
					}
					else
					{
						*current_operation = ( use_ssl ? IO_Shutdown : IO_Close );
					}
				}
				else
				{
					bool skip_process = false;

					EnterCriticalSection( &context->context_cs );

					if ( IS_STATUS( context->status,
							STATUS_STOPPED |
							STATUS_REMOVE |
							STATUS_RESTART |
							STATUS_UPDATING ) )	// Stop, Stop and Remove, Restart, or Updating.
					{
						if ( *current_operation != IO_GetContent )
						{
							// We don't need to shutdown the SSL/TLS connection since it will not have been established yet.
							*current_operation = IO_Close;
						}
						else
						{
							*current_operation = ( use_ssl ? IO_Shutdown : IO_Close );
						}
					}
					else if ( IS_STATUS( context->status, STATUS_PAUSED ) )	// Pause.
					{
						context->current_bytes_read = io_size;

						context->is_paused = true;	// Tells us how to stop the download if it's pausing/paused.

						skip_process = true;
					}
					else if ( ( cfg_download_speed_limit > 0 &&
							  ( g_session_total_downloaded - g_session_last_total_downloaded ) > cfg_download_speed_limit ) ||
							  ( context->download_info != NULL &&
								context->download_info->download_speed_limit > 0 &&
							  ( context->download_info->downloaded - context->download_info->last_downloaded ) > context->download_info->download_speed_limit ) ) // Preempt the next receive.
					{
						Sleep( 1 );	// Prevents high CPU usage for some reason.

						context->current_bytes_read = io_size;

						InterlockedIncrement( &context->pending_operations );

						PostQueuedCompletionStatus( hIOCP, context->current_bytes_read, ( ULONG_PTR )context, ( WSAOVERLAPPED * )overlapped );

						skip_process = true;
					}

					LeaveCriticalSection( &context->context_cs );

					if ( skip_process )
					{
						continue;
					}
				}
			}
		}

		switch ( *current_operation )
		{
			case IO_Accept:
			{
				bool free_context = false;

				EnterCriticalSection( &context->context_cs );

				if ( context->cleanup == 0 )
				{
					SOCKET_CONTEXT *new_context = NULL;

					SOCKET_CONTEXT *ftp_control_context = context->ftp_context;
					bool is_ftp_data_connection;
					SOCKET listen_socket;

					if ( ftp_control_context != NULL )
					{
						EnterCriticalSection( &ftp_control_context->context_cs );

						// The Listen context points to the Control context and its download info and vice versa.
						// Set the pointers to NULL so that when it's freed it doesn't have access to the Control context anymore.
						context->ftp_context = NULL;
						context->download_info = NULL;

						// Make sure the Control context no longer has access to the Listen context.
						ftp_control_context->ftp_context = NULL;

						is_ftp_data_connection = true;

						listen_socket = ftp_control_context->listen_socket;

						//////////////////

						// Allow the accept socket to inherit the properties of the listen socket.
						if ( _setsockopt( context->socket, SOL_SOCKET, SO_UPDATE_ACCEPT_CONTEXT, ( char * )&listen_socket, sizeof( SOCKET ) ) != SOCKET_ERROR )
						{
							// Create a new socket context with the inherited socket.
							new_context = UpdateCompletionPort( context->socket,
															  ( ftp_control_context->request_info.protocol == PROTOCOL_FTPS || ftp_control_context->request_info.protocol == PROTOCOL_FTPES ? true : false ),
															  ( ftp_control_context->download_info != NULL ? ftp_control_context->download_info->ssl_version : 0 ),
																true,
																false );

							// The Data context's socket has inherited the properties (and handle) of the Listen context's socket.
							// Invalidate the Listen context's socket so it doesn't shutdown/close the Data context's socket.
							context->socket = INVALID_SOCKET;

							SetDataContextValues( ftp_control_context, new_context );
						}
						else
						{
							InterlockedIncrement( &ftp_control_context->pending_operations );

							ftp_control_context->overlapped.current_operation = IO_Close;

							PostQueuedCompletionStatus( hIOCP, 0, ( ULONG_PTR )ftp_control_context, ( WSAOVERLAPPED * )&ftp_control_context->overlapped );
						}

						LeaveCriticalSection( &ftp_control_context->context_cs );
					}
					else
					{
						is_ftp_data_connection = false;

						listen_socket = g_listen_socket;

						//////////////////

						// Allow the accept socket to inherit the properties of the listen socket.
						if ( _setsockopt( context->socket, SOL_SOCKET, SO_UPDATE_ACCEPT_CONTEXT, ( char * )&listen_socket, sizeof( SOCKET ) ) != SOCKET_ERROR )
						{
							// Create a new socket context with the inherited socket.
							new_context = UpdateCompletionPort( context->socket, false, 0, true, true );

							// The Data context's socket has inherited the properties (and handle) of the Listen context's socket.
							// Invalidate the Listen context's socket so it doesn't shutdown/close the Data context's socket.
							context->socket = INVALID_SOCKET;

							// Post another outstanding AcceptEx for our web server to listen on.
							CreateAcceptSocket( g_listen_socket, g_server_use_ipv6 );
						}
					}

					if ( new_context != NULL )
					{
						EnterCriticalSection( &cleanup_cs );

						EnableTimers( true );

						LeaveCriticalSection( &cleanup_cs );

						EnterCriticalSection( &new_context->context_cs );

						if ( new_context->cleanup == 0 )
						{
							InterlockedIncrement( &new_context->pending_operations );

							if ( is_ftp_data_connection )
							{
								if ( new_context->request_info.protocol == PROTOCOL_FTPS )	// Encrypted Data connections will always be FTPS (implicit).
								{
									new_context->overlapped.next_operation = IO_ClientHandshakeResponse;

									SSL_WSAConnect( new_context, &new_context->overlapped, new_context->request_info.host, sent );
								}
								else	// Non-encrypted connections.
								{
									sent = true;

									new_context->overlapped.current_operation = IO_GetContent;

									new_context->wsabuf.buf = new_context->buffer;
									new_context->wsabuf.len = new_context->buffer_size;

									nRet = _WSARecv( new_context->socket, &new_context->wsabuf, 1, NULL, &dwFlags, ( WSAOVERLAPPED * )&new_context->overlapped, NULL );
									if ( nRet == SOCKET_ERROR && ( _WSAGetLastError() != ERROR_IO_PENDING ) )
									{
										sent = false;
									}
								}

								free_context = true;	// The listen context can be freed.
							}
							else	// A connection has been made to our web server.
							{
								if ( cfg_server_enable_ssl )	// Accept incoming SSL/TLS connections.
								{
									new_context->overlapped.current_operation = IO_ServerHandshakeReply;

									SSL_WSAAccept( new_context, &new_context->overlapped, sent );
								}
								else	// Non-encrypted connections.
								{
									sent = true;

									new_context->overlapped.current_operation = IO_GetRequest;

									new_context->wsabuf.buf = new_context->buffer;
									new_context->wsabuf.len = new_context->buffer_size;

									nRet = _WSARecv( new_context->socket, &new_context->wsabuf, 1, NULL, &dwFlags, ( WSAOVERLAPPED * )&new_context->overlapped, NULL );
									if ( nRet == SOCKET_ERROR && ( _WSAGetLastError() != ERROR_IO_PENDING ) )
									{
										sent = false;
									}
								}
							}

							if ( !sent )
							{
								new_context->overlapped.current_operation = IO_Close;

								PostQueuedCompletionStatus( hIOCP, 0, ( ULONG_PTR )new_context, ( WSAOVERLAPPED * )&new_context->overlapped );
							}
						}
						else if ( new_context->cleanup == 2 )	// If we've forced the cleanup, then allow it to continue its steps.
						{
							new_context->cleanup = 1;	// Auto cleanup.
						}
						else	// We've already shutdown and/or closed the connection.
						{
							InterlockedIncrement( &new_context->pending_operations );

							new_context->overlapped.current_operation = IO_Close;

							PostQueuedCompletionStatus( hIOCP, 0, ( ULONG_PTR )new_context, ( WSAOVERLAPPED * )&new_context->overlapped );
						}

						LeaveCriticalSection( &new_context->context_cs );
					}
					else	// Clean up the listen context.
					{
						free_context = true;
					}
				}
				else if ( context->cleanup == 2 )	// If we've forced the cleanup, then allow it to continue its steps.
				{
					context->cleanup = 1;	// Auto cleanup.
				}
				else	// We've already shutdown and/or closed the connection.
				{
					InterlockedIncrement( &context->pending_operations );

					*current_operation = IO_Close;

					PostQueuedCompletionStatus( hIOCP, 0, ( ULONG_PTR )context, ( WSAOVERLAPPED * )overlapped );
				}

				LeaveCriticalSection( &context->context_cs );

				if ( free_context )
				{
					CleanupConnection( context );
				}
			}
			break;

			case IO_Connect:
			{
				bool connection_failed = false;

				EnterCriticalSection( &context->context_cs );

				if ( context->cleanup == 0 )
				{
					// Allow the connect socket to inherit the properties of the previously set properties.
					// Must be done so that shutdown() will work.
					nRet = _setsockopt( context->socket, SOL_SOCKET, SO_UPDATE_CONNECT_CONTEXT, NULL, 0 );
					if ( nRet != SOCKET_ERROR )
					{
						if ( context->request_info.protocol == PROTOCOL_HTTPS ||
							 context->request_info.protocol == PROTOCOL_FTPS )	// FTPES starts out unencrypted and is upgraded later.
						{
							char shared_protocol = ( context->download_info != NULL ? context->download_info->ssl_version : 0 );
							DWORD protocol = 0;
							switch ( shared_protocol )
							{
								case 4:	protocol |= SP_PROT_TLS1_2_CLIENT;
								case 3:	protocol |= SP_PROT_TLS1_1_CLIENT;
								case 2:	protocol |= SP_PROT_TLS1_CLIENT;
								case 1:	protocol |= SP_PROT_SSL3_CLIENT;
								case 0:	{ if ( shared_protocol < 2 ) { protocol |= SP_PROT_SSL2_CLIENT; } }
							}

							SSL *ssl = SSL_new( protocol, false );
							if ( ssl == NULL )
							{
								connection_failed = true;
							}
							else
							{
								ssl->s = context->socket;

								context->ssl = ssl;
							}
						}

						if ( context->download_info != NULL )
						{
							EnterCriticalSection( &context->download_info->shared_cs );

							if ( !connection_failed )
							{
								context->download_info->status = STATUS_DOWNLOADING;

								if ( IS_STATUS( context->status, STATUS_PAUSED ) )
								{
									context->download_info->status |= STATUS_PAUSED;

									context->is_paused = false;	// Set to true when last IO operation has completed.
								}

								context->status = context->download_info->status;
							}
							else
							{
								context->status = STATUS_FAILED;
							}

							LeaveCriticalSection( &context->download_info->shared_cs );
						}

						if ( !connection_failed )
						{
							InterlockedIncrement( &context->pending_operations );

							// If it's an HTTPS or FTPS (not FTPES) request and we're not going through a SSL/TLS proxy, then begin the SSL/TLS handshake.
							if ( ( context->request_info.protocol == PROTOCOL_HTTPS ||
								   context->request_info.protocol == PROTOCOL_FTPS ) &&
								   !cfg_enable_proxy_s && !cfg_enable_proxy_socks )
							{
								*next_operation = IO_ClientHandshakeResponse;

								SSL_WSAConnect( context, overlapped, context->request_info.host, sent );
								if ( !sent )
								{
									InterlockedDecrement( &context->pending_operations );

									connection_failed = true;
								}
							}
							else if ( context->request_info.protocol == PROTOCOL_FTP ||
									  context->request_info.protocol == PROTOCOL_FTPES )	// FTPES starts out unencrypted and is upgraded later.
							{
								context->wsabuf.buf = context->buffer;
								context->wsabuf.len = context->buffer_size;

								if ( cfg_enable_proxy_socks )	// SOCKS5 request.
								{
									*current_operation = IO_Write;
									*next_operation = IO_SOCKSResponse;

									context->content_status = SOCKS_STATUS_REQUEST_AUTH;

									ConstructSOCKSRequest( context, 0 );

									nRet = _WSASend( context->socket, &context->wsabuf, 1, NULL, dwFlags, ( WSAOVERLAPPED * )overlapped, NULL );
									if ( nRet == SOCKET_ERROR && ( _WSAGetLastError() != ERROR_IO_PENDING ) )
									{
										InterlockedDecrement( &context->pending_operations );

										connection_failed = true;
									}
								}
								else
								{
									*current_operation = IO_GetContent;

									nRet = _WSARecv( context->socket, &context->wsabuf, 1, NULL, &dwFlags, ( WSAOVERLAPPED * )overlapped, NULL );
									if ( nRet == SOCKET_ERROR && ( _WSAGetLastError() != ERROR_IO_PENDING ) )
									{
										InterlockedDecrement( &context->pending_operations );

										connection_failed = true;
									}
								}
							}
							else	// HTTP and tunneled HTTPS requests send/recv data normally.
							{
								*current_operation = IO_Write;

								context->wsabuf.buf = context->buffer;
								context->wsabuf.len = context->buffer_size;

								// Tunneled HTTPS requests need to send a CONNECT response before sending/receiving data.
								if ( context->request_info.protocol == PROTOCOL_HTTPS && cfg_enable_proxy_s )
								{
									*next_operation = IO_GetCONNECTResponse;

									ConstructRequest( context, true );
								}
								else if ( cfg_enable_proxy_socks )	// SOCKS5 request.
								{
									*next_operation = IO_SOCKSResponse;

									context->content_status = SOCKS_STATUS_REQUEST_AUTH;

									ConstructSOCKSRequest( context, 0 );
								}
								else	// HTTP request.
								{
									*next_operation = IO_GetContent;

									ConstructRequest( context, false );
								}

								nRet = _WSASend( context->socket, &context->wsabuf, 1, NULL, dwFlags, ( WSAOVERLAPPED * )overlapped, NULL );
								if ( nRet == SOCKET_ERROR && ( _WSAGetLastError() != ERROR_IO_PENDING ) )
								{
									InterlockedDecrement( &context->pending_operations );

									connection_failed = true;
								}
							}
						}
					}
					else
					{
						connection_failed = true;
					}
				}
				else if ( context->cleanup == 2 )	// If we've forced the cleanup, then allow it to continue its steps.
				{
					context->cleanup = 1;	// Auto cleanup.
				}
				else	// We've already shutdown and/or closed the connection.
				{
					connection_failed = true;
				}

				if ( connection_failed )
				{
					InterlockedIncrement( &context->pending_operations );

					*current_operation = IO_Close;

					PostQueuedCompletionStatus( hIOCP, 0, ( ULONG_PTR )context, ( WSAOVERLAPPED * )overlapped );
				}

				LeaveCriticalSection( &context->context_cs );
			}
			break;

			case IO_ClientHandshakeResponse:
			case IO_ClientHandshakeReply:
			{
				EnterCriticalSection( &context->context_cs );

				if ( context->cleanup == 0 )
				{
					context->wsabuf.buf = context->buffer;
					context->wsabuf.len = context->buffer_size;

					InterlockedIncrement( &context->pending_operations );

					if ( *current_operation == IO_ClientHandshakeReply )
					{
						context->ssl->cbIoBuffer += io_size;

						if ( context->ssl->cbIoBuffer > 0 )
						{
							*current_operation = IO_ClientHandshakeResponse;
							*next_operation = IO_ClientHandshakeResponse;

							scRet = SSL_WSAConnect_Reply( context, overlapped, sent );
						}
						else
						{
							sent = false;
							scRet = SEC_E_INTERNAL_ERROR;
						}
					}
					else
					{
						*current_operation = IO_ClientHandshakeReply;

						scRet = SSL_WSAConnect_Response( context, overlapped, sent );
					}

					if ( !sent )
					{
						InterlockedDecrement( &context->pending_operations );
					}

					if ( scRet == SEC_E_OK )
					{
						// Post request.

						context->wsabuf.buf = context->buffer;
						context->wsabuf.len = context->buffer_size;

						*next_operation = IO_GetContent;

						if ( context->request_info.protocol == PROTOCOL_FTPS ||
							 context->request_info.protocol == PROTOCOL_FTPES )
						{
							*current_operation = IO_GetContent;

							if ( context->request_info.protocol == PROTOCOL_FTPES )
							{
								if ( MakeFTPResponse( context ) == FTP_CONTENT_STATUS_FAILED )
								{
									InterlockedIncrement( &context->pending_operations );

									*current_operation = IO_Shutdown;

									PostQueuedCompletionStatus( hIOCP, 0, ( ULONG_PTR )context, ( WSAOVERLAPPED * )overlapped );
								}
							}
							else
							{
								InterlockedIncrement( &context->pending_operations );

								SSL_WSARecv( context, overlapped, sent );
								if ( !sent )
								{
									*current_operation = IO_Shutdown;

									PostQueuedCompletionStatus( hIOCP, 0, ( ULONG_PTR )context, ( WSAOVERLAPPED * )overlapped );
								}
							}
						}
						else	// HTTP
						{
							InterlockedIncrement( &context->pending_operations );

							ConstructRequest( context, false );

							SSL_WSASend( context, overlapped, &context->wsabuf, sent );
							if ( !sent )
							{
								*current_operation = IO_Shutdown;

								PostQueuedCompletionStatus( hIOCP, 0, ( ULONG_PTR )context, ( WSAOVERLAPPED * )overlapped );
							}
						}
					}
					else if ( scRet != SEC_I_CONTINUE_NEEDED && scRet != SEC_E_INCOMPLETE_MESSAGE && scRet != SEC_I_INCOMPLETE_CREDENTIALS )
					{
						// Have seen SEC_E_ILLEGAL_MESSAGE (for a bad target name in InitializeSecurityContext), SEC_E_BUFFER_TOO_SMALL, and SEC_E_MESSAGE_ALTERED.

						InterlockedIncrement( &context->pending_operations );

						*current_operation = IO_Close;

						PostQueuedCompletionStatus( hIOCP, 0, ( ULONG_PTR )context, ( WSAOVERLAPPED * )overlapped );
					}
				}
				else if ( context->cleanup == 2 )	// If we've forced the cleanup, then allow it to continue its steps.
				{
					context->cleanup = 1;	// Auto cleanup.
				}
				else	// We've already shutdown and/or closed the connection.
				{
					InterlockedIncrement( &context->pending_operations );

					*current_operation = IO_Close;

					PostQueuedCompletionStatus( hIOCP, 0, ( ULONG_PTR )context, ( WSAOVERLAPPED * )overlapped );
				}

				LeaveCriticalSection( &context->context_cs );
			}
			break;

			case IO_ServerHandshakeResponse:
			case IO_ServerHandshakeReply:
			{
				EnterCriticalSection( &context->context_cs );

				if ( context->cleanup == 0 )
				{
					// We process data from the client and write our reply.
					InterlockedIncrement( &context->pending_operations );

					if ( *current_operation == IO_ServerHandshakeReply )
					{
						context->ssl->cbIoBuffer += io_size;

						*current_operation = IO_ServerHandshakeResponse;
						*next_operation = IO_ServerHandshakeResponse;

						scRet = SSL_WSAAccept_Reply( context, overlapped, sent );
					}
					else
					{
						*current_operation = IO_ServerHandshakeReply;

						scRet = SSL_WSAAccept_Response( context, overlapped, sent );
					}

					if ( !sent )
					{
						InterlockedDecrement( &context->pending_operations );
					}

					if ( scRet == SEC_E_OK )	// If true, then no send was made.
					{
						InterlockedIncrement( &context->pending_operations );

						*current_operation = IO_GetRequest;

						if ( context->ssl->cbIoBuffer > 0 )
						{
							// The request was sent with the handshake.
							PostQueuedCompletionStatus( hIOCP, context->ssl->cbIoBuffer, ( ULONG_PTR )context, ( WSAOVERLAPPED * )overlapped );
						}
						else
						{
							context->wsabuf.buf = context->buffer;
							context->wsabuf.len = context->buffer_size;

							/*scRet =*/ SSL_WSARecv( context, overlapped, sent );
							if ( /*scRet != SEC_E_OK ||*/ !sent )
							{
								*current_operation = IO_Shutdown;

								PostQueuedCompletionStatus( hIOCP, 0, ( ULONG_PTR )context, ( WSAOVERLAPPED * )overlapped );
							}
						}
					}
					else if ( scRet == SEC_E_INCOMPLETE_MESSAGE && *current_operation == IO_ServerHandshakeResponse )
					{
						// An SEC_E_INCOMPLETE_MESSAGE after SSL_WSAAccept_Reply can indicate that it doesn't support SSL/TLS, but sent the request as plaintext.

						/*InterlockedIncrement( &context->pending_operations );

						context->wsabuf.buf = context->buffer;
						context->wsabuf.len = context->buffer_size;

						DWORD bytes_read = min( context->buffer_size, context->ssl->cbIoBuffer );

						_memcpy_s( context->wsabuf.buf, context->buffer_size, context->ssl->pbIoBuffer, bytes_read );
						*current_operation = IO_GetRequest;

						SSL_free( context->ssl );
						context->ssl = NULL;

						PostQueuedCompletionStatus( hIOCP, bytes_read, ( ULONG_PTR )context, ( WSAOVERLAPPED * )overlapped );*/

						*current_operation = IO_Write;
						*next_operation = IO_Close;	// This is closed because the SSL connection was never established. An SSL shutdown would just fail.

						InterlockedIncrement( &context->pending_operations );

						context->wsabuf.buf = context->buffer;

						context->wsabuf.len = __snprintf( context->wsabuf.buf, context->buffer_size,
							"HTTP/1.1 301 Moved Permanently\r\n" \
							"Location: https://%s:%hu/\r\n"
							"Content-Type: text/html\r\n" \
							"Content-Length: 120\r\n" \
							"Connection: close\r\n\r\n" \
							"<!DOCTYPE html><html><head><title>301 Moved Permanently</title></head><body><h1>301 Moved Permanently</h1></body></html>", g_server_domain, g_server_port );

						// We do a regular WSASend here since the connection was not encrypted.
						nRet = _WSASend( context->socket, &context->wsabuf, 1, NULL, dwFlags, ( WSAOVERLAPPED * )overlapped, NULL );
						if ( nRet == SOCKET_ERROR && ( _WSAGetLastError() != ERROR_IO_PENDING ) )
						{
							*current_operation = IO_Close;

							PostQueuedCompletionStatus( hIOCP, 0, ( ULONG_PTR )context, ( WSAOVERLAPPED * )overlapped );
						}
					}
					else if ( scRet != SEC_I_CONTINUE_NEEDED && scRet != SEC_E_INCOMPLETE_MESSAGE && scRet != SEC_I_INCOMPLETE_CREDENTIALS )	// Stop handshake and close the connection.
					{
						InterlockedIncrement( &context->pending_operations );

						*current_operation = IO_Close;

						PostQueuedCompletionStatus( hIOCP, 0, ( ULONG_PTR )context, ( WSAOVERLAPPED * )overlapped );
					}
				}
				else if ( context->cleanup == 2 )	// If we've forced the cleanup, then allow it to continue its steps.
				{
					context->cleanup = 1;	// Auto cleanup.
				}
				else	// We've already shutdown and/or closed the connection.
				{
					InterlockedIncrement( &context->pending_operations );

					*current_operation = IO_Close;

					PostQueuedCompletionStatus( hIOCP, 0, ( ULONG_PTR )context, ( WSAOVERLAPPED * )overlapped );
				}

				LeaveCriticalSection( &context->context_cs );
			}
			break;

			case IO_GetCONNECTResponse:
			{
				EnterCriticalSection( &context->context_cs );

				if ( context->cleanup == 0 )
				{
					context->current_bytes_read = io_size + ( DWORD )( context->wsabuf.buf - context->buffer );

					context->wsabuf.buf = context->buffer;
					context->wsabuf.len = context->buffer_size;

					context->wsabuf.buf[ context->current_bytes_read ] = 0;	// Sanity.

					char content_status = ParseHTTPHeader( context, context->wsabuf.buf, context->current_bytes_read );

					if ( content_status == CONTENT_STATUS_READ_MORE_HEADER )	// Request more header data.
					{
						InterlockedIncrement( &context->pending_operations );

						// wsabuf will be offset in ParseHTTPHeader to handle more data.
						nRet = _WSARecv( context->socket, &context->wsabuf, 1, NULL, &dwFlags, ( WSAOVERLAPPED * )overlapped, NULL );
						if ( nRet == SOCKET_ERROR && ( _WSAGetLastError() != ERROR_IO_PENDING ) )
						{
							*current_operation = IO_Close;

							PostQueuedCompletionStatus( hIOCP, 0, ( ULONG_PTR )context, ( WSAOVERLAPPED * )overlapped );
						}
					}
					else if ( content_status == CONTENT_STATUS_FAILED )
					{
						context->status = STATUS_FAILED;

						InterlockedIncrement( &context->pending_operations );

						// We don't need to shutdown the SSL/TLS connection since it will not have been established yet.
						*current_operation = IO_Close;

						PostQueuedCompletionStatus( hIOCP, 0, ( ULONG_PTR )context, ( WSAOVERLAPPED * )overlapped );
					}
					else// if ( content_status == CONTENT_STATUS_GET_CONTENT );
					{
						// Any 2XX status is valid.
						if ( context->header_info.http_status >= 200 && context->header_info.http_status <= 299 )
						{
							context->got_filename = 0;
							context->got_last_modified = 0;
							context->show_file_size_prompt = false;

							context->header_info.chunk_length = 0;
							context->header_info.end_of_header = NULL;
							context->header_info.http_status = 0;
							context->header_info.connection = CONNECTION_NONE;
							context->header_info.content_encoding = CONTENT_ENCODING_NONE;
							context->header_info.chunked_transfer = false;
							//context->header_info.etag = false;
							context->header_info.got_chunk_start = false;
							context->header_info.got_chunk_terminator = false;

							context->header_info.range_info->content_length = 0;	// We must reset this to get the real request length (not the length of the 2XX request).

							// Do not reset the other range_info values.

							//

							context->content_status = CONTENT_STATUS_NONE;

							InterlockedIncrement( &context->pending_operations );

							*next_operation = IO_ClientHandshakeResponse;

							SSL_WSAConnect( context, overlapped, context->request_info.host, sent );
							if ( !sent )
							{
								*current_operation = IO_Shutdown;

								PostQueuedCompletionStatus( hIOCP, 0, ( ULONG_PTR )context, ( WSAOVERLAPPED * )overlapped );
							}
						}
						else	// Proxy can't/won't tunnel SSL/TLS connections, or authentication is required.
						{
							bool skip_close = false;

							// Authentication is required.
							if ( context->header_info.http_status == 407 )
							{
								if ( context->header_info.proxy_digest_info != NULL &&
								   ( context->header_info.proxy_digest_info->auth_type == AUTH_TYPE_BASIC || context->header_info.proxy_digest_info->auth_type == AUTH_TYPE_DIGEST ) &&
									 context->header_info.proxy_digest_info->nc == 0 )
								{
									bool use_keep_alive_connection = false;

									// If we have a keep-alive connection and were sent all of the data,
									// then we can reuse the connection and not have to flush any remnant data from the buffer.
									if ( context->header_info.connection == CONNECTION_KEEP_ALIVE )
									{
										char *response_buffer = context->header_info.end_of_header;
										int response_buffer_length = context->current_bytes_read - ( DWORD )( context->header_info.end_of_header - context->wsabuf.buf );

										// Look for a chunk terminator.
										if ( context->header_info.chunked_transfer )
										{
											if ( ( response_buffer_length >= 5 ) && ( _memcmp( response_buffer + ( response_buffer_length - 5 ), "0\r\n\r\n", 5 ) != 0 ) )
											{
												use_keep_alive_connection = true;
											}
										}
										else if ( response_buffer_length >= context->header_info.range_info->content_length )	// See if the response data length is the same as the content length.
										{
											use_keep_alive_connection = true;
										}
									}

									context->header_info.connection = ( use_keep_alive_connection ? CONNECTION_KEEP_ALIVE : CONNECTION_CLOSE );

									if ( MakeRequest( context, IO_GetCONNECTResponse, true ) == CONTENT_STATUS_FAILED )
									{
										context->status = STATUS_FAILED;
									}
									else	// Request was sent, don't close the connection below.
									{
										skip_close = true;
									}
								}
								else	// Exhausted the nonce count.
								{
									context->status = STATUS_PROXY_AUTH_REQUIRED;
								}
							}
							else	// Unhandled status response.
							{
								context->status = STATUS_FAILED;
							}

							if ( !skip_close )
							{
								InterlockedIncrement( &context->pending_operations );

								// We don't need to shutdown the SSL/TLS connection since it will not have been established yet.
								*current_operation = IO_Close;

								PostQueuedCompletionStatus( hIOCP, 0, ( ULONG_PTR )context, ( WSAOVERLAPPED * )overlapped );
							}
						}
					}
				}
				else if ( context->cleanup == 2 )	// If we've forced the cleanup, then allow it to continue its steps.
				{
					context->cleanup = 1;	// Auto cleanup.
				}
				else	// We've already shutdown and/or closed the connection.
				{
					InterlockedIncrement( &context->pending_operations );

					*current_operation = IO_Close;

					PostQueuedCompletionStatus( hIOCP, 0, ( ULONG_PTR )context, ( WSAOVERLAPPED * )overlapped );
				}

				LeaveCriticalSection( &context->context_cs );
			}
			break;

			case IO_SOCKSResponse:
			{
				char connection_status = 0;	// 0 = continue, 1 = fail, 2 = exit

				EnterCriticalSection( &context->context_cs );

				if ( context->cleanup == 0 )
				{
					InterlockedIncrement( &context->pending_operations );

					context->current_bytes_read = io_size + ( DWORD )( context->wsabuf.buf - context->buffer );

					context->wsabuf.buf = context->buffer;
					context->wsabuf.len = context->buffer_size;

					context->wsabuf.buf[ context->current_bytes_read ] = 0;	// Sanity.

					if ( context->content_status == SOCKS_STATUS_REQUEST_AUTH )
					{
						if ( context->current_bytes_read < 2 )	// Request more data.
						{
							context->wsabuf.buf += context->current_bytes_read;
							context->wsabuf.len -= context->current_bytes_read;

							// wsabuf will be offset in ParseHTTPHeader to handle more data.
							nRet = _WSARecv( context->socket, &context->wsabuf, 1, NULL, &dwFlags, ( WSAOVERLAPPED * )overlapped, NULL );
							if ( nRet == SOCKET_ERROR && ( _WSAGetLastError() != ERROR_IO_PENDING ) )
							{
								connection_status = 1;	// Failed.
							}
						}
						else
						{
							if ( context->wsabuf.buf[ 1 ] == 0x5A )	// SOCKS4 - request granted.
							{
								context->content_status = SOCKS_STATUS_HANDLE_CONNECTION;
							}
							else if ( context->wsabuf.buf[ 1 ] == 0x00 )	// SOCKS5 - no authentication required.
							{
								context->content_status = SOCKS_STATUS_REQUEST_CONNECTION;
							}
							else if ( context->wsabuf.buf[ 1 ] == 0x02 )	// SOCKS5 - username and password authentication required.
							{
								if ( cfg_use_authentication_socks )
								{
									context->content_status = SOCKS_STATUS_AUTH_SENT;

									ConstructSOCKSRequest( context, 2 );

									*current_operation = IO_Write;

									nRet = _WSASend( context->socket, &context->wsabuf, 1, NULL, dwFlags, ( WSAOVERLAPPED * )overlapped, NULL );
									if ( nRet == SOCKET_ERROR && ( _WSAGetLastError() != ERROR_IO_PENDING ) )
									{
										connection_status = 1;	// Failed.
									}
									else
									{
										connection_status = 2;	// Exit the case.
									}
								}
								else	// Server wants us to send it, but we don't have it enabled.
								{
									context->status = STATUS_PROXY_AUTH_REQUIRED;

									connection_status = 1;	// Failed.
								}
							}
							else	// Bad request, or unsupported authentication method.
							{
								connection_status = 1;	// Failed.
							}
						}
					}
					else if ( context->content_status == SOCKS_STATUS_AUTH_SENT )
					{
						if ( context->wsabuf.buf[ 1 ] == 0x00 )	// Username and password accepted.
						{
							context->content_status = SOCKS_STATUS_REQUEST_CONNECTION;
						}
						else	// We sent the username and password, but it was rejected.
						{
							context->status = STATUS_PROXY_AUTH_REQUIRED;

							connection_status = 1;	// Failed.
						}
					}

					// No problems, continue with our request.
					if ( connection_status == 0 )
					{
						if ( context->content_status == SOCKS_STATUS_REQUEST_CONNECTION )
						{
							*current_operation = IO_Write;

							context->content_status = SOCKS_STATUS_HANDLE_CONNECTION;

							ConstructSOCKSRequest( context, 1 );

							nRet = _WSASend( context->socket, &context->wsabuf, 1, NULL, dwFlags, ( WSAOVERLAPPED * )overlapped, NULL );
							if ( nRet == SOCKET_ERROR && ( _WSAGetLastError() != ERROR_IO_PENDING ) )
							{
								connection_status = 1;	// Failed.
							}
						}
						else if ( context->content_status == SOCKS_STATUS_HANDLE_CONNECTION )
						{
							if ( context->ftp_connection_type == FTP_CONNECTION_TYPE_DATA )
							{
								context->content_status = CONTENT_STATUS_GET_CONTENT;	// This is the data connection and we want to start downloading from it.
							}
							else
							{
								context->content_status = CONTENT_STATUS_NONE;	// Reset.
							}

							if ( context->request_info.protocol == PROTOCOL_HTTPS ||
								 context->request_info.protocol == PROTOCOL_FTPS )	// FTPES starts out unencrypted and is upgraded later.
							{
								*next_operation = IO_ClientHandshakeResponse;

								SSL_WSAConnect( context, overlapped, context->request_info.host, sent );
								if ( !sent )
								{
									connection_status = 1;	// Failed.
								}
							}
							else
							{
								*next_operation = IO_GetContent;

								if ( context->request_info.protocol == PROTOCOL_FTP ||
									 context->request_info.protocol == PROTOCOL_FTPES )	// FTPES starts out unencrypted and is upgraded later.
								{
									*current_operation = IO_GetContent;

									nRet = _WSARecv( context->socket, &context->wsabuf, 1, NULL, &dwFlags, ( WSAOVERLAPPED * )overlapped, NULL );
									if ( nRet == SOCKET_ERROR && ( _WSAGetLastError() != ERROR_IO_PENDING ) )
									{
										*current_operation = IO_Close;

										PostQueuedCompletionStatus( hIOCP, 0, ( ULONG_PTR )context, ( WSAOVERLAPPED * )overlapped );
									}
								}
								else	// HTTP
								{
									*current_operation = IO_Write;

									ConstructRequest( context, false );

									nRet = _WSASend( context->socket, &context->wsabuf, 1, NULL, dwFlags, ( WSAOVERLAPPED * )overlapped, NULL );
									if ( nRet == SOCKET_ERROR && ( _WSAGetLastError() != ERROR_IO_PENDING ) )
									{
										connection_status = 1;
									}
								}
							}
						}
						else
						{
							context->status = STATUS_FAILED;

							connection_status = 1;	// Failed.
						}
					}
				}
				else if ( context->cleanup == 2 )	// If we've forced the cleanup, then allow it to continue its steps.
				{
					context->cleanup = 1;	// Auto cleanup.
				}
				else	// We've already shutdown and/or closed the connection.
				{
					connection_status = 1;	// Failed.

					InterlockedIncrement( &context->pending_operations );
				}

				// If something failed.
				if ( connection_status == 1 )
				{
					*current_operation = IO_Close;

					PostQueuedCompletionStatus( hIOCP, 0, ( ULONG_PTR )context, ( WSAOVERLAPPED * )overlapped );
				}

				LeaveCriticalSection( &context->context_cs );
			}
			break;

			case IO_GetContent:
			case IO_ResumeGetContent:
			case IO_GetRequest:
			{
				EnterCriticalSection( &context->context_cs );

				if ( context->cleanup == 0 )
				{
					char content_status = CONTENT_STATUS_FAILED;

					DWORD bytes_decrypted = io_size;

					//if ( *current_operation == IO_GetContent || *current_operation == IO_GetRequest )
					if ( *current_operation != IO_ResumeGetContent )
					{
						context->current_bytes_read = 0;

						if ( use_ssl )
						{
							// We'll need to decrypt any remaining undecrypted data as well as copy the decrypted data to our wsabuf.
							if ( context->ssl->continue_decrypt )
							{
								bytes_decrypted = context->ssl->cbIoBuffer;
							}

							scRet = DecryptRecv( context, bytes_decrypted );
						}
					}

					if ( bytes_decrypted > 0 )
					{
						//if ( *current_operation == IO_GetContent || *current_operation == IO_GetRequest )
						if ( *current_operation != IO_ResumeGetContent )
						{
							context->current_bytes_read = bytes_decrypted + ( DWORD )( context->wsabuf.buf - context->buffer );

							context->wsabuf.buf = context->buffer;
							context->wsabuf.len = context->buffer_size;

							context->wsabuf.buf[ context->current_bytes_read ] = 0;	// Sanity.
						}
						else
						{
							*current_operation = IO_GetContent;
						}

						if ( *current_operation == IO_GetContent )
						{
							if ( context->request_info.protocol == PROTOCOL_FTP ||
								 context->request_info.protocol == PROTOCOL_FTPS ||
								 context->request_info.protocol == PROTOCOL_FTPES )
							{
								content_status = GetFTPResponseContent( context, context->wsabuf.buf, context->current_bytes_read );
							}
							else
							{
								content_status = GetHTTPResponseContent( context, context->wsabuf.buf, context->current_bytes_read );
							}
						}
						else// if ( *current_operation == IO_GetRequest )
						{
							if ( context->request_info.protocol != PROTOCOL_FTP &&
								 context->request_info.protocol != PROTOCOL_FTPS &&
								 context->request_info.protocol != PROTOCOL_FTPES )
							{
								content_status = GetHTTPRequestContent( context, context->wsabuf.buf, context->current_bytes_read );
							}
						}
					}
					else if ( use_ssl )
					{
						if ( scRet == SEC_E_INCOMPLETE_MESSAGE )
						{
							InterlockedIncrement( &context->pending_operations );

							//context->wsabuf.buf += bytes_decrypted;
							//context->wsabuf.len -= bytes_decrypted;

							SSL_WSARecv( context, overlapped, sent );
							if ( !sent )
							{
								InterlockedDecrement( &context->pending_operations );
							}
							else
							{
								content_status = CONTENT_STATUS_NONE;
							}
						}

						// SEC_I_CONTEXT_EXPIRED may occur here.
					}

					if ( content_status == CONTENT_STATUS_FAILED )
					{
						InterlockedIncrement( &context->pending_operations );

						*current_operation = ( use_ssl ? IO_Shutdown : IO_Close );

						PostQueuedCompletionStatus( hIOCP, 0, ( ULONG_PTR )context, ( WSAOVERLAPPED * )overlapped );
					}
					else if ( content_status == CONTENT_STATUS_HANDLE_RESPONSE )
					{
						context->content_status = CONTENT_STATUS_GET_CONTENT;

						if ( MakeRangeRequest( context ) == CONTENT_STATUS_FAILED )
						{
							InterlockedIncrement( &context->pending_operations );

							*current_operation = ( use_ssl ? IO_Shutdown : IO_Close );

							PostQueuedCompletionStatus( hIOCP, 0, ( ULONG_PTR )context, ( WSAOVERLAPPED * )overlapped );
						}
					}
					else if ( content_status == CONTENT_STATUS_HANDLE_REQUEST )
					{
						context->content_status = CONTENT_STATUS_GET_CONTENT;

						if ( MakeResponse( context ) == CONTENT_STATUS_FAILED )
						{
							InterlockedIncrement( &context->pending_operations );

							*current_operation = ( use_ssl ? IO_Shutdown : IO_Close );

							PostQueuedCompletionStatus( hIOCP, 0, ( ULONG_PTR )context, ( WSAOVERLAPPED * )overlapped );
						}
					}
					else if ( content_status == FTP_CONTENT_STATUS_HANDLE_REQUEST )
					{
						if ( MakeFTPResponse( context ) == FTP_CONTENT_STATUS_FAILED )
						{
							InterlockedIncrement( &context->pending_operations );

							*current_operation = ( use_ssl ? IO_Shutdown : IO_Close );

							PostQueuedCompletionStatus( hIOCP, 0, ( ULONG_PTR )context, ( WSAOVERLAPPED * )overlapped );
						}
					}
					else if ( content_status == CONTENT_STATUS_READ_MORE_CONTENT || content_status == CONTENT_STATUS_READ_MORE_HEADER ) // Read more header information, or continue to read more content. Do not reset context->wsabuf since it may have been offset to handle partial data.
					{
						InterlockedIncrement( &context->pending_operations );

						//*current_operation = IO_GetContent;

						if ( use_ssl )
						{
							if ( context->ssl->continue_decrypt )
							{
								// We need to post a non-zero status to avoid our code shutting down the connection.
								// We'll use context->current_bytes_read for that, but it can be anything that's not zero.
								PostQueuedCompletionStatus( hIOCP, context->current_bytes_read, ( ULONG_PTR )context, ( WSAOVERLAPPED * )overlapped );
							}
							else
							{
								SSL_WSARecv( context, overlapped, sent );
								if ( !sent )
								{
									*current_operation = IO_Shutdown;

									PostQueuedCompletionStatus( hIOCP, 0, ( ULONG_PTR )context, ( WSAOVERLAPPED * )overlapped );
								}
							}
						}
						else
						{
							nRet = _WSARecv( context->socket, &context->wsabuf, 1, NULL, &dwFlags, ( WSAOVERLAPPED * )overlapped, NULL );
							if ( nRet == SOCKET_ERROR && ( _WSAGetLastError() != ERROR_IO_PENDING ) )
							{
								*current_operation = IO_Close;

								PostQueuedCompletionStatus( hIOCP, 0, ( ULONG_PTR )context, ( WSAOVERLAPPED * )overlapped );
							}
						}
					}
				}
				else if ( context->cleanup == 2 )	// If we've forced the cleanup, then allow it to continue its steps.
				{
					context->cleanup = 1;	// Auto cleanup.
				}
				else	// We've already shutdown and/or closed the connection.
				{
					InterlockedIncrement( &context->pending_operations );

					*current_operation = IO_Close;

					PostQueuedCompletionStatus( hIOCP, 0, ( ULONG_PTR )context, ( WSAOVERLAPPED * )overlapped );
				}

				LeaveCriticalSection( &context->context_cs );
			}
			break;

			case IO_WriteFile:
			{
				EnterCriticalSection( &context->context_cs );

				if ( context->cleanup == 0 )
				{
					EnterCriticalSection( &context->download_info->shared_cs );
					context->download_info->downloaded += io_size;				// The total amount of data (decoded) that was saved/simulated.
					LeaveCriticalSection( &context->download_info->shared_cs );

					EnterCriticalSection( &session_totals_cs );
					g_session_total_downloaded += io_size;
					LeaveCriticalSection( &session_totals_cs );

					context->header_info.range_info->file_write_offset += io_size;	// The size of the non-encoded/decoded data that we're writing to the file.

					// Make sure we've written everything before we do anything else.
					if ( io_size < context->write_wsabuf.len )
					{
						EnterCriticalSection( &context->download_info->shared_cs );

						InterlockedIncrement( &context->pending_operations );

						context->write_wsabuf.buf += io_size;
						context->write_wsabuf.len -= io_size;

						BOOL bRet = WriteFile( context->download_info->hFile, context->write_wsabuf.buf, context->write_wsabuf.len, NULL, ( WSAOVERLAPPED * )overlapped );
						if ( bRet == FALSE && ( GetLastError() != ERROR_IO_PENDING ) )
						{
							*current_operation = ( use_ssl ? IO_Shutdown : IO_Close );

							PostQueuedCompletionStatus( hIOCP, 0, ( ULONG_PTR )context, ( WSAOVERLAPPED * )overlapped );
						}

						LeaveCriticalSection( &context->download_info->shared_cs );
					}
					else
					{
						char content_status = context->content_status;

						// Reset so we don't try to process the header again.
						context->content_status = CONTENT_STATUS_GET_CONTENT;

						// We had set the overlapped structure for file operations, but now we need to reset it for socket operations.
						_memzero( &overlapped->overlapped, sizeof( WSAOVERLAPPED ) );

						//

						context->header_info.range_info->content_offset += context->content_offset;	// The true amount that was downloaded. Allows us to resume if we stop the download.
						context->content_offset = 0;

						if ( context->header_info.chunked_transfer )
						{
							if ( ( context->parts == 1 && context->header_info.connection == CONNECTION_KEEP_ALIVE && context->header_info.got_chunk_terminator ) ||
								 ( context->parts > 1 && ( context->header_info.range_info->content_offset >= ( ( context->header_info.range_info->range_end - context->header_info.range_info->range_start ) + 1 ) ) ) )
							{
								InterlockedIncrement( &context->pending_operations );

								*current_operation = ( use_ssl ? IO_Shutdown : IO_Close );

								PostQueuedCompletionStatus( hIOCP, 0, ( ULONG_PTR )context, ( WSAOVERLAPPED * )overlapped );

								content_status = CONTENT_STATUS_NONE;
							}
						}
						else
						{
							// We need to force the keep-alive connections closed since the server will just keep it open after we've gotten all the data.
							if ( ( ( ( context->request_info.protocol == PROTOCOL_FTP ||
									   context->request_info.protocol == PROTOCOL_FTPS ||
									   context->request_info.protocol == PROTOCOL_FTPES ) && context->parts > 1 ) ||
								   context->header_info.connection == CONNECTION_KEEP_ALIVE ) &&
								 ( context->header_info.range_info->content_length == 0 ||
								 ( context->header_info.range_info->content_offset >= ( ( context->header_info.range_info->range_end - context->header_info.range_info->range_start ) + 1 ) ) ) )
							{
								InterlockedIncrement( &context->pending_operations );

								*current_operation = ( use_ssl ? IO_Shutdown : IO_Close );

								PostQueuedCompletionStatus( hIOCP, 0, ( ULONG_PTR )context, ( WSAOVERLAPPED * )overlapped );

								content_status = CONTENT_STATUS_NONE;
							}
						}

						//

						if ( content_status == CONTENT_STATUS_FAILED )
						{
							InterlockedIncrement( &context->pending_operations );

							*current_operation = ( use_ssl ? IO_Shutdown : IO_Close );

							PostQueuedCompletionStatus( hIOCP, 0, ( ULONG_PTR )context, ( WSAOVERLAPPED * )overlapped );
						}
						else if ( content_status == CONTENT_STATUS_HANDLE_RESPONSE )
						{
							if ( MakeRangeRequest( context ) == CONTENT_STATUS_FAILED )
							{
								InterlockedIncrement( &context->pending_operations );

								*current_operation = ( use_ssl ? IO_Shutdown : IO_Close );

								PostQueuedCompletionStatus( hIOCP, 0, ( ULONG_PTR )context, ( WSAOVERLAPPED * )overlapped );
							}
						}
						else if ( content_status == CONTENT_STATUS_READ_MORE_CONTENT || content_status == CONTENT_STATUS_READ_MORE_HEADER ) // Read more header information, or continue to read more content. Do not reset context->wsabuf since it may have been offset to handle partial data.
						{
							InterlockedIncrement( &context->pending_operations );

							*current_operation = IO_GetContent;

							if ( use_ssl )
							{
								if ( context->ssl->continue_decrypt )
								{
									// We need to post a non-zero status to avoid our code shutting down the connection.
									// We'll use context->current_bytes_read for that, but it can be anything that's not zero.
									PostQueuedCompletionStatus( hIOCP, context->current_bytes_read, ( ULONG_PTR )context, ( WSAOVERLAPPED * )overlapped );
								}
								else
								{
									SSL_WSARecv( context, overlapped, sent );
									if ( !sent )
									{
										*current_operation = IO_Shutdown;

										PostQueuedCompletionStatus( hIOCP, 0, ( ULONG_PTR )context, ( WSAOVERLAPPED * )overlapped );
									}
								}
							}
							else
							{
								nRet = _WSARecv( context->socket, &context->wsabuf, 1, NULL, &dwFlags, ( WSAOVERLAPPED * )overlapped, NULL );
								if ( nRet == SOCKET_ERROR && ( _WSAGetLastError() != ERROR_IO_PENDING ) )
								{
									*current_operation = IO_Close;

									PostQueuedCompletionStatus( hIOCP, 0, ( ULONG_PTR )context, ( WSAOVERLAPPED * )overlapped );
								}
							}
						}
					}
				}
				else if ( context->cleanup == 2 )	// If we've forced the cleanup, then allow it to continue its steps.
				{
					context->cleanup = 1;	// Auto cleanup.
				}
				else	// We've already shutdown and/or closed the connection.
				{
					InterlockedIncrement( &context->pending_operations );

					*current_operation = IO_Close;

					PostQueuedCompletionStatus( hIOCP, 0, ( ULONG_PTR )context, ( WSAOVERLAPPED * )overlapped );
				}

				LeaveCriticalSection( &context->context_cs );
			}
			break;

			case IO_Write:
			{
				EnterCriticalSection( &context->context_cs );

				if ( context->cleanup == 0 || context->cleanup >= 10 )
				{
					if ( context->cleanup >= 10 )
					{
						context->cleanup -= 10;
					}

					InterlockedIncrement( &context->pending_operations );

					// Make sure we've sent everything before we do anything else.
					if ( io_size < context->wsabuf.len )
					{
						context->wsabuf.buf += io_size;
						context->wsabuf.len -= io_size;

						// We do a regular WSASend here since that's what we last did in SSL_WSASend.
						nRet = _WSASend( context->socket, &context->wsabuf, 1, NULL, dwFlags, ( WSAOVERLAPPED * )overlapped, NULL );
						if ( nRet == SOCKET_ERROR && ( _WSAGetLastError() != ERROR_IO_PENDING ) )
						{
							*current_operation = IO_Close;

							PostQueuedCompletionStatus( hIOCP, 0, ( ULONG_PTR )context, ( WSAOVERLAPPED * )overlapped );
						}
					}
					else	// All the data that we wanted to send has been sent. Post our next operation.
					{
						*current_operation = *next_operation;

						context->wsabuf.buf = context->buffer;
						context->wsabuf.len = context->buffer_size;

						if ( *current_operation == IO_ServerHandshakeResponse ||
							 *current_operation == IO_ClientHandshakeResponse ||
							 *current_operation == IO_Shutdown ||
							 *current_operation == IO_Close )
						{
							PostQueuedCompletionStatus( hIOCP, 0, ( ULONG_PTR )context, ( WSAOVERLAPPED * )overlapped );
						}
						else	// Read more data.
						{
							if ( *current_operation != IO_GetCONNECTResponse &&
								 *current_operation != IO_SOCKSResponse &&
								  use_ssl )
							{
								SSL_WSARecv( context, overlapped, sent );
								if ( !sent )
								{
									*current_operation = IO_Shutdown;

									PostQueuedCompletionStatus( hIOCP, 0, ( ULONG_PTR )context, ( WSAOVERLAPPED * )overlapped );
								}
							}
							else
							{
								nRet = _WSARecv( context->socket, &context->wsabuf, 1, NULL, &dwFlags, ( WSAOVERLAPPED * )overlapped, NULL );
								if ( nRet == SOCKET_ERROR && ( _WSAGetLastError() != ERROR_IO_PENDING ) )
								{
									*current_operation = IO_Close;

									PostQueuedCompletionStatus( hIOCP, 0, ( ULONG_PTR )context, ( WSAOVERLAPPED * )overlapped );
								}
							}
						}
					}
				}
				else if ( context->cleanup == 2 )	// If we've forced the cleanup, then allow it to continue its steps.
				{
					context->cleanup = 1;	// Auto cleanup.
				}
				else	// We've already shutdown and/or closed the connection.
				{
					InterlockedIncrement( &context->pending_operations );

					*current_operation = IO_Close;

					PostQueuedCompletionStatus( hIOCP, 0, ( ULONG_PTR )context, ( WSAOVERLAPPED * )overlapped );
				}

				LeaveCriticalSection( &context->context_cs );
			}
			break;

			case IO_KeepAlive:	// For FTP keep-alive requests.
			{
				EnterCriticalSection( &context->context_cs );

				if ( context->cleanup == 0 )
				{
					// Make sure we've sent everything before we do anything else.
					if ( io_size < context->keep_alive_wsabuf.len )
					{
						context->keep_alive_wsabuf.buf += io_size;
						context->keep_alive_wsabuf.len -= io_size;

						InterlockedIncrement( &context->pending_operations );

						// We do a regular WSASend here since that's what we last did in SSL_WSASend.
						nRet = _WSASend( context->socket, &context->keep_alive_wsabuf, 1, NULL, dwFlags, ( WSAOVERLAPPED * )overlapped, NULL );
						if ( nRet == SOCKET_ERROR && ( _WSAGetLastError() != ERROR_IO_PENDING ) )
						{
							*current_operation = IO_Close;

							PostQueuedCompletionStatus( hIOCP, 0, ( ULONG_PTR )context, ( WSAOVERLAPPED * )overlapped );
						}
					}
				}
				else if ( context->cleanup == 2 )	// If we've forced the cleanup, then allow it to continue its steps.
				{
					context->cleanup = 1;	// Auto cleanup.
				}
				else	// We've already shutdown and/or closed the connection.
				{
					InterlockedIncrement( &context->pending_operations );

					*current_operation = IO_Close;

					PostQueuedCompletionStatus( hIOCP, 0, ( ULONG_PTR )context, ( WSAOVERLAPPED * )overlapped );
				}

				LeaveCriticalSection( &context->context_cs );
			}
			break;

			case IO_Shutdown:
			{
				bool fall_through = true;

				EnterCriticalSection( &context->context_cs );

				if ( context->cleanup == 0 || context->cleanup == 2 )
				{
					context->cleanup += 10;	// Allow IO_Write to continue to process.

					*next_operation = IO_Close;

					InterlockedIncrement( &context->pending_operations );

					context->wsabuf.buf = context->buffer;
					context->wsabuf.len = context->buffer_size;

					SSL_WSAShutdown( context, overlapped, sent );

					// We'll fall through the IO_Shutdown to IO_Close.
					if ( !sent )
					{
						context->cleanup -= 10;

						InterlockedDecrement( &context->pending_operations );

						*current_operation = IO_Close;
					}
					else	// The shutdown sent data. IO_Close will be called in IO_Write.
					{
						fall_through = false;
					}
				}
				/*else	// We've already shutdown and/or closed the connection.
				{
					fall_through = false;

					InterlockedIncrement( &context->pending_operations );

					*current_operation = IO_Close;

					PostQueuedCompletionStatus( hIOCP, 0, ( ULONG_PTR )context, ( WSAOVERLAPPED * )overlapped );
				}*/

				LeaveCriticalSection( &context->context_cs );

				if ( !fall_through )
				{
					break;
				}
			}

			case IO_Close:
			{
				bool cleanup = true;

				EnterCriticalSection( &context->context_cs );

				if ( context->pending_operations > 0 )
				{
					cleanup = false;

					context->cleanup = 1;	// Auto cleanup.

					if ( context->socket != INVALID_SOCKET )
					{
						SOCKET s = context->socket;
						context->socket = INVALID_SOCKET;
						_shutdown( s, SD_BOTH );
						_closesocket( s );	// Saves us from having to post if there's already a pending IO operation. Should force the operation to complete.
					}
				}

				LeaveCriticalSection( &context->context_cs );

				if ( cleanup )
				{
					CleanupConnection( context );
				}
			}
			break;
		}
	}

	_ExitThread( 0 );
	return 0;
}

SOCKET_CONTEXT *CreateSocketContext()
{
	SOCKET_CONTEXT *context = ( SOCKET_CONTEXT * )GlobalAlloc( GPTR, sizeof( SOCKET_CONTEXT ) );
	if ( context != NULL )
	{
		context->buffer = ( char * )GlobalAlloc( GPTR, sizeof( char ) * ( BUFFER_SIZE + 1 ) );
		if ( context->buffer != NULL )
		{
			context->buffer_size = BUFFER_SIZE;

			context->wsabuf.buf = context->buffer;
			context->wsabuf.len = context->buffer_size;

			context->socket = INVALID_SOCKET;
			context->listen_socket = INVALID_SOCKET;

			context->overlapped.context = context;
			context->overlapped_close.context = context;
			context->overlapped_keep_alive.context = context;

			InitializeCriticalSection( &context->context_cs );
		}
		else
		{
			GlobalFree( context );
			context = NULL;
		}
	}

	return context;
}

bool CreateConnection( SOCKET_CONTEXT *context, char *host, unsigned short port )
{
	if ( context == NULL || host == NULL )
	{
		return false;
	}

	int nRet = 0;

	struct addrinfoW hints;

	bool use_ipv6 = false;

	wchar_t *whost = NULL, *t_whost = NULL;
	wchar_t wcs_ip[ 16 ];
	wchar_t wport[ 6 ];

	if ( context->address_info == NULL )
	{
		// Resolve the remote host.
		_memzero( &hints, sizeof( addrinfoW ) );
		hints.ai_family = AF_INET;
		hints.ai_socktype = SOCK_STREAM;
		hints.ai_protocol = IPPROTO_IP;

		if ( cfg_enable_proxy && context->request_info.protocol == PROTOCOL_HTTP )
		{
			__snwprintf( wport, 6, L"%hu", cfg_port );

			if ( cfg_address_type == 0 )
			{
				whost = ( g_punycode_hostname != NULL ? g_punycode_hostname : cfg_hostname );
			}
			else
			{
				struct sockaddr_in src_addr;
				_memzero( &src_addr, sizeof( sockaddr_in ) );

				src_addr.sin_family = AF_INET;
				src_addr.sin_addr.s_addr = _htonl( cfg_ip_address );

				DWORD wcs_ip_length = 16;
				_WSAAddressToStringW( ( SOCKADDR * )&src_addr, sizeof( struct sockaddr_in ), NULL, wcs_ip, &wcs_ip_length );

				whost = wcs_ip;
			}
		}
		else if ( cfg_enable_proxy_s && context->request_info.protocol == PROTOCOL_HTTPS )
		{
			__snwprintf( wport, 6, L"%hu", cfg_port_s );

			if ( cfg_address_type_s == 0 )
			{
				whost = ( g_punycode_hostname_s != NULL ? g_punycode_hostname_s : cfg_hostname_s );
			}
			else
			{
				struct sockaddr_in src_addr;
				_memzero( &src_addr, sizeof( sockaddr_in ) );

				src_addr.sin_family = AF_INET;
				src_addr.sin_addr.s_addr = _htonl( cfg_ip_address_s );

				DWORD wcs_ip_length = 16;
				_WSAAddressToStringW( ( SOCKADDR * )&src_addr, sizeof( struct sockaddr_in ), NULL, wcs_ip, &wcs_ip_length );

				whost = wcs_ip;
			}
		}
		else if ( cfg_enable_proxy_socks )
		{
			__snwprintf( wport, 6, L"%hu", cfg_port_socks );

			if ( cfg_address_type_socks == 0 )
			{
				whost = ( g_punycode_hostname_socks != NULL ? g_punycode_hostname_socks : cfg_hostname_socks );
			}
			else
			{
				struct sockaddr_in src_addr;
				_memzero( &src_addr, sizeof( sockaddr_in ) );

				src_addr.sin_family = AF_INET;
				src_addr.sin_addr.s_addr = _htonl( cfg_ip_address_socks );

				DWORD wcs_ip_length = 16;
				_WSAAddressToStringW( ( SOCKADDR * )&src_addr, sizeof( struct sockaddr_in ), NULL, wcs_ip, &wcs_ip_length );

				whost = wcs_ip;
			}
		}
		else
		{
			__snwprintf( wport, 6, L"%hu", port );

			int whost_length = MultiByteToWideChar( CP_UTF8, 0, host, -1, NULL, 0 );	// Include the NULL terminator.
			whost = ( wchar_t * )GlobalAlloc( GMEM_FIXED, sizeof( wchar_t ) * whost_length );
			MultiByteToWideChar( CP_UTF8, 0, host, -1, whost, whost_length );

			t_whost = whost;
		}

		nRet = _GetAddrInfoW( whost, wport, &hints, &context->address_info );
		if ( nRet == WSAHOST_NOT_FOUND )
		{
			use_ipv6 = true;

			hints.ai_family = AF_INET6;	// Try IPv6
			nRet = _GetAddrInfoW( whost, wport, &hints, &context->address_info );
		}

		GlobalFree( t_whost );

		if ( nRet != 0 )
		{
			return false;
		}
	}

	if ( cfg_enable_proxy_socks &&
		 context->proxy_address_info == NULL &&
		 ( ( cfg_socks_type == SOCKS_TYPE_V4 && !cfg_resolve_domain_names_v4a ) ||
		   ( cfg_socks_type == SOCKS_TYPE_V5 && !cfg_resolve_domain_names ) ) )
	{
		_memzero( &hints, sizeof( addrinfoW ) );
		hints.ai_family = AF_INET;
		hints.ai_socktype = SOCK_STREAM;
		hints.ai_protocol = IPPROTO_IP;

		__snwprintf( wport, 6, L"%hu", context->request_info.port );

		int whost_length = MultiByteToWideChar( CP_UTF8, 0, context->request_info.host, -1, NULL, 0 );	// Include the NULL terminator.
		whost = ( wchar_t * )GlobalAlloc( GMEM_FIXED, sizeof( wchar_t ) * whost_length );
		MultiByteToWideChar( CP_UTF8, 0, context->request_info.host, -1, whost, whost_length );

		nRet = _GetAddrInfoW( whost, wport, &hints, &context->proxy_address_info );
		if ( nRet == WSAHOST_NOT_FOUND && cfg_socks_type == SOCKS_TYPE_V5 )	// Allow IPv6 for SOCKS 5
		{
			hints.ai_family = AF_INET6;	// Try IPv6
			_GetAddrInfoW( whost, wport, &hints, &context->proxy_address_info );
		}

		GlobalFree( whost );
	}

	SOCKET socket = CreateSocket( use_ipv6 );
	if ( socket == INVALID_SOCKET )
	{
		return false;
	}

	context->socket = socket;

	g_hIOCP = CreateIoCompletionPort( ( HANDLE )socket, g_hIOCP, 0/*( ULONG_PTR )context*/, 0 );
	if ( g_hIOCP == NULL )
	{
		return false;
	}

	// Socket must be bound before we can use it with ConnectEx.
	struct sockaddr_in ipv4_addr;
	struct sockaddr_in6 ipv6_addr;

	if ( use_ipv6 )
	{
		_memzero( &ipv6_addr, sizeof( ipv6_addr ) );
		ipv6_addr.sin6_family = AF_INET6;
		//ipv6_addr.sin6_addr = in6addr_any;	// This assignment requires the CRT, but it's all zeros anyway and it gets set by _memzero().
		//ipv6_addr.sin6_port = 0;
		nRet = _bind( socket, ( SOCKADDR * )&ipv6_addr, sizeof( ipv6_addr ) );
	}
	else
	{
		_memzero( &ipv4_addr, sizeof( ipv4_addr ) );
		ipv4_addr.sin_family = AF_INET;
		//ipv4_addr.sin_addr.s_addr = INADDR_ANY;
		//ipv4_addr.sin_port = 0;
		nRet = _bind( socket, ( SOCKADDR * )&ipv4_addr, sizeof( ipv4_addr ) );
	}

	if ( nRet == SOCKET_ERROR )
	{
		return false;
	}

	// Attempt to connect to the host.
	InterlockedIncrement( &context->pending_operations );

	context->overlapped.current_operation = IO_Connect;

	DWORD lpdwBytesSent = 0;
	BOOL bRet = _ConnectEx( socket, context->address_info->ai_addr, ( int )context->address_info->ai_addrlen, NULL, 0, &lpdwBytesSent, ( OVERLAPPED * )&context->overlapped );
	if ( bRet == FALSE && ( _WSAGetLastError() != ERROR_IO_PENDING ) )
	{
		InterlockedDecrement( &context->pending_operations );

		/*if ( context->address_info != NULL )
		{
			_FreeAddrInfoW( context->address_info );
		}*/

		return false;
	}

	/*if ( context->address_info != NULL )
	{
		_FreeAddrInfoW( context->address_info );
	}*/

	return true;
}

void UpdateRangeList( DOWNLOAD_INFO *di )
{
	if ( di == NULL )
	{
		return;
	}

	RANGE_INFO *ri;
	RANGE_INFO *ri_copy;
	DoublyLinkedList *range_node = di->range_list;
	DoublyLinkedList *range_node_copy;

	if ( range_node != NULL )
	{
		unsigned char range_info_count = 0;

		DoublyLinkedList *active_range_list = NULL;

		// Determine the number of ranges that still need downloading.
		while ( range_node != NULL )
		{
			ri = ( RANGE_INFO * )range_node->data;

			// Check if our range still needs to download.
			if ( ri != NULL && ( ri->content_offset < ( ( ri->range_end - ri->range_start ) + 1 ) ) )
			{
				++range_info_count;

				RANGE_INFO *ri_copy = ( RANGE_INFO * )GlobalAlloc( GMEM_FIXED, sizeof( RANGE_INFO ) );

				ri_copy->content_length = ri->content_length;
				ri_copy->content_offset = ri->content_offset;
				ri_copy->file_write_offset = ri->file_write_offset;
				ri_copy->range_end = ri->range_end;
				ri_copy->range_start = ri->range_start;

				DoublyLinkedList *new_range_node = DLL_CreateNode( ( void * )ri_copy );
				DLL_AddNode( &active_range_list, new_range_node, -1 );
			}

			range_node = range_node->next;
		}

		// Can we split any remaining parts to fill the total?
		if ( range_info_count > 0 && range_info_count < di->parts )
		{
			unsigned char parts = di->parts / range_info_count;
			unsigned char rem_parts = di->parts % range_info_count;
			unsigned char total_parts = di->parts - rem_parts;

			range_node = di->range_list;
			range_node_copy = active_range_list;

			while ( range_node_copy != NULL )
			{
				unsigned char t_parts = parts;

				if ( rem_parts > 0 )	// Distribute any remainder parts amongst the remaining ranges.
				{
					++t_parts;
					--rem_parts;
				}

				ri_copy = ( RANGE_INFO * )range_node_copy->data;

				unsigned long long remaining_length = ( ( ri_copy->range_end - ri_copy->range_start ) + 1 ) - ri_copy->content_offset;

				// We'll only use 1 part for this range since it's too small to split up and the remainder of parts will be used for the next range.
				if ( remaining_length < t_parts )
				{
					rem_parts += ( t_parts - 1 );
					t_parts = 1;
				}

				unsigned long long range_size = remaining_length / t_parts;
				unsigned long long range_offset = ri_copy->range_start + ri_copy->content_offset;
				unsigned long long range_end = ri_copy->range_end;

				for ( unsigned char i = 1; i <= t_parts; ++i )
				{
					ri = ( RANGE_INFO * )range_node->data;

					// Reuse this range info.
					ri->content_length = 0;
					ri->content_offset = 0;

					if ( i == 1 )
					{
						ri->range_start = range_offset;
					}
					else
					{
						ri->range_start = range_offset + 1;
					}

					if ( i < t_parts )
					{
						range_offset += range_size;
						ri->range_end = range_offset;
					}
					else	// Make sure we have an accurate range end for the last part.
					{
						ri->range_end = range_end;
					}

					ri->file_write_offset = ri->range_start;

					range_node = range_node->next;
				}

				range_node_copy = range_node_copy->next;
			}

			di->range_list_end = range_node;

			// Zero out the unused range info.
			/*while ( range_node != NULL )
			{
				ri = ( RANGE_INFO * )range_node->data;
				ri->content_length = 0;
				ri->content_offset = 0;
				ri->file_write_offset = 0;
				ri->range_end = 0;
				ri->range_start = 0;

				range_node = range_node->next;
			}*/
		}

		while ( active_range_list != NULL )
		{
			range_node = active_range_list;
			active_range_list = active_range_list->next;

			GlobalFree( range_node->data );
			GlobalFree( range_node );
		}
	}
	else
	{
		ri = ( RANGE_INFO * )GlobalAlloc( GPTR, sizeof( RANGE_INFO ) );
		range_node = DLL_CreateNode( ( void * )ri );
		DLL_AddNode( &di->range_list, range_node, -1 );
	}
}

void StartDownload( DOWNLOAD_INFO *di, bool check_if_file_exists )
{
	if ( di == NULL )
	{
		return;
	}

	unsigned char add_state = 0;

	PROTOCOL protocol = PROTOCOL_UNKNOWN;
	wchar_t *host = NULL;
	wchar_t *resource = NULL;
	unsigned short port = 0;

	if ( check_if_file_exists )
	{
		bool skip_start = false;

		EnterCriticalSection( &di->shared_cs );

		wchar_t prompt_message[ MAX_PATH + 512 ];
		wchar_t file_path[ MAX_PATH ];

		int filename_offset;
		int file_extension_offset;

		if ( cfg_use_temp_download_directory )
		{
			int filename_length = GetTemporaryFilePath( di, file_path );

			filename_offset = g_temp_download_directory_length + 1;
			file_extension_offset = filename_offset + get_file_extension_offset( di->file_path + di->filename_offset, filename_length );
		}
		else
		{
			GetDownloadFilePath( di, file_path );

			filename_offset = di->filename_offset;
			file_extension_offset = di->file_extension_offset;
		}

		// See if the file exits.
		if ( GetFileAttributes( file_path ) != INVALID_FILE_ATTRIBUTES )
		{
			if ( cfg_prompt_rename == 0 && di->download_operations & DOWNLOAD_OPERATION_OVERRIDE_PROMPTS )
			{
				di->status = STATUS_SKIPPED;

				skip_start = true;
			}
			else
			{
				// If the last return value was not set to remember our choice, then prompt again.
				if ( cfg_prompt_rename == 0 &&
					 g_rename_file_cmb_ret != CMBIDRENAMEALL &&
					 g_rename_file_cmb_ret != CMBIDOVERWRITEALL &&
					 g_rename_file_cmb_ret != CMBIDSKIPALL )
				{
					__snwprintf( prompt_message, MAX_PATH + 512, ST_V_PROMPT___already_exists, file_path );

					g_rename_file_cmb_ret = CMessageBoxW( g_hWnd_main, prompt_message, PROGRAM_CAPTION, CMB_ICONWARNING | CMB_RENAMEOVERWRITESKIPALL );
				}

				// Rename the file and try again.
				if ( cfg_prompt_rename == 1 ||
				   ( cfg_prompt_rename == 0 && ( g_rename_file_cmb_ret == CMBIDRENAME ||
												 g_rename_file_cmb_ret == CMBIDRENAMEALL ) ) )
				{
					// Creates a tree of active and queued downloads.
					dllrbt_tree *add_files_tree = CreateFilenameTree();

					bool rename_succeeded = RenameFile( di, add_files_tree, file_path, filename_offset, file_extension_offset );

					// The tree is only used to determine duplicate filenames.
					DestroyFilenameTree( add_files_tree );

					if ( !rename_succeeded )
					{
						if ( g_rename_file_cmb_ret2 != CMBIDOKALL && !( di->download_operations & DOWNLOAD_OPERATION_OVERRIDE_PROMPTS ) )
						{
							__snwprintf( prompt_message, MAX_PATH + 512, ST_V_PROMPT___could_not_be_renamed, file_path );

							g_rename_file_cmb_ret2 = CMessageBoxW( g_hWnd_main, prompt_message, PROGRAM_CAPTION, CMB_ICONWARNING | CMB_OKALL );
						}

						di->status = STATUS_SKIPPED;

						skip_start = true;
					}
				}
				else if ( cfg_prompt_rename == 3 ||
						( cfg_prompt_rename == 0 && ( g_rename_file_cmb_ret == CMBIDFAIL ||
													  g_rename_file_cmb_ret == CMBIDSKIP ||
													  g_rename_file_cmb_ret == CMBIDSKIPALL ) ) ) // Skip the rename or overwrite if the return value fails, or the user selected skip.
				{
					di->status = STATUS_SKIPPED;

					skip_start = true;
				}
			}
		}

		LeaveCriticalSection( &di->shared_cs );

		if ( skip_start )
		{
			return;
		}
	}

	unsigned int host_length = 0;
	unsigned int resource_length = 0;

	ParseURL_W( di->url, NULL, protocol, &host, host_length, port, &resource, resource_length, NULL, NULL, NULL, NULL );

	wchar_t *w_resource;

	if ( protocol == PROTOCOL_FTP ||
		 protocol == PROTOCOL_FTPS ||
		 protocol == PROTOCOL_FTPES )
	{
		w_resource = url_decode_w( resource, resource_length, &resource_length );

		if ( w_resource != NULL )
		{
			GlobalFree( resource );
			resource = w_resource;
		}
	}

	w_resource = resource;

	if ( protocol != PROTOCOL_FTP &&
		 protocol != PROTOCOL_FTPS &&
		 protocol != PROTOCOL_FTPES )
	{
		while ( *w_resource != NULL )
		{
			if ( *w_resource == L'#' )
			{
				*w_resource = 0;
				resource_length = ( unsigned int )( w_resource - resource );

				break;
			}

			++w_resource;
		}
	}

	/*w_resource = url_encode_w( resource, resource_length, &w_resource_length );

	// Did we encode anything?
	if ( resource_length != w_resource_length )
	{
		GlobalFree( resource );
		resource = w_resource;

		resource_length = w_resource_length;
	}
	else
	{
		GlobalFree( w_resource );
	}*/

	if ( normaliz_state == NORMALIZ_STATE_RUNNING )
	{
		int punycode_length = _IdnToAscii( 0, host, host_length, NULL, 0 );

		if ( ( unsigned int )punycode_length > host_length )
		{
			wchar_t *punycode = ( wchar_t * )GlobalAlloc( GMEM_FIXED, sizeof( wchar_t ) * ( punycode_length + 1 ) );
			host_length = _IdnToAscii( 0, host, host_length, punycode, punycode_length );
			punycode[ host_length ] = 0;	// Sanity.

			GlobalFree( host );
			host = punycode;
		}
	}

	unsigned char part = 1;

	EnterCriticalSection( &cleanup_cs );

	// If the number of ranges is less than the total number of parts that's been set for the download,
	// then the remaining ranges will be split to equal the total number of parts.
	UpdateRangeList( di );

	di->print_range_list = di->range_list;
	di->range_queue = NULL;

	DoublyLinkedList *range_node = di->range_list;

	while ( range_node != di->range_list_end )
	{
		RANGE_INFO *ri = ( RANGE_INFO * )range_node->data;

		// Check if our range still needs to download.
		if ( ri != NULL && ( ri->content_offset < ( ( ri->range_end - ri->range_start ) + 1 ) ) )
		{
			// Split the remaining range_list off into the range_queue.
			if ( di->parts_limit > 0 && part > di->parts_limit )
			{
				di->range_queue = range_node;

				break;
			}

			// Check the state of our downloads/queue once.
			if ( add_state == 0 )
			{
				if ( total_downloading < cfg_max_downloads )
				{
					add_state = 1;	// Create the connection.

					// Set the start time only if we've manually started the download.
					if ( di->start_time.QuadPart == 0 )
					{
						FILETIME ft;
						GetSystemTimeAsFileTime( &ft );
						di->start_time.LowPart = ft.dwLowDateTime;
						di->start_time.HighPart = ft.dwHighDateTime;
					}

					di->status = STATUS_CONNECTING;	// Connecting.

					EnableTimers( true );

					EnterCriticalSection( &active_download_list_cs );

					// Add to the global active download list.
					di->download_node.data = di;
					DLL_AddNode( &active_download_list, &di->download_node, -1 );

					++total_downloading;

					LeaveCriticalSection( &active_download_list_cs );
				}
				else
				{
					add_state = 2;	// Queue the download.

					di->status = STATUS_CONNECTING | STATUS_QUEUED;	// Queued.

					EnterCriticalSection( &download_queue_cs );
					
					// Add to the global download queue.
					di->queue_node.data = di;
					DLL_AddNode( &download_queue, &di->queue_node, -1 );

					LeaveCriticalSection( &download_queue_cs );
				}
			}

			di->last_downloaded = di->downloaded;

			if ( add_state == 1 )
			{
				// Save the request information, the header information (if we got any), and create a new connection.
				SOCKET_CONTEXT *context = CreateSocketContext();

				if ( protocol == PROTOCOL_FTP ||
					 protocol == PROTOCOL_FTPS ||
					 protocol == PROTOCOL_FTPES )
				{
					context->ftp_connection_type = FTP_CONNECTION_TYPE_CONTROL;
				}

				context->processed_header = di->processed_header;

				context->part = part;
				context->parts = di->parts;

				// If we've processed the header, then we would have already gotten a content disposition filename.
				if ( di->processed_header )
				{
					context->got_filename = 1;
				}

				context->request_info.port = port;
				context->request_info.protocol = protocol;

				int cfg_val_length = WideCharToMultiByte( CP_UTF8, 0, host, host_length + 1, NULL, 0, NULL, NULL );
				char *utf8_cfg_val = ( char * )GlobalAlloc( GMEM_FIXED, sizeof( char ) * cfg_val_length ); // Size includes the null character.
				WideCharToMultiByte( CP_UTF8, 0, host, host_length + 1, utf8_cfg_val, cfg_val_length, NULL, NULL );

				context->request_info.host = utf8_cfg_val;

				cfg_val_length = WideCharToMultiByte( CP_UTF8, 0, resource, resource_length + 1, NULL, 0, NULL, NULL );
				utf8_cfg_val = ( char * )GlobalAlloc( GMEM_FIXED, sizeof( char ) * cfg_val_length ); // Size includes the null character.
				WideCharToMultiByte( CP_UTF8, 0, resource, resource_length + 1, utf8_cfg_val, cfg_val_length, NULL, NULL );

				context->request_info.resource = utf8_cfg_val;

				context->download_info = di;

				ri->range_start += ri->content_offset;	// Begin where we left off.
				ri->content_offset = 0;	// Reset.

				context->header_info.range_info = ri;

				if ( di->cookies != NULL )
				{
					char *new_cookies = NULL;

					// This value will be saved
					if ( !ParseCookieValues( di->cookies, &context->header_info.cookie_tree, &new_cookies ) )
					{
						GlobalFree( new_cookies );
						new_cookies = NULL;
					}

					// If we got a new cookie.
					if ( new_cookies != NULL )
					{
						// Then see if the new cookie is not blank.
						if ( new_cookies[ 0 ] != NULL )
						{
							context->header_info.cookies = new_cookies;
						}
						else	// Otherwise, if the cookie is blank, then free it.
						{
							GlobalFree( new_cookies );
						}
					}
				}

				// Add to the parts list.
				context->parts_node.data = context;
				DLL_AddNode( &context->download_info->parts_list, &context->parts_node, -1 );

				context->context_node.data = context;

				EnterCriticalSection( &context_list_cs );

				// Add to the global download list.
				DLL_AddNode( &g_context_list, &context->context_node, 0 );

				LeaveCriticalSection( &context_list_cs );

				EnterCriticalSection( &di->shared_cs );

				++( di->active_parts );

				LeaveCriticalSection( &di->shared_cs );

				context->status = STATUS_CONNECTING;

				if ( !CreateConnection( context, context->request_info.host, context->request_info.port ) )
				{
					context->status = STATUS_FAILED;

					CleanupConnection( context );
				}
			}
			else if ( add_state == 2 )
			{
				add_state = 3;	// Skip adding anymore values to the queue.

				EnterCriticalSection( &di->shared_cs );

				di->active_parts = 0;

				LeaveCriticalSection( &di->shared_cs );
			}

			++part;
		}

		range_node = range_node->next;
	}

	LeaveCriticalSection( &cleanup_cs );

	GlobalFree( host );
	GlobalFree( resource );
}

dllrbt_tree *CreateFilenameTree()
{
	DOWNLOAD_INFO *di = NULL;
	wchar_t *filename = NULL;

	// Make a tree of active and queued downloads to find filenames that need to be renamed.
	dllrbt_tree *filename_tree = dllrbt_create( dllrbt_compare_w );

	EnterCriticalSection( &download_queue_cs );

	DoublyLinkedList *tmp_node = download_queue;
	while ( tmp_node != NULL )
	{
		di = ( DOWNLOAD_INFO * )tmp_node->data;
		if ( di != NULL )
		{
			filename = GlobalStrDupW( di->file_path + di->filename_offset );

			if ( dllrbt_insert( filename_tree, ( void * )filename, ( void * )filename ) != DLLRBT_STATUS_OK )
			{
				GlobalFree( filename );
			}
		}

		tmp_node = tmp_node->next;
	}

	LeaveCriticalSection( &download_queue_cs );

	EnterCriticalSection( &active_download_list_cs );

	tmp_node = active_download_list;
	while ( tmp_node != NULL )
	{
		di = ( DOWNLOAD_INFO * )tmp_node->data;
		if ( di != NULL )
		{
			filename = GlobalStrDupW( di->file_path + di->filename_offset );

			if ( dllrbt_insert( filename_tree, ( void * )filename, ( void * )filename ) != DLLRBT_STATUS_OK )
			{
				GlobalFree( filename );
			}
		}

		tmp_node = tmp_node->next;
	}

	LeaveCriticalSection( &active_download_list_cs );

	return filename_tree;
}

void DestroyFilenameTree( dllrbt_tree *filename_tree )
{
	// The tree is only used to determine duplicate filenames.
	node_type *node = dllrbt_get_head( filename_tree );
	while ( node != NULL )
	{
		wchar_t *filename = ( wchar_t * )node->val;

		if ( filename != NULL )
		{
			GlobalFree( filename );
		}

		node = node->next;
	}
	dllrbt_delete_recursively( filename_tree );
}

bool RenameFile( DOWNLOAD_INFO *di, dllrbt_tree *filename_tree, wchar_t *file_path, unsigned int filename_offset, unsigned int file_extension_offset )
{
	unsigned int rename_count = 0;

	// The maximum folder path length is 248 (including the trailing '\').
	// The maximum file name length in the case above is 11 (not including the NULL terminator).
	// The total is 259 characters (not including the NULL terminator).
	// MAX_PATH is 260.

	// We don't want to overwrite the download info until the very end.
	wchar_t new_file_path[ MAX_PATH ];
	_wmemcpy_s( new_file_path, MAX_PATH, file_path, MAX_PATH );

	new_file_path[ filename_offset - 1 ] = L'\\';	// Replace the download directory NULL terminator with a directory slash.

	do
	{
		while ( dllrbt_find( filename_tree, ( void * )( new_file_path + filename_offset ), false ) != NULL )
		{
			// If there's a file extension, then put the counter before it.
			int ret = __snwprintf( new_file_path + file_extension_offset, MAX_PATH - file_extension_offset - 1, L" (%lu)%s", ++rename_count, file_path + file_extension_offset );

			// Can't rename.
			if ( ret < 0 )
			{
				return false;
			}
		}

		// Add the new filename to the add files tree.
		wchar_t *filename = GlobalStrDupW( new_file_path + filename_offset );

		if ( dllrbt_insert( filename_tree, ( void * )filename, ( void * )filename ) != DLLRBT_STATUS_OK )
		{
			GlobalFree( filename );
		}
	}
	while ( GetFileAttributes( new_file_path ) != INVALID_FILE_ATTRIBUTES );

	// Set the new filename.
	_wmemcpy_s( di->file_path + di->filename_offset, MAX_PATH - di->filename_offset, new_file_path + filename_offset, MAX_PATH - di->filename_offset );
	di->file_path[ MAX_PATH - 1 ] = 0;	// Sanity.

	// Get the new file extension offset.
	di->file_extension_offset = di->filename_offset + get_file_extension_offset( di->file_path + di->filename_offset, lstrlenW( di->file_path + di->filename_offset ) );

	return true;
}

ICON_INFO *CacheIcon( DOWNLOAD_INFO *di, SHFILEINFO *sfi )
{
	ICON_INFO *ii = NULL;

	if ( di != NULL && sfi != NULL )
	{
		// Cache our file's icon.
		EnterCriticalSection( &icon_cache_cs );
		ii = ( ICON_INFO * )dllrbt_find( g_icon_handles, ( void * )( di->file_path + di->file_extension_offset ), true );
		if ( ii == NULL )
		{
			bool destroy = true;
			#ifndef OLE32_USE_STATIC_LIB
				if ( ole32_state == OLE32_STATE_SHUTDOWN )
				{
					destroy = InitializeOle32();
				}
			#endif

			if ( destroy )
			{
				_CoInitializeEx( NULL, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE );
			}

			// Use an unknown file type icon for extensionless files.
			_SHGetFileInfoW( ( di->file_path[ di->file_extension_offset ] != 0 ? di->file_path + di->file_extension_offset : L" " ), FILE_ATTRIBUTE_NORMAL, sfi, sizeof( SHFILEINFO ), SHGFI_USEFILEATTRIBUTES | SHGFI_ICON | SHGFI_SMALLICON );

			if ( destroy )
			{
				_CoUninitialize();
			}

			ii = ( ICON_INFO * )GlobalAlloc( GMEM_FIXED, sizeof( DOWNLOAD_INFO ) );

			ii->file_extension = GlobalStrDupW( di->file_path + di->file_extension_offset );
			ii->icon = sfi->hIcon;

			ii->count = 1;

			if ( dllrbt_insert( g_icon_handles, ( void * )ii->file_extension, ( void * )ii ) != DLLRBT_STATUS_OK )
			{
				DestroyIcon( ii->icon );
				GlobalFree( ii->file_extension );
				GlobalFree( ii );
				ii = NULL;
			}
		}
		else
		{
			++( ii->count );
		}
		LeaveCriticalSection( &icon_cache_cs );
	}

	return ii;
}

DWORD WINAPI AddURL( void *add_info )
{
	if ( add_info == NULL )
	{
		_ExitThread( 0 );
		return 0;
	}

	EnterCriticalSection( &worker_cs );

	in_worker_thread = true;

	ProcessingList( true );

	ADD_INFO *ai = ( ADD_INFO * )add_info;

	wchar_t *url_list = ai->urls;

	wchar_t *host = NULL;
	wchar_t *resource = NULL;

	PROTOCOL protocol = PROTOCOL_UNKNOWN;
	unsigned short port = 0;

	unsigned int host_length = 0;
	unsigned int resource_length = 0;

	wchar_t *url_username = NULL;
	wchar_t *url_password = NULL;

	unsigned int url_username_length = 0;
	unsigned int url_password_length = 0;

	int ai_username_length = 0;	// The original length from our auth_info struct.
	int ai_password_length = 0;

	char *username = NULL;
	char *password = NULL;

	int username_length = 0;
	int password_length = 0;
	int cookies_length = 0;
	int headers_length = 0;
	int data_length = 0;

	if ( ai->auth_info.username != NULL )
	{
		username = ai->auth_info.username;
		username_length = ai_username_length = lstrlenA( username );
	}

	if ( ai->auth_info.password != NULL )
	{
		password = ai->auth_info.password;
		password_length = ai_password_length = lstrlenA( password );
	}

	if ( ai->utf8_cookies != NULL )
	{
		cookies_length = lstrlenA( ai->utf8_cookies );
	}

	if ( ai->utf8_headers != NULL )
	{
		headers_length = lstrlenA( ai->utf8_headers );
	}

	if ( ai->utf8_data != NULL )
	{
		data_length = lstrlenA( ai->utf8_data );
	}

	if ( ai->method == METHOD_NONE )
	{
		ai->method = METHOD_GET;
	}

	SHFILEINFO *sfi = ( SHFILEINFO * )GlobalAlloc( GMEM_FIXED, sizeof( SHFILEINFO ) );

	// Creates a tree of active and queued downloads.
	dllrbt_tree *add_files_tree = CreateFilenameTree();

	while ( url_list != NULL )
	{
		// Stop processing and exit the thread.
		if ( kill_worker_thread_flag )
		{
			break;
		}

		// Find the end of the current url.
		wchar_t *current_url = url_list;

		// Remove anything before our URL (spaces, tabs, newlines, etc.)
		while ( *current_url != 0 && ( ( *current_url != L'h' && *current_url != L'H' ) && ( *current_url != L'f' && *current_url != L'F' ) ) )
		{
			++current_url;
		}

		int current_url_length = 0;

		bool decode_converted_resource = false;
		unsigned int white_space_count = 0;

		while ( *url_list != NULL )
		{
			if ( *url_list == L'%' )
			{
				decode_converted_resource = true;
			}
			else if ( *url_list == L' ' )
			{
				++white_space_count;
			}
			else if ( *url_list == L'\r' && *( url_list + 1 ) == L'\n' )
			{
				*url_list = 0;	// Sanity.

				current_url_length = ( int )( url_list - current_url );

				url_list += 2;

				break;
			}

			++url_list;
		}

		if ( *url_list == NULL )
		{
			current_url_length = ( int )( url_list - current_url );

			url_list = NULL;
		}

		// Remove whitespace at the end of our URL.
		while ( current_url_length > 0 )
		{
			if ( current_url[ current_url_length - 1 ] != L' ' && current_url[ current_url_length - 1 ] != L'\t' && current_url[ current_url_length - 1 ] != L'\f' )
			{
				break;
			}
			else
			{
				if ( current_url[ current_url_length - 1 ] == L' ' )
				{
					--white_space_count;
				}

				current_url[ current_url_length - 1 ] = 0;	// Sanity.
			}

			--current_url_length;
		}

		wchar_t *current_url_encoded = NULL;

		if ( white_space_count > 0 && *current_url != L'f' && *current_url != L'F' )
		{
			wchar_t *pstr = current_url;
			current_url_encoded = ( wchar_t * )GlobalAlloc( GMEM_FIXED, sizeof( wchar_t ) * ( current_url_length + ( white_space_count * 2 ) + 1 ) );
			wchar_t *pbuf = current_url_encoded;

			while ( pstr < ( current_url + current_url_length ) )
			{
				if ( *pstr == L' ' )
				{
					pbuf[ 0 ] = L'%';
					pbuf[ 1 ] = L'2';
					pbuf[ 2 ] = L'0';

					pbuf = pbuf + 3;
				}
				else
				{
					*pbuf++ = *pstr;
				}

				++pstr;
			}

			*pbuf = L'\0';
		}

		// Reset.
		protocol = PROTOCOL_UNKNOWN;
		host = NULL;
		resource = NULL;
		port = 0;

		host_length = 0;
		resource_length = 0;

		url_username = NULL;
		url_password = NULL;

		url_username_length = 0;
		url_password_length = 0;

		ParseURL_W( ( current_url_encoded != NULL ? current_url_encoded : current_url ), NULL, protocol, &host, host_length, port, &resource, resource_length, &url_username, &url_username_length, &url_password, &url_password_length );

		// The username and password could be encoded.
		if ( url_username != NULL )
		{
			int val_length = WideCharToMultiByte( CP_UTF8, 0, url_username, url_username_length + 1, NULL, 0, NULL, NULL );
			char *utf8_val = ( char * )GlobalAlloc( GMEM_FIXED, sizeof( char ) * val_length ); // Size includes the null character.
			WideCharToMultiByte( CP_UTF8, 0, url_username, url_username_length + 1, utf8_val, val_length, NULL, NULL );

			url_username_length = 0;
			username = url_decode_a( utf8_val, val_length - 1, &url_username_length );
			username_length = url_username_length;
			GlobalFree( utf8_val );

			//

			if ( url_password != NULL )
			{
				val_length = WideCharToMultiByte( CP_UTF8, 0, url_password, url_password_length + 1, NULL, 0, NULL, NULL );
				utf8_val = ( char * )GlobalAlloc( GMEM_FIXED, sizeof( char ) * val_length ); // Size includes the null character.
				WideCharToMultiByte( CP_UTF8, 0, url_password, url_password_length + 1, utf8_val, val_length, NULL, NULL );

				url_password_length = 0;
				password = url_decode_a( utf8_val, val_length - 1, &url_password_length );
				password_length = url_password_length;
				GlobalFree( utf8_val );
			}
			else
			{
				password = NULL;
				password_length = 0;
			}
		}

		if ( ( protocol != PROTOCOL_UNKNOWN && protocol != PROTOCOL_RELATIVE ) &&
			   host != NULL && resource != NULL && port != 0 )
		{
			DOWNLOAD_INFO *di = ( DOWNLOAD_INFO * )GlobalAlloc( GPTR, sizeof( DOWNLOAD_INFO ) );

			if ( !( ai->download_operations & DOWNLOAD_OPERATION_SIMULATE ) )
			{
				di->filename_offset = lstrlenW( ai->download_directory );
				_wmemcpy_s( di->file_path, MAX_PATH, ai->download_directory, di->filename_offset );
				di->file_path[ di->filename_offset ] = 0;	// Sanity.

				++di->filename_offset;	// Include the NULL terminator.
			}
			else
			{
				di->filename_offset = 1;
			}

			wchar_t *directory = NULL;

			if ( decode_converted_resource )
			{
				int val_length = WideCharToMultiByte( CP_UTF8, 0, resource, resource_length + 1, NULL, 0, NULL, NULL );
				char *utf8_val = ( char * )GlobalAlloc( GMEM_FIXED, sizeof( char ) * val_length ); // Size includes the null character.
				WideCharToMultiByte( CP_UTF8, 0, resource, resource_length + 1, utf8_val, val_length, NULL, NULL );

				unsigned int directory_length = 0;
				char *c_directory = url_decode_a( utf8_val, val_length - 1, &directory_length );
				GlobalFree( utf8_val );

				val_length = MultiByteToWideChar( CP_UTF8, 0, c_directory, directory_length + 1, NULL, 0 );	// Include the NULL terminator.
				directory = ( wchar_t * )GlobalAlloc( GMEM_FIXED, sizeof( wchar_t ) * val_length );
				MultiByteToWideChar( CP_UTF8, 0, c_directory, directory_length + 1, directory, val_length );

				GlobalFree( c_directory );	
			}
			else
			{
				directory = url_decode_w( resource, resource_length, NULL );
			}

			unsigned int w_filename_length = 0;

			// Try to create a filename from the resource path.
			if ( directory != NULL )
			{
				wchar_t *directory_ptr = directory;
				wchar_t *current_directory = directory;
				wchar_t *last_directory = NULL;

				// Iterate forward because '/' can be found after '#'.
				while ( *directory_ptr != NULL )
				{
					if ( *directory_ptr == L'?' || *directory_ptr == L'#' )
					{
						*directory_ptr = 0;	// Sanity.

						break;
					}
					else if ( *directory_ptr == L'/' )
					{
						last_directory = current_directory;
						current_directory = directory_ptr + 1; 
					}

					++directory_ptr;
				}

				if ( *current_directory == NULL )
				{
					// Adjust for '/'. current_directory will always be at least 1 greater than last_directory.
					if ( last_directory != NULL && ( current_directory - 1 ) - last_directory > 0 )
					{
						w_filename_length = ( unsigned int )( ( current_directory - 1 ) - last_directory );
						current_directory = last_directory;
					}
					else	// No filename could be made from the resource path. Use the host name instead.
					{
						w_filename_length = host_length;
						current_directory = host;
					}
				}
				else
				{
					w_filename_length = ( unsigned int )( directory_ptr - current_directory );
				}

				w_filename_length = min( w_filename_length, ( int )( MAX_PATH - di->filename_offset - 1 ) );

				_wmemcpy_s( di->file_path + di->filename_offset, MAX_PATH - di->filename_offset, current_directory, w_filename_length );
				di->file_path[ di->filename_offset + w_filename_length ] = 0;	// Sanity.

				EscapeFilename( di->file_path + di->filename_offset );

				GlobalFree( directory );
			}
			else	// Shouldn't happen.
			{
				w_filename_length = 11;
				_wmemcpy_s( di->file_path + di->filename_offset, MAX_PATH - di->filename_offset, L"NO_FILENAME\0", 12 );
			}

			di->file_extension_offset = di->filename_offset + get_file_extension_offset( di->file_path + di->filename_offset, w_filename_length );

			di->hFile = INVALID_HANDLE_VALUE;

			InitializeCriticalSection( &di->shared_cs );

			if ( current_url_encoded != NULL )
			{
				di->url = current_url_encoded;
				current_url_encoded = NULL;
			}
			else
			{
				//di->url = GlobalStrDupW( current_url );
				di->url = ( wchar_t * )GlobalAlloc( GMEM_FIXED, sizeof( wchar_t ) * ( current_url_length + 1 ) );
				_wmemcpy_s( di->url, current_url_length + 1, current_url, current_url_length );
				di->url[ current_url_length ] = 0;	// Sanity.
			}

			// Cache our file's icon.
			ICON_INFO *ii = CacheIcon( di, sfi );

			if ( ii != NULL )
			{
				di->icon = &ii->icon;
			}

			di->parts = ai->parts;

			di->download_speed_limit = ai->download_speed_limit;

			di->ssl_version = ai->ssl_version;

			di->download_operations = ai->download_operations;

			if ( ai->download_operations & DOWNLOAD_OPERATION_ADD_STOPPED )
			{
				di->status = STATUS_STOPPED;
				di->download_operations &= ~DOWNLOAD_OPERATION_ADD_STOPPED;
			}

			di->method = ai->method;

			if ( username == NULL && password == NULL )
			{
				LOGIN_INFO tli;
				tli.host = host;
				tli.protocol = protocol;
				tli.port = port;
				LOGIN_INFO *li = ( LOGIN_INFO * )dllrbt_find( g_login_info, ( void * )&tli, true );

				if ( li != NULL )
				{
					username_length = lstrlenA( li->username );
					if ( username_length > 0 )
					{
						di->auth_info.username = ( char * )GlobalAlloc( GMEM_FIXED, sizeof( char ) * ( username_length + 1 ) );
						_memcpy_s( di->auth_info.username, username_length + 1, li->username, username_length );
						di->auth_info.username[ username_length ] = 0;	// Sanity.
					}

					password_length = lstrlenA( li->password );
					if ( password_length > 0 )
					{
						di->auth_info.password = ( char * )GlobalAlloc( GMEM_FIXED, sizeof( char ) * ( password_length + 1 ) );
						_memcpy_s( di->auth_info.password, password_length + 1, li->password, password_length );
						di->auth_info.password[ password_length ] = 0;	// Sanity.
					}
				}
			}
			else
			{
				if ( username != NULL && username_length > 0 )
				{
					di->auth_info.username = ( char * )GlobalAlloc( GMEM_FIXED, sizeof( char ) * ( username_length + 1 ) );
					_memcpy_s( di->auth_info.username, username_length + 1, username, username_length );
					di->auth_info.username[ username_length ] = 0;	// Sanity.
				}

				if ( password != NULL && password_length > 0 )
				{
					di->auth_info.password = ( char * )GlobalAlloc( GMEM_FIXED, sizeof( char ) * ( password_length + 1 ) );
					_memcpy_s( di->auth_info.password, password_length + 1, password, password_length );
					di->auth_info.password[ password_length ] = 0;	// Sanity.
				}
			}

			if ( ai->utf8_cookies != NULL && cookies_length > 0 )
			{
				di->cookies = ( char * )GlobalAlloc( GMEM_FIXED, sizeof( char ) * ( cookies_length + 1 ) );
				_memcpy_s( di->cookies, cookies_length + 1, ai->utf8_cookies, cookies_length );
				di->cookies[ cookies_length ] = 0;	// Sanity.
			}

			if ( ai->utf8_headers != NULL && headers_length > 0 )
			{
				di->headers = ( char * )GlobalAlloc( GMEM_FIXED, sizeof( char ) * ( headers_length + 1 ) );
				_memcpy_s( di->headers, headers_length + 1, ai->utf8_headers, headers_length );
				di->headers[ headers_length ] = 0;	// Sanity.
			}

			if ( ai->utf8_data != NULL && data_length > 0 )
			{
				di->data = ( char * )GlobalAlloc( GMEM_FIXED, sizeof( char ) * ( data_length + 1 ) );
				_memcpy_s( di->data, data_length + 1, ai->utf8_data, data_length );
				di->data[ data_length ] = 0;	// Sanity.
			}

			SYSTEMTIME st;
			FILETIME ft;

			GetLocalTime( &st );
			SystemTimeToFileTime( &st, &ft );

			di->add_time.LowPart = ft.dwLowDateTime;
			di->add_time.HighPart = ft.dwHighDateTime;

			int buffer_length = 0;

			#ifndef NTDLL_USE_STATIC_LIB
				//buffer_length = 64;	// Should be enough to hold most translated values.
				buffer_length = __snwprintf( NULL, 0, L"%s, %s %d, %04d %d:%02d:%02d %s", GetDay( st.wDayOfWeek ), GetMonth( st.wMonth ), st.wDay, st.wYear, ( st.wHour > 12 ? st.wHour - 12 : ( st.wHour != 0 ? st.wHour : 12 ) ), st.wMinute, st.wSecond, ( st.wHour >= 12 ? L"PM" : L"AM" ) ) + 1;	// Include the NULL character.
			#else
				buffer_length = _scwprintf( L"%s, %s %d, %04d %d:%02d:%02d %s", GetDay( st.wDayOfWeek ), GetMonth( st.wMonth ), st.wDay, st.wYear, ( st.wHour > 12 ? st.wHour - 12 : ( st.wHour != 0 ? st.wHour : 12 ) ), st.wMinute, st.wSecond, ( st.wHour >= 12 ? L"PM" : L"AM" ) ) + 1;	// Include the NULL character.
			#endif

			di->w_add_time = ( wchar_t * )GlobalAlloc( GMEM_FIXED, sizeof( wchar_t ) * buffer_length );

			__snwprintf( di->w_add_time, buffer_length, L"%s, %s %d, %04d %d:%02d:%02d %s", GetDay( st.wDayOfWeek ), GetMonth( st.wMonth ), st.wDay, st.wYear, ( st.wHour > 12 ? st.wHour - 12 : ( st.wHour != 0 ? st.wHour : 12 ) ), st.wMinute, st.wSecond, ( st.wHour >= 12 ? L"PM" : L"AM" ) );

			EnterCriticalSection( &cleanup_cs );

			LVITEM lvi;
			_memzero( &lvi, sizeof( LVITEM ) );
			lvi.mask = LVIF_PARAM | LVIF_TEXT;
			lvi.iItem = ( int )_SendMessageW( g_hWnd_files, LVM_GETITEMCOUNT, 0, 0 );
			lvi.lParam = ( LPARAM )di;
			lvi.pszText = di->file_path + di->filename_offset;
			_SendMessageW( g_hWnd_files, LVM_INSERTITEM, 0, ( LPARAM )&lvi );

			if ( !( ai->download_operations & DOWNLOAD_OPERATION_ADD_STOPPED ) )
			{
				StartDownload( di, !( di->download_operations & DOWNLOAD_OPERATION_SIMULATE ) );
			}

			download_history_changed = true;

			LeaveCriticalSection( &cleanup_cs );
		}

		GlobalFree( current_url_encoded );
		GlobalFree( host );
		GlobalFree( resource );

		// If we got a username and password from the URL, then the username and password character strings were allocated and we need to free them.
		if ( url_username != NULL ) { GlobalFree( username ); GlobalFree( url_username ); }
		if ( url_password != NULL ) { GlobalFree( password ); GlobalFree( url_password ); }

		// Reset our username and password character strings.
		username = ai->auth_info.username;
		username_length = ai_username_length;

		password = ai->auth_info.password;
		password_length = ai_password_length;
	}

	// The tree is only used to determine duplicate filenames.
	DestroyFilenameTree( add_files_tree );

	GlobalFree( sfi );

	GlobalFree( ai->utf8_data );
	GlobalFree( ai->utf8_headers );
	GlobalFree( ai->utf8_cookies );
	GlobalFree( ai->auth_info.username );
	GlobalFree( ai->auth_info.password );
	GlobalFree( ai->download_directory );
	GlobalFree( ai->urls );
	GlobalFree( ai );

	if ( cfg_sort_added_and_updating_items &&
		 cfg_sorted_column_index != COLUMN_NUM )	// #
	{
		SORT_INFO si;
		si.column = GetColumnIndexFromVirtualIndex( cfg_sorted_column_index, download_columns, NUM_COLUMNS );
		si.hWnd = g_hWnd_files;
		si.direction = cfg_sorted_direction;

		_SendMessageW( g_hWnd_files, LVM_SORTITEMS, ( WPARAM )&si, ( LPARAM )( PFNLVCOMPARE )DMCompareFunc );
	}

	ProcessingList( false );

	// Release the semaphore if we're killing the thread.
	if ( worker_semaphore != NULL )
	{
		ReleaseSemaphore( worker_semaphore, 1, NULL );
	}

	in_worker_thread = false;

	LeaveCriticalSection( &worker_cs );

	_ExitThread( 0 );
	return 0;
}

void StartQueuedItem()
{
	EnterCriticalSection( &download_queue_cs );

	if ( download_queue != NULL )
	{
		DoublyLinkedList *download_queue_node = download_queue;

		DOWNLOAD_INFO *di = NULL;

		// Run through our download queue and start the first context that hasn't been paused or stopped.
		// Continue to dequeue if we haven't hit our maximum allowed active downloads.
		while ( download_queue_node != NULL )
		{
			di = ( DOWNLOAD_INFO * )download_queue_node->data;

			download_queue_node = download_queue_node->next;

			if ( di != NULL )
			{
				// Remove the item from the download queue.
				DLL_RemoveNode( &download_queue, &di->queue_node );
				di->queue_node.data = NULL;

				StartDownload( di, false );

				// Exit the loop if we've hit our maximum allowed active downloads.
				if ( total_downloading >= cfg_max_downloads )
				{
					break;
				}
			}
		}
	}

	LeaveCriticalSection( &download_queue_cs );
}


bool RetryTimedOut( SOCKET_CONTEXT *context )
{
	// Attempt to connect to a new address if we time out.
	if ( context != NULL &&
		 context->timed_out == TIME_OUT_RETRY && 
		 context->address_info != NULL &&
		 context->address_info->ai_next != NULL )
	{
		if ( context->socket != INVALID_SOCKET )
		{
			_shutdown( context->socket, SD_BOTH );
			_closesocket( context->socket );
			context->socket = INVALID_SOCKET;
		}

		if ( context->ssl != NULL )
		{
			SSL_free( context->ssl );
			context->ssl = NULL;
		}

		addrinfoW *old_address_info = context->address_info;
		context->address_info = context->address_info->ai_next;
		old_address_info->ai_next = NULL;

		_FreeAddrInfoW( old_address_info );

		// If we're going to restart the download, then we need to reset these values.
		context->header_info.chunk_length = 0;
		context->header_info.end_of_header = NULL;
		context->header_info.http_status = 0;
		context->header_info.connection = CONNECTION_NONE;
		context->header_info.content_encoding = CONTENT_ENCODING_NONE;
		context->header_info.chunked_transfer = false;
		//context->header_info.etag = false;
		context->header_info.got_chunk_start = false;
		context->header_info.got_chunk_terminator = false;

		if ( context->header_info.range_info != NULL )
		{
			context->header_info.range_info->content_length = 0;	// We must reset this to get the real request length (not the length of the 401/407 request).

			context->header_info.range_info->range_start += context->header_info.range_info->content_offset;	// Begin where we left off.
			context->header_info.range_info->content_offset = 0;	// Reset.
		}

		context->content_status = CONTENT_STATUS_NONE;

		context->timed_out = TIME_OUT_FALSE;

		context->status = STATUS_CONNECTING;

		context->cleanup = 0;	// Reset. Can only be set in CleanupConnection and if there's no more pending operations.

		// Connect to the remote server.
		if ( !CreateConnection( context, context->request_info.host, context->request_info.port ) )
		{
			context->status = STATUS_FAILED;
		}
		else
		{
			return true;
		}
	}

	return false;
}

DWORD CALLBACK MoveFileProgress( LARGE_INTEGER TotalFileSize, LARGE_INTEGER TotalBytesTransferred, LARGE_INTEGER StreamSize, LARGE_INTEGER StreamBytesTransferred, DWORD dwStreamNumber, DWORD dwCallbackReason, HANDLE hSourceFile, HANDLE hDestinationFile, LPVOID lpData )
{
	DOWNLOAD_INFO *di = ( DOWNLOAD_INFO * )lpData;

	if ( di != NULL )
	{
		if ( di->moving_state == 0 )
		{
			di->moving_state = 1;	// Move file.
		}

		di->last_downloaded = TotalBytesTransferred.QuadPart;

		if ( di->moving_state == 2 )
		{
			di->last_downloaded = TotalFileSize.QuadPart; // Reset.

			return PROGRESS_CANCEL;
		}
		else
		{
			return PROGRESS_CONTINUE;
		}
	}
	else
	{
		return PROGRESS_CANCEL;
	}
}

THREAD_RETURN ProcessMoveQueue( void *pArguments )
{
	DOWNLOAD_INFO *di = NULL;

	bool skip_processing = false;

	wchar_t prompt_message[ MAX_PATH + 512 ];
	wchar_t file_path[ MAX_PATH ];

	do
	{
		EnterCriticalSection( &move_file_queue_cs );

		DoublyLinkedList *move_file_queue_node = move_file_queue;

		if ( move_file_queue_node != NULL )
		{
			di = ( DOWNLOAD_INFO * )move_file_queue_node->data;

			DLL_RemoveNode( &move_file_queue, move_file_queue_node );
		}

		LeaveCriticalSection( &move_file_queue_cs );

		if ( di != NULL )
		{
			di->queue_node.data = NULL;

			GetTemporaryFilePath( di, file_path );

			di->file_path[ di->filename_offset - 1 ] = L'\\';	// Replace the download directory NULL terminator with a directory slash.

			di->status &= ~STATUS_QUEUED;

			DWORD move_type = MOVEFILE_COPY_ALLOWED;

			while ( true )
			{
				if ( MoveFileWithProgressW( file_path, di->file_path, MoveFileProgress, di, move_type ) == FALSE )
				{
					if ( GetLastError() == ERROR_FILE_EXISTS )
					{
						if ( cfg_prompt_rename == 0 && di->download_operations & DOWNLOAD_OPERATION_OVERRIDE_PROMPTS )
						{
							di->status = STATUS_SKIPPED;
						}
						else
						{
							// If the last return value was not set to remember our choice, then prompt again.
							if ( cfg_prompt_rename == 0 &&
								 g_rename_file_cmb_ret != CMBIDRENAMEALL &&
								 g_rename_file_cmb_ret != CMBIDOVERWRITEALL &&
								 g_rename_file_cmb_ret != CMBIDSKIPALL )
							{
								__snwprintf( prompt_message, MAX_PATH + 512, ST_V_PROMPT___already_exists, di->file_path );

								g_rename_file_cmb_ret = CMessageBoxW( g_hWnd_main, prompt_message, PROGRAM_CAPTION, CMB_ICONWARNING | CMB_RENAMEOVERWRITESKIPALL );
							}

							// Rename the file and try again.
							if ( cfg_prompt_rename == 1 ||
							   ( cfg_prompt_rename == 0 && ( g_rename_file_cmb_ret == CMBIDRENAME ||
															 g_rename_file_cmb_ret == CMBIDRENAMEALL ) ) )
							{
								// Creates a tree of active and queued downloads.
								dllrbt_tree *add_files_tree = CreateFilenameTree();

								bool rename_succeeded = RenameFile( di, add_files_tree, di->file_path, di->filename_offset, di->file_extension_offset );

								// The tree is only used to determine duplicate filenames.
								DestroyFilenameTree( add_files_tree );

								if ( !rename_succeeded )
								{
									if ( g_rename_file_cmb_ret2 != CMBIDOKALL && !( di->download_operations & DOWNLOAD_OPERATION_OVERRIDE_PROMPTS ) )
									{
										__snwprintf( prompt_message, MAX_PATH + 512, ST_V_PROMPT___could_not_be_renamed, file_path );

										g_rename_file_cmb_ret2 = CMessageBoxW( g_hWnd_main, prompt_message, PROGRAM_CAPTION, CMB_ICONWARNING | CMB_OKALL );
									}

									di->status = STATUS_SKIPPED;
								}
								else
								{
									continue;	// Try the move with our new filename.
								}
							}
							else if ( cfg_prompt_rename == 3 ||
									( cfg_prompt_rename == 0 && ( g_rename_file_cmb_ret == CMBIDFAIL ||
																  g_rename_file_cmb_ret == CMBIDSKIP ||
																  g_rename_file_cmb_ret == CMBIDSKIPALL ) ) ) // Skip the rename or overwrite if the return value fails, or the user selected skip.
							{
								di->status = STATUS_SKIPPED;
							}
							else	// Overwrite.
							{
								move_type |= MOVEFILE_REPLACE_EXISTING;

								continue;
							}
						}
					}
					else// if ( GetLastError() == ERROR_REQUEST_ABORTED )
					{
						di->status = STATUS_STOPPED;
					}
					/*else
					{
						di->status = STATUS_FILE_IO_ERROR;
					}*/
				}
				else
				{
					di->status = STATUS_COMPLETED;
				}

				break;
			}

			di->file_path[ di->filename_offset - 1 ] = 0;	// Restore.
		}

		EnterCriticalSection( &move_file_queue_cs );

		if ( move_file_queue == NULL )
		{
			skip_processing = true;

			move_file_process_active = false;
		}

		LeaveCriticalSection( &move_file_queue_cs );
	}
	while ( !skip_processing );

	EnterCriticalSection( &cleanup_cs );

	if ( total_downloading == 0 )
	{
		EnableTimers( false );
	}

	LeaveCriticalSection( &cleanup_cs );

	_ExitThread( 0 );
	return 0;
}

void AddToMoveFileQueue( DOWNLOAD_INFO *di )
{
	if ( di != NULL )
	{
		// Add item to move file queue and continue.
		EnterCriticalSection( &move_file_queue_cs );

		di->queue_node.data = di;
		DLL_AddNode( &move_file_queue, &di->queue_node, -1 );

		di->status = STATUS_MOVING_FILE | STATUS_QUEUED;

		if ( !move_file_process_active )
		{
			move_file_process_active = true;

			HANDLE handle_prompt = ( HANDLE )_CreateThread( NULL, 0, ProcessMoveQueue, NULL, 0, NULL );

			// Make sure our thread spawned.
			if ( handle_prompt == NULL )
			{
				DLL_RemoveNode( &move_file_queue, &di->queue_node );
				di->queue_node.data = NULL;

				move_file_process_active = false;

				di->status = STATUS_STOPPED;
			}
			else
			{
				CloseHandle( handle_prompt );
			}
		}

		LeaveCriticalSection( &move_file_queue_cs );
	}
}

bool CleanupFTPContexts( SOCKET_CONTEXT *context )
{
	bool skip_cleanup = false;

	if ( context != NULL )
	{
		EnterCriticalSection( &cleanup_cs );

		// This forces the FTP control context to cleanup everything.
		if ( context->download_info != NULL )
		{
			DOWNLOAD_INFO *di = context->download_info;

			EnterCriticalSection( &di->shared_cs );

			EnterCriticalSection( &context->context_cs );

			// We want the control port to handle everything.
			if ( context->ftp_context != NULL )
			{
				if ( context->ftp_connection_type & ( FTP_CONNECTION_TYPE_CONTROL | FTP_CONNECTION_TYPE_CONTROL_SUCCESS ) )	// Control context.
				{
					context->cleanup = 0;	// Reset.

					// Force the listen context to complete if it's still waiting.
					// This will have been set to INVALID_SOCKET if the AcceptEx completed.
					if ( context->ftp_connection_type == FTP_CONNECTION_TYPE_CONTROL &&
						 context->listen_socket != INVALID_SOCKET )
					{
						_shutdown( context->listen_socket, SD_BOTH );
						_closesocket( context->listen_socket );
						context->listen_socket = INVALID_SOCKET;
					}

					context->ftp_connection_type = ( FTP_CONNECTION_TYPE_CONTROL | FTP_CONNECTION_TYPE_CONTROL_WAIT );	// Wait for Data to finish.

					skip_cleanup = true;
				}
				else	// Data context.
				{
					if ( context->timed_out != TIME_OUT_FALSE )
					{
						// Force the Control connection to time out.
						// Make sure it's larger than 300 and less than LONG_MAX.
						InterlockedExchange( &context->ftp_context->timeout, SHRT_MAX );
					}

					if ( context->ftp_context->ftp_connection_type & FTP_CONNECTION_TYPE_CONTROL_WAIT )	// Control is waiting.
					{
						InterlockedIncrement( &context->ftp_context->pending_operations );

						context->ftp_context->overlapped_close.current_operation = IO_Close;	// No need to shutdown.

						PostQueuedCompletionStatus( g_hIOCP, 0, ( ULONG_PTR )context->ftp_context, ( OVERLAPPED * )&context->ftp_context->overlapped_close );
					}

					context->ftp_context->ftp_context = NULL;

					DLL_RemoveNode( &context->download_info->parts_list, &context->parts_node );

					context->download_info = NULL;
				}
			}

			LeaveCriticalSection( &context->context_cs );

			LeaveCriticalSection( &di->shared_cs );
		}

		LeaveCriticalSection( &cleanup_cs );
	}

	return skip_cleanup;
}

bool SetCleanupState( SOCKET_CONTEXT *context )
{
	bool skip_cleanup = false;

	if ( context != NULL )
	{
		EnterCriticalSection( &context->context_cs );

		// If we've forced the cleanup, then skip everything below and wait for the pending operation to enter CleanupConnection to clean things up.
		if ( context->cleanup == 2 )
		{
			context->cleanup = 1;	// All pending operations will shutdown/close and enter CleanupConnection to clean things up.

			skip_cleanup = true;
		}
		else if ( context->cleanup == 12 )	// We forced the cleanup and are waiting for IO_Write to complete.
		{
			context->cleanup = 10;	// Let IO_Write do its thing.

			skip_cleanup = true;
		}
		else if ( context->cleanup == 10 )
		{
			skip_cleanup = true;
		}
		else	// 0 or 1
		{
			context->cleanup = 1;	// All pending operations will shutdown/close and enter CleanupConnection to clean things up.
		}

		LeaveCriticalSection( &context->context_cs );
	}

	return skip_cleanup;
}

void CleanupConnection( SOCKET_CONTEXT *context )
{
	if ( context != NULL )
	{
		// Returns true if there's pending operations that need to complete first.
		if ( SetCleanupState( context ) )
		{
			return;
		}

		// Returns true if we need to wait for the Data context to complete.
		if ( CleanupFTPContexts( context ) )
		{
			return;
		}

		// Check if our context timed out and if it has any additional addresses to connect to.
		// If it does, then reuse the context and connect to the new address.
		if ( RetryTimedOut( context ) )
		{
			return;
		}

		bool retry_context_connection = false;

		// This critical section must encompass the (context->download_info != NULL) section below so that any listview manipulation (like remove_items(...))
		// doesn't affect the queuing/starting proceedure.
		EnterCriticalSection( &cleanup_cs );

		EnterCriticalSection( &context_list_cs );

		// Remove from the global download list.
		DLL_RemoveNode( &g_context_list, &context->context_node );

		LeaveCriticalSection( &context_list_cs );

		// Remove from the parts list.
		if ( !g_end_program )
		{
			if ( context->download_info != NULL )
			{
				bool incomplete_part = false;

				if ( context->header_info.range_info != NULL &&  
				   ( context->header_info.range_info->content_offset < ( ( context->header_info.range_info->range_end - context->header_info.range_info->range_start ) + 1 ) ) )
				{
					incomplete_part = true;
				}

				// Connecting, Downloading, Paused.
				if ( incomplete_part &&
					 context->retries < cfg_retry_parts_count &&
				   ( IS_STATUS( context->status,
						STATUS_CONNECTING |
						STATUS_DOWNLOADING ) ) )
				{
					++context->retries;

					if ( context->socket != INVALID_SOCKET )
					{
						_shutdown( context->socket, SD_BOTH );
						_closesocket( context->socket );
						context->socket = INVALID_SOCKET;
					}

					if ( context->ssl != NULL )
					{
						SSL_free( context->ssl );
						context->ssl = NULL;
					}

					EnterCriticalSection( &context_list_cs );

					DLL_AddNode( &g_context_list, &context->context_node, 0 );

					LeaveCriticalSection( &context_list_cs );

					// If we're going to restart the download, then we need to reset these values.
					context->header_info.chunk_length = 0;
					context->header_info.end_of_header = NULL;
					context->header_info.http_status = 0;
					context->header_info.connection = CONNECTION_NONE;
					context->header_info.content_encoding = CONTENT_ENCODING_NONE;
					context->header_info.chunked_transfer = false;
					//context->header_info.etag = false;
					context->header_info.got_chunk_start = false;
					context->header_info.got_chunk_terminator = false;

					if ( context->header_info.range_info != NULL )
					{
						context->header_info.range_info->content_length = 0;	// We must reset this to get the real request length (not the length of the 401/407 request).

						context->header_info.range_info->range_start += context->header_info.range_info->content_offset;	// Begin where we left off.
						context->header_info.range_info->content_offset = 0;	// Reset.
					}

					context->content_status = CONTENT_STATUS_NONE;

					// Remember if we timed out in case we failed to connect.
					unsigned char timed_out = context->timed_out;

					context->timed_out = TIME_OUT_FALSE;

					context->status = STATUS_CONNECTING;

					context->cleanup = 0;	// Reset. Can only be set in CleanupConnection and if there's no more pending operations.

					// Connect to the remote server.
					if ( !CreateConnection( context, context->request_info.host, context->request_info.port ) )
					{
						context->status = STATUS_FAILED;

						context->timed_out = timed_out;

						EnterCriticalSection( &context_list_cs );

						DLL_RemoveNode( &g_context_list, &context->context_node );

						LeaveCriticalSection( &context_list_cs );
					}
					else
					{
						retry_context_connection = true;
					}
				}

				if ( !retry_context_connection )
				{
					EnterCriticalSection( &download_queue_cs );

					// If the context we're cleaning up is in the download queue.
					if ( context->download_info->queue_node.data != NULL )
					{
						DLL_RemoveNode( &download_queue, &context->download_info->queue_node );
						context->download_info->queue_node.data = NULL;
					}

					LeaveCriticalSection( &download_queue_cs );

					EnterCriticalSection( &context->download_info->shared_cs );

					DLL_RemoveNode( &context->download_info->parts_list, &context->parts_node );

					if ( context->download_info->active_parts > 0 )
					{
						// If incomplete_part is tested below and is true and the new range fails, then the download will stop.
						// If incomplete_part is not tested, then all queued ranges will be tried until they either all succeed or all fail.
						if ( /*!incomplete_part &&*/
							 IS_STATUS_NOT( context->status,
								STATUS_STOPPED |
								STATUS_REMOVE |
								STATUS_RESTART |
								STATUS_UPDATING ) &&
							 context->download_info->range_queue != NULL &&
							 context->download_info->range_queue != context->download_info->range_list_end )
						{
							// Add back to the parts list.
							DLL_AddNode( &context->download_info->parts_list, &context->parts_node, -1 );

							DoublyLinkedList *range_queue_node = context->download_info->range_queue;
							context->download_info->range_queue = context->download_info->range_queue->next;

							context->retries = 0;

							if ( context->socket != INVALID_SOCKET )
							{
								_shutdown( context->socket, SD_BOTH );
								_closesocket( context->socket );
								context->socket = INVALID_SOCKET;
							}

							if ( context->ssl != NULL )
							{
								SSL_free( context->ssl );
								context->ssl = NULL;
							}

							EnterCriticalSection( &context_list_cs );

							DLL_AddNode( &g_context_list, &context->context_node, 0 );

							LeaveCriticalSection( &context_list_cs );

							// If we're going to restart the download, then we need to reset these values.
							context->header_info.chunk_length = 0;
							context->header_info.end_of_header = NULL;
							context->header_info.http_status = 0;
							context->header_info.connection = CONNECTION_NONE;
							context->header_info.content_encoding = CONTENT_ENCODING_NONE;
							context->header_info.chunked_transfer = false;
							//context->header_info.etag = false;
							context->header_info.got_chunk_start = false;
							context->header_info.got_chunk_terminator = false;

							context->header_info.range_info = ( RANGE_INFO * )range_queue_node->data;

							if ( context->header_info.range_info != NULL )
							{
								context->header_info.range_info->content_length = 0;	// We must reset this to get the real request length (not the length of the 401/407 request).

								context->header_info.range_info->range_start += context->header_info.range_info->content_offset;	// Begin where we left off.
								context->header_info.range_info->content_offset = 0;	// Reset.
							}

							context->content_status = CONTENT_STATUS_NONE;

							context->timed_out = TIME_OUT_FALSE;

							context->status = STATUS_CONNECTING;

							context->cleanup = 0;	// Reset. Can only be set in CleanupConnection and if there's no more pending operations.

							// Connect to the remote server.
							if ( !CreateConnection( context, context->request_info.host, context->request_info.port ) )
							{
								context->status = STATUS_FAILED;

								EnterCriticalSection( &context_list_cs );

								DLL_RemoveNode( &g_context_list, &context->context_node );

								LeaveCriticalSection( &context_list_cs );

								--context->download_info->active_parts;

								retry_context_connection = false;
							}
							else
							{
								retry_context_connection = true;
							}
						}
						else
						{
							--context->download_info->active_parts;
						}

						// There are no more active connections.
						if ( context->download_info->active_parts == 0 )
						{
							bool incomplete_download = false;

							// Go through our range list and see if any connections have not fully completed.
							DoublyLinkedList *range_node = context->download_info->range_list;
							while ( range_node != context->download_info->range_list_end )
							{
								RANGE_INFO *range_info = ( RANGE_INFO * )range_node->data;
								if ( range_info->content_offset < ( ( range_info->range_end - range_info->range_start ) + 1 ) )
								{
									incomplete_download = true;

									break;
								}
								
								range_node = range_node->next;
							}

							if ( incomplete_download )
							{
								// Connecting, Downloading, Paused.
								if ( IS_STATUS( context->status,
										STATUS_CONNECTING |
										STATUS_DOWNLOADING ) )
								{
									// If any of our connections timed out (after we have no more active connections), then set our status to timed out.
									if ( context->timed_out != TIME_OUT_FALSE )
									{
										context->download_info->status = STATUS_TIMED_OUT;
									}
									else
									{
										context->download_info->status = STATUS_STOPPED;
									}
								}
								else
								{
									incomplete_download = false;

									context->download_info->status = context->status;
								}
							}
							else
							{
								context->download_info->status = STATUS_COMPLETED;
							}

							EnterCriticalSection( &active_download_list_cs );

							// Remove the node from the active download list.
							DLL_RemoveNode( &active_download_list, &context->download_info->download_node );
							context->download_info->download_node.data = NULL;

							context->download_info->last_downloaded = context->download_info->downloaded;

							--total_downloading;

							LeaveCriticalSection( &active_download_list_cs );

							context->download_info->time_remaining = 0;
							context->download_info->speed = 0;

							if ( context->download_info->hFile != INVALID_HANDLE_VALUE )
							{
								CloseHandle( context->download_info->hFile );
								context->download_info->hFile = INVALID_HANDLE_VALUE;
							}

							FILETIME ft;
							GetSystemTimeAsFileTime( &ft );
							ULARGE_INTEGER current_time;
							current_time.HighPart = ft.dwHighDateTime;
							current_time.LowPart = ft.dwLowDateTime;

							context->download_info->time_elapsed = ( current_time.QuadPart - context->download_info->start_time.QuadPart ) / FILETIME_TICKS_PER_SECOND;

							// Stop and Remove.
							if ( IS_STATUS( context->status, STATUS_REMOVE ) )
							{
								// Find the icon info
								EnterCriticalSection( &icon_cache_cs );
								dllrbt_iterator *itr = dllrbt_find( g_icon_handles, ( void * )( context->download_info->file_path + context->download_info->file_extension_offset ), false );
								// Free its values and remove it from the tree if there are no other items using it.
								if ( itr != NULL )
								{
									ICON_INFO *ii = ( ICON_INFO * )( ( node_type * )itr )->val;

									if ( ii != NULL )
									{
										if ( --ii->count == 0 )
										{
											DestroyIcon( ii->icon );
											GlobalFree( ii->file_extension );
											GlobalFree( ii );

											dllrbt_remove( g_icon_handles, itr );
										}
									}
									else
									{
										dllrbt_remove( g_icon_handles, itr );
									}
								}
								LeaveCriticalSection( &icon_cache_cs );

								GlobalFree( context->download_info->url );
								GlobalFree( context->download_info->w_add_time );
								GlobalFree( context->download_info->cookies );
								GlobalFree( context->download_info->headers );
								GlobalFree( context->download_info->data );
								//GlobalFree( context->download_info->etag );
								GlobalFree( context->download_info->auth_info.username );
								GlobalFree( context->download_info->auth_info.password );

								// Safe to free this here since the listview item will have been removed.
								while ( context->download_info->range_list != NULL )
								{
									DoublyLinkedList *range_node = context->download_info->range_list;
									context->download_info->range_list = context->download_info->range_list->next;

									GlobalFree( range_node->data );
									GlobalFree( range_node );
								}

								// Do we want to delete the file as well?
								if ( !( context->download_info->download_operations & DOWNLOAD_OPERATION_SIMULATE ) &&
									IS_STATUS( context->status, STATUS_DELETE ) )
								{
									wchar_t *file_path_delete;

									wchar_t file_path[ MAX_PATH ];
									if ( cfg_use_temp_download_directory )
									{
										GetTemporaryFilePath( context->download_info, file_path );

										file_path_delete = file_path;
									}
									else
									{
										// We're freeing this anyway so it's safe to modify.
										context->download_info->file_path[ context->download_info->filename_offset - 1 ] = L'\\';	// Replace the download directory NULL terminator with a directory slash.

										file_path_delete = context->download_info->file_path;
									}

									DeleteFileW( file_path_delete );
								}

								DeleteCriticalSection( &context->download_info->shared_cs );

								GlobalFree( context->download_info );
								context->download_info = NULL;
							}
							else if ( IS_STATUS( context->status, STATUS_RESTART ) )
							{
								// Safe to free this here since the print_range_list will have been reset.
								while ( context->download_info->range_list != NULL )
								{
									DoublyLinkedList *range_node = context->download_info->range_list;
									context->download_info->range_list = context->download_info->range_list->next;

									GlobalFree( range_node->data );
									GlobalFree( range_node );
								}

								context->download_info->processed_header = false;

								context->download_info->downloaded = 0;

								context->download_info->last_modified.QuadPart = 0;

								// If we restart a download, then set the incomplete retry attempts back to 0.
								context->download_info->retries = 0;
								context->download_info->start_time.QuadPart = 0;

								StartDownload( context->download_info, false );

								LeaveCriticalSection( &context->download_info->shared_cs );
							}
							else
							{
								if ( incomplete_download )
								{
									if ( context->download_info->retries < cfg_retry_downloads_count )
									{
										++context->download_info->retries;

										StartDownload( context->download_info, false );
									}
									else
									{
										SetSessionStatusCount( context->download_info->status );
									}
								}
								else if ( context->download_info->status == STATUS_UPDATING )
								{
									StartDownload( context->download_info, false );
								}
								else if ( context->download_info->status == STATUS_COMPLETED )
								{
									SetSessionStatusCount( context->download_info->status );

									if ( cfg_use_temp_download_directory &&
									  !( context->download_info->download_operations & DOWNLOAD_OPERATION_SIMULATE ) )
									{
										AddToMoveFileQueue( context->download_info );
									}
								}
								else
								{
									SetSessionStatusCount( context->download_info->status );
								}

								LeaveCriticalSection( &context->download_info->shared_cs );
							}

							// Start any items that are in our download queue.
							if ( total_downloading < cfg_max_downloads )
							{
								StartQueuedItem();
							}

							// Turn off our timers if we're not currently downloading, or moving anything.
							if ( total_downloading == 0 )
							{
								EnterCriticalSection( &move_file_queue_cs );

								if ( !move_file_process_active )
								{
									EnableTimers( false );
								}

								LeaveCriticalSection( &move_file_queue_cs );
							}
						}
						else
						{
							LeaveCriticalSection( &context->download_info->shared_cs );
						}
					}
					else
					{
						LeaveCriticalSection( &context->download_info->shared_cs );
					}
				}
			}
		}

		if ( !retry_context_connection )
		{
			if ( context->socket != INVALID_SOCKET )
			{
				_shutdown( context->socket, SD_BOTH );
				_closesocket( context->socket );
				context->socket = INVALID_SOCKET;
			}

			if ( context->listen_socket != INVALID_SOCKET )
			{
				_shutdown( context->listen_socket, SD_BOTH );
				_closesocket( context->listen_socket );
				context->listen_socket = INVALID_SOCKET;
			}

			if ( context->ssl != NULL ) { SSL_free( context->ssl ); }

			if ( context->address_info != NULL ) { _FreeAddrInfoW( context->address_info ); }
			if ( context->proxy_address_info != NULL ) { _FreeAddrInfoW( context->proxy_address_info ); }

			if ( context->decompressed_buf != NULL ) { GlobalFree( context->decompressed_buf ); }
			if ( zlib1_state == ZLIB1_STATE_RUNNING ) { _inflateEnd( &context->stream ); }

			FreePOSTInfo( &context->post_info );

			FreeAuthInfo( &context->header_info.digest_info );
			FreeAuthInfo( &context->header_info.proxy_digest_info );

			if ( context->header_info.url_location.host != NULL ) { GlobalFree( context->header_info.url_location.host ); }
			if ( context->header_info.url_location.resource != NULL ) { GlobalFree( context->header_info.url_location.resource ); }
			if ( context->header_info.url_location.auth_info.username != NULL ) { GlobalFree( context->header_info.url_location.auth_info.username ); }
			if ( context->header_info.url_location.auth_info.password != NULL ) { GlobalFree( context->header_info.url_location.auth_info.password ); }

			if ( context->header_info.chunk_buffer != NULL ) { GlobalFree( context->header_info.chunk_buffer ); }

			if ( context->header_info.cookies != NULL ) { GlobalFree( context->header_info.cookies ); }
			if ( context->header_info.cookie_tree != NULL )
			{
				node_type *node = dllrbt_get_head( context->header_info.cookie_tree );
				while ( node != NULL )
				{
					COOKIE_CONTAINER *cc = ( COOKIE_CONTAINER * )node->val;
					if ( cc != NULL )
					{
						GlobalFree( cc->cookie_name );
						GlobalFree( cc->cookie_value );
						GlobalFree( cc );
					}

					node = node->next;
				}

				dllrbt_delete_recursively( context->header_info.cookie_tree );
			}

			if ( context->request_info.host != NULL ) { GlobalFree( context->request_info.host ); }
			if ( context->request_info.resource != NULL ) { GlobalFree( context->request_info.resource ); }

			if ( context->request_info.auth_info.username != NULL ) { GlobalFree( context->request_info.auth_info.username ); }
			if ( context->request_info.auth_info.password != NULL ) { GlobalFree( context->request_info.auth_info.password ); }

			// context->download_info is freed in WM_DESTROY.

			DeleteCriticalSection( &context->context_cs );

			if ( context->buffer != NULL ){ GlobalFree( context->buffer ); }

			GlobalFree( context );
		}

		LeaveCriticalSection( &cleanup_cs );
	}
}

void FreePOSTInfo( POST_INFO **post_info )
{
	if ( *post_info != NULL )
	{
		if ( ( *post_info )->method != NULL ) { GlobalFree( ( *post_info )->method ); }
		if ( ( *post_info )->urls != NULL ) { GlobalFree( ( *post_info )->urls ); }
		if ( ( *post_info )->username != NULL ) { GlobalFree( ( *post_info )->username ); }
		if ( ( *post_info )->password != NULL ) { GlobalFree( ( *post_info )->password ); }
		if ( ( *post_info )->cookies != NULL ) { GlobalFree( ( *post_info )->cookies ); }
		if ( ( *post_info )->headers != NULL ) { GlobalFree( ( *post_info )->headers ); }
		if ( ( *post_info )->data != NULL ) { GlobalFree( ( *post_info )->data ); }
		if ( ( *post_info )->parts != NULL ) { GlobalFree( ( *post_info )->parts ); }
		if ( ( *post_info )->download_speed_limit != NULL ) { GlobalFree( ( *post_info )->download_speed_limit ); }
		if ( ( *post_info )->directory != NULL ) { GlobalFree( ( *post_info )->directory ); }
		if ( ( *post_info )->download_operations != NULL ) { GlobalFree( ( *post_info )->download_operations ); }

		GlobalFree( *post_info );

		*post_info = NULL;
	}
}

void FreeAuthInfo( AUTH_INFO **auth_info )
{
	if ( *auth_info != NULL )
	{
		if ( ( *auth_info )->domain != NULL ) { GlobalFree( ( *auth_info )->domain ); }
		if ( ( *auth_info )->nonce != NULL ) { GlobalFree( ( *auth_info )->nonce ); }
		if ( ( *auth_info )->opaque != NULL ) { GlobalFree( ( *auth_info )->opaque ); }
		if ( ( *auth_info )->qop != NULL ) { GlobalFree( ( *auth_info )->qop ); }
		if ( ( *auth_info )->realm != NULL ) { GlobalFree( ( *auth_info )->realm ); }

		if ( ( *auth_info )->cnonce != NULL ) { GlobalFree( ( *auth_info )->cnonce ); }
		if ( ( *auth_info )->uri != NULL ) { GlobalFree( ( *auth_info )->uri ); }
		if ( ( *auth_info )->response != NULL ) { GlobalFree( ( *auth_info )->response ); }
		if ( ( *auth_info )->username != NULL ) { GlobalFree( ( *auth_info )->username ); }

		GlobalFree( *auth_info );

		*auth_info = NULL;
	}
}

// Free all context structures in the global list of context structures.
void FreeContexts()
{
	DoublyLinkedList *context_node = g_context_list;
	DoublyLinkedList *del_context_node = NULL;

	while ( context_node != NULL )
	{
		del_context_node = context_node;
		context_node = context_node->next;

		CleanupConnection( ( SOCKET_CONTEXT * )del_context_node->data );
	}

	g_context_list = NULL;
}

void FreeListenContext()
{
	if ( g_listen_context != NULL )
	{
		CleanupConnection( g_listen_context );
		g_listen_context = NULL;
	}
}
