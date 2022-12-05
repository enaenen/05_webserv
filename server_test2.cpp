#include <sys/socket.h>
#include <sys/event.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <sys/uio.h>
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
#define SERV_RESPONSE                                                    \
	"HTTP/1.1 200 KO\r\nContent-Type: text/plain\r\nTransfer-Encoding: " \
	"chunked\r\n\r\n4\r\nghan\r\n6\r\njiskim\r\n8\r\nyongjule\r\n0\r\n\r\n"
#define READ_BUF_SIZE 8 * 1024

#define SOCK_READ 1
#define HEAD_END_FLAG 2
#define SOCK_WRITE 4
// #define HEADER_PARED 16

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

void disconnect_client( int                                 client_fd,
						std::map<uintptr_t, t_header_info> &clients ) {
	std::cout << "client disconnected: " << client_fd << std::endl;
	close( client_fd );
	clients.erase( client_fd );
}

typedef struct headerInfo {
	char                               rbuf[READ_BUF_SIZE];
	std::string                        rsaved;
	int                                rchecked;
	char                               wbuf[READ_BUF_SIZE];
	std::string                        wsaved;
	int                                wchecked;
	timespec                           timeout;
	int                                flag;
	std::map<std::string, std::string> field;

	headerInfo()
		: rbuf(),
		  rsaved(),
		  rchecked( 0 ),
		  wbuf(),
		  wsaved(),
		  wchecked( 0 ),
		  timeout(),
		  flag( 0 ),
		  field() {
	}
	~headerInfo() {
	}
} t_header_info;

std::string getNextCRLF( uintptr_t fd, t_header_info &buf ) {
	size_t pos;

	if ( buf.rsaved.size() ) {
		pos = buf.rsaved.find( "\r\n" );
		if ( pos != std::string::npos ) {
			std::string tmp = buf.rsaved.substr( buf.rchecked, pos );
			buf.rchecked = pos + 2;
			if ( tmp.size() == 0 ) {
				buf.flag |= HEAD_END_FLAG;
			}
			return tmp;
		}
		buf.flag |= SOCK_READ;
	}
	return "";
}

bool read_client_fd( uintptr_t fd, t_header_info &buf ) {
	char    strbuf[8196];
	ssize_t n;

	if ( buf.flag & SOCK_READ ) {
		n = read( fd, strbuf, 1024 );
		if ( n <= 0 ) {
			// client error??
			return false;
		}
		buf.rsaved.append( strbuf, buf.rsaved.size(), n );
		return true;
	}
	return true;
}

bool write_client_fd( uintptr_t fd, t_header_info &buf ) {
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

	// if ( fcntl( serv_sd, F_SETFL, O_NONBLOCK ) == -1 ) {
	// 	error_exit( "fcntl()", close, serv_sd );
	// }

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

	std::map<uintptr_t, t_header_info> clients;
	std::vector<struct kevent>         changelist;
	struct kevent                      eventlist[8];
	int                                kq;

	kq = kqueue();
	if ( kq == -1 ) {
		error_exit( "kqueue()", close, serv_sd );
	}

	add_event_change_list( changelist, serv_sd, EVFILT_READ, EV_ADD | EV_ENABLE,
						   0, 0, NULL );

	int            event_len;
	struct kevent *cur_event;
	int            l = 0;
	while ( true ) {
		event_len = kevent( kq, changelist.begin().base(), changelist.size(),
							eventlist, 8, NULL );
		if ( event_len == -1 ) {
			error_exit( "kevent()", close, serv_sd );
		}
		changelist.clear();

		// std::cout << "current loop: " << l++ << std::endl;

		for ( int i = 0; i < event_len; ++i ) {
			cur_event = &eventlist[i];

			if ( cur_event->flags & EV_ERROR ) {
				if ( cur_event->ident == serv_sd ) {
					error_exit( "server socket error", close, serv_sd );
				} else {
					std::cerr << "client socket error" << std::endl;
					disconnect_client( cur_event->ident, clients );
				}
			} else if ( cur_event->filter == EVFILT_READ ) {
				if ( cur_event->ident == serv_sd ) {
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
						add_event_change_list(
							changelist, client_fd, EVFILT_WRITE,
							EV_ADD | EV_DISABLE, 0, 0, NULL );
						clients[cur_event->ident];
					};
				} else {
					std::cout << "recieved data from " << cur_event->ident
							  << ":" << std::endl;

					std::string new_line;
					do {
						new_line = getNextCRLF( cur_event->ident );
						std::cout << new_line << std::endl;
					} while ( new_line != "" );

					add_event_change_list( changelist, cur_event->ident,
										   EVFILT_WRITE, EV_ADD | EV_ENABLE, 0,
										   0, NULL );
				}
			} else if ( cur_event->filter == EVFILT_WRITE ) {
				std::cout << "sending response" << std::endl;

				if ( clients[cur_event->ident].flag & SOCK_WRITE ) {
					int n;
					n = write( cur_event->ident,
							   clients[cur_event->ident].wsaved.c_str(),
							   clients[cur_event->ident].wsaved.size() );
					if ( n < 0 ) {
						// client error
					} else if ( n > 0 ) {
					}
					// write( cur_event->ident, SERV_RESPONSE,
					// 	   strlen( SERV_RESPONSE ) );
				}
				if ( clients[cur_event->ident].flag & SOCK_WRITE == 0 ) {
					add_event_change_list( changelist, cur_event->ident,
										   EVFILT_WRITE, EV_ADD | EV_DISABLE, 0,
										   0, NULL );
				}
				std::cout << "sending response end" << std::endl;
			}
		}
	}
	return 0;
}

// class getHttpHeader {
// }

// struct header_buffer {
// 	std::string saved;
// 	size_t      read;
// 	size_t      no_crlf_to;
// };

// std::string getNextCRLF( uintptr_t fd ) {
// 	static struct header_buffer buf[1000000];
// 	char                        strbuf[1024];
// 	ssize_t                     n;
// 	int                         pos;

// 	if ( buf[fd].saved.size() &&
// 		 buf[fd].saved.size() - 1 > buf[fd].no_crlf_to ) {
// 		pos = buf[fd].saved.find( "\r\n" );
// 		if ( pos != std::string::npos ) {
// 			std::string tmp = buf[fd].saved.substr( buf[fd].no_crlf_to, pos
// ); 			buf[fd].no_crlf_to = pos + 2; 			return tmp;
// 		}
// 		buf[fd].no_crlf_to = buf[fd].saved.size() - 2;
// 	}
// 	n = read( fd, strbuf, 1024 );
// 	if ( n <= 0 ) {
// 		std::string tmp = buf[fd].saved.substr( buf[fd].no_crlf_to, pos );
// 		buf[fd].saved.clear();
// 		return "";
// 	} else if ( n == 0 ) {
// 		buf[fd].saved.clear();
// 		return "";
// 	}
// 	buf[fd].saved.append( strbuf, buf[fd].saved.size(), n );
// 	return getNextCRLF( fd );
// }

// std::string getHeader( uintptr_t fd, std::string &body ) {
// 	char    strbuf[10000];
// 	ssize_t n;
// 	size_t  pos;

// 	n = read( fd, strbuf, 10000 );
// 	if ( n < 0 ) {
// 		std::cerr << "client error: " << fd << std::endl;
// 		close( fd );
// 		return "";
// 	}
// }

// char **str_split(std::string &orgin, char *delim) {

// }
