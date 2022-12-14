#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/types.h>
#include <sys/socket.h>

#define BUFSIZE 10240
#define SERV_IP "127.0.0.1"
#define SERV_PORT 80

void error_handling( char *message );

int main( int argc, char **argv ) {
	int                sock;
	struct sockaddr_in serv_addr;
	char               message[BUFSIZE];
	int                str_len;

	// if ( argc != 3 ) {
	// 	printf( "Usage : %s <IP> <port> \n", argv[0] );
	// 	exit( 1 );
	// }

	sock = socket( PF_INET, SOCK_STREAM, 0 ); /* 서버 접속을 위한 소켓 생성 */
	if ( sock == -1 ) {
		error_handling( "socket() error" );
	}

	memset( &serv_addr, 0, sizeof( serv_addr ) );
	serv_addr.sin_family = AF_INET;
	serv_addr.sin_addr.s_addr = inet_addr( SERV_IP );
	serv_addr.sin_port = htons( SERV_PORT );

	if ( connect( sock, (struct sockaddr *)&serv_addr, sizeof( serv_addr ) ) ==
		 -1 ) {
		error_handling( "connect() error" );
	}

	while ( 1 ) {
		/* 메시지 입력 전송*/
		// fputs( "전송할 메시지를 입력하세요(q to quit) : ", stdout );
		// fgets( message, BUFSIZE, stdin );
		// if ( !strcmp( message, "q\n" ) ) {
		// 	break;
		// }
		sleep( 1 );
		char *message2 =
			"HEAD / HTTP/1.1\r\nHost:localhost:1234\r\nUser-Agent: "
			"Go-http-client/1.1\r\nAccept-Encoding:"
			"gzip\r\n\r\n";
		write( sock, message2, strlen( message2 ) );

		/* 메시지 수신 출력 */
		str_len = read( sock, message, BUFSIZE - 1 );
		message[str_len] = 0;
		printf( "서버로부터 전송된 메시지 : %s \n", message );
	}
	close( sock );
	return 0;
}

void error_handling( char *message ) {
	fputs( message, stderr );
	fputc( '\n', stderr );
	exit( 1 );
}
