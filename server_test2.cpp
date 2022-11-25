#include <sys/socket.h>
#include <sys/event.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <stdlib.h>
#include <errno.h>
#include <unistd.h>
#include <fcntl.h>
#include <iostream>
#include <string>
#include <vector>
#include <map>

#define SERV_PORT 1234
#define SERV_SOCK_BACKLOG 10

void error_exit( std::string err, int ( *func )( int ), int fd ) {
	std::cerr << strerror( errno ) << std::endl;
	if ( func != NULL ) {
		func( fd );
	}
	exit( EXIT_FAILURE );
}

void add_event_change_list( std::vector<struct kevent> &changelist,
							uintptr_t ident, int64_t filter, uint16_t flags,
							uint32_t fflags, intptr_t data, void *udata ) {
	struct kevent tmp_event;
	EV_SET( &tmp_event, ident, filter, flags, fflags, data, udata );
	changelist.push_back( tmp_event );
}

void disconnect_client( int                               client_fd,
						std::map<uintptr_t, std::string> &clients ) {
	std::cout << "client disconnected: " << client_fd << std::endl;
	close( client_fd );
	clients.erase( client_fd );
}

int main( void ) {
	int                serv_sd;
	struct sockaddr_in serv_addr;

	serv_sd = socket( PF_INET, SOCK_STREAM, IPPROTO_TCP );
	if ( serv_sd == -1 ) {
		error_exit( "socket()", NULL, 0 );
	}

	int opt = 1;
	if ( setsockopt( serv_sd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof( opt ) ) ==
		 -1 ) {
		error_exit( "setsockopt()", close, serv_sd );
	}

	if ( fcntl( serv_sd, F_SETFL, O_NONBLOCK ) == -1 ) {
		error_exit( "fcntl()", close, serv_sd );
	}

	memset( &serv_addr, 0, sizeof( serv_addr ) );
	serv_addr.sin_family = AF_INET;
	serv_addr.sin_port = htons( SERV_PORT );
	serv_addr.sin_addr.s_addr = htonl( INADDR_ANY );

	if ( bind( serv_sd, (struct sockaddr *)&serv_addr, sizeof( serv_addr ) ) ==
		 -1 ) {
		error_exit( "bind", close, serv_sd );
	}

	if ( listen( serv_sd, SERV_SOCK_BACKLOG ) == -1 ) {
		error_exit( "bind", close, serv_sd );
	}

	std::map<uintptr_t, std::string> clients;
	std::vector<struct kevent>       changelist;
	struct kevent                    eventlist[8];
	int                              kq;

	kq = kqueue();
	if ( kq == -1 ) {
		error_exit( "kqueue()", close, serv_sd );
	}

	add_event_change_list( changelist, serv_sd, EVFILT_READ, EV_ADD | EV_ENABLE,
						   0, 0, NULL );

	int            event_len;
	struct kevent *cur_evnet;
	while ( true ) {
		event_len = kevent( kq, changelist.begin().base(), changelist.size(),
							eventlist, 8, NULL );

		if ( event_len == -1 ) {
			error_exit( "kevent()", close, serv_sd );
		}

		changelist.clear();

		for ( int i = 0; i < event_len; ++i ) {
			cur_evnet = &eventlist[i];

			if ( cur_evnet->flags & EV_ERROR ) {
				if ( cur_evnet->ident == serv_sd ) {
					error_exit( "server socket error", close, serv_sd );
				} else {
					std::cerr << "client socket error" << std::endl;
					disconnect_client( cur_evnet->ident, clients );
				}
			} else if ( cur_evnet->filter == EVFILT_READ ) {
				if ( cur_evnet->ident == serv_sd ) {
					uintptr_t client_fd;
					if ( ( client_fd = accept( serv_sd, NULL, NULL ) ) == -1 ) {
						std::cerr << strerror( errno ) << std::endl;
					} else {
						std::cout << "accept new client: " << client_fd
								  << std::endl;
						fcntl( client_fd, F_SETFL, O_NONBLOCK );
						add_event_change_list( changelist, client_fd,
											   EVFILT_READ, EV_ADD | EV_ENABLE,
											   0, 0, NULL );
						add_event_change_list( changelist, client_fd,
											   EVFILT_WRITE, EV_ADD | EV_ENABLE,
											   0, 0, NULL );
						clients[client_fd] = "";
					};
				} else {
					char buf[1025];
					int  n = read( cur_evnet->ident, buf, sizeof( buf ) - 1 );

					buf[n] = '\n';
					clients[cur_evnet->ident] += buf;
					std::cout << "received data from " << cur_evnet->ident
							  << std::endl;
				}
			} else if ( cur_evnet->filter == EVFILT_WRITE ) {
				std::map<uintptr_t, std::string>::iterator iter =
					clients.find( cur_evnet->ident );
				// if ( iter != clients.end() ) {
				// 	if ( clients[cur_evnet->ident] != "" ) {
				// 		int n;
				// 	}
				// }
				write( cur_evnet->ident, "ok", 2 );
				sleep( 1 );
			}
		}
	}
	return 0;
}
