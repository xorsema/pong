#include <SDL2/SDL.h>
#include <stdio.h>
#include <stdbool.h>
#include <stdint.h>
#include <math.h>
#include <stdlib.h>
#include <pthread.h>
#include <errno.h>

#include <sys/unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>

#define MAXPACKETSIZE 512
#define PORTNUM "1200"

#define dist_form( x, y ) ( sqrt( pow( x, 2 ) + pow( y, 2 ) ) )

struct player
{
	SDL_Rect rect[2];
	float offset;
	bool status[2];
	int score;
	SDL_mutex *mutex;
};

struct ball
{
	float x, y;
	float xv, yv;
	SDL_Rect rect;
	bool colliding;
	SDL_mutex *mutex;
};

struct net
{
	char port[6];
	char *node;
	int socket;
	struct sockaddr *addr;
	int addrlen;
	int state;
	int type;
};

bool init();
void quit();
void input( SDL_Event );
void loop();
void white_rect( SDL_Rect * );
void reset_ball();
void handle_ball();
bool net_bind( struct net * );
bool net_recv( struct net *, void *, int, int *, struct sockaddr*, int* );
bool net_send( struct net *, void *, int, struct sockaddr *, int );
bool net_simple_packet( struct net *, uint8_t, struct sockaddr *, int );
void *net_thread( void * );

const char *WINDOW_TITLE = "Pong";
const int WIN_WIDTH = 640;
const int WIN_HEIGHT = 480;
const int PADDLE_HEIGHT = 125;
const int PADDLE_WIDTH = 25;
const int PADDLE_SPEED = 200;
const int BALL_SIZE = 20;
const int BALL_SPEED = 400;
const int PADDLE_STRENGTH = 300;

enum 
{
	NET_LOCAL = 1,
	NET_HOST = 2,
	NET_JOIN = 3
};

enum
{
	PACKET_SYN = 1,
	PACKET_ACK = 2,
	PACKET_SYNACK = 3,
	PACKET_UPDATE = 4
};

enum
{
	NET_STATE_WAIT_SYN,
	NET_STATE_WAIT_ACK,
	NET_STATE_WAIT_SYNACK,
	NET_STATE_GAME
};

SDL_Window *window;
SDL_Renderer *renderer;
bool running;
Uint32 delta;

struct player player1, player2;
struct ball ball;

bool init()
{
	window = SDL_CreateWindow( WINDOW_TITLE, SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, WIN_WIDTH, WIN_HEIGHT, 0 );
	renderer = SDL_CreateRenderer( window, -1, SDL_RENDERER_ACCELERATED );

	player1.rect[0].w = PADDLE_WIDTH;
	player1.rect[0].h = PADDLE_HEIGHT;

	player1.rect[1].w = PADDLE_WIDTH;
	player1.rect[1].h = PADDLE_HEIGHT;

	player2.rect[0].w = PADDLE_HEIGHT;
	player2.rect[0].h = PADDLE_WIDTH;

	player2.rect[1].w = PADDLE_HEIGHT;
	player2.rect[1].h = PADDLE_WIDTH;

	ball.rect.w = BALL_SIZE;
	ball.rect.h = BALL_SIZE;

	reset_ball();

	player1.rect[1].x = WIN_WIDTH - player1.rect[1].w;
	player2.rect[1].y = WIN_HEIGHT - player2.rect[1].h;

	ball.colliding = false;

	player1.mutex = SDL_CreateMutex();
	player2.mutex = SDL_CreateMutex();
	ball.mutex = SDL_CreateMutex();

	return ( ( window != NULL ) && ( renderer != NULL ) );
}

void quit()
{
	if( renderer )
	{
		SDL_DestroyRenderer( renderer );
	}

	if( window )
	{
		SDL_DestroyWindow( window );
	}

	SDL_Quit();
}

void input( SDL_Event event )
{
	switch( event.type )
	{
	case SDL_QUIT:
		running = false;
		break;
		
	case SDL_KEYDOWN:
		if( event.key.keysym.sym == SDLK_DOWN )
		{
			player1.status[1] = true;
		}
		if( event.key.keysym.sym == SDLK_UP )
		{
			player1.status[0] = true;
		}
		if( event.key.keysym.sym == SDLK_a )
		{
			player2.status[1] = true;
		}
		if( event.key.keysym.sym == SDLK_d )
		{
			player2.status[0] = true;
		}

		break;

	case SDL_KEYUP:
		if( event.key.keysym.sym == SDLK_DOWN )
		{
			player1.status[1] = false;
		}
		if( event.key.keysym.sym == SDLK_UP )
		{
			player1.status[0] = false;
		}

		if( event.key.keysym.sym == SDLK_a )
		{
			player2.status[1] = false;
		}
		if( event.key.keysym.sym == SDLK_d )
		{
			player2.status[0] = false;
		}

		if( event.key.keysym.sym == SDLK_r )
		{
			reset_ball();
			ball.xv = -BALL_SPEED;
		}

		break;
	}
}

void loop()
{
	SDL_Event event;
	Uint32 ticks = SDL_GetTicks();

	while( running )
	{
		while( SDL_PollEvent( &event ) )
		{
			input( event );
		}

		if( player1.status[0] )
		{
			player1.offset -= PADDLE_SPEED * ( delta / 1000.f );
		}

		if( player1.status[1] )
		{
			player1.offset += PADDLE_SPEED * ( delta / 1000.f );
		}

		if( player2.status[1] )
		{
			player2.offset -= PADDLE_SPEED * ( delta / 1000.f );
		}

		if( player2.status[0] )
		{
			player2.offset += PADDLE_SPEED * ( delta / 1000.f );
		}


		player1.rect[0].y = player1.offset;
		player2.rect[0].x = player2.offset;
		player1.rect[1].y = player1.offset;
		player2.rect[1].x = player2.offset;

		ball.x += ball.xv * ( delta / 1000.f );
		ball.y += ball.yv * ( delta / 1000.f );

		ball.rect.x = ball.x;
		ball.rect.y = ball.y;

		handle_ball();

		SDL_RenderClear( renderer );

		white_rect( &player1.rect[0] );
		white_rect( &player2.rect[0] );
		white_rect( &player1.rect[1] );
		white_rect( &player2.rect[1] );

		white_rect( &ball.rect );

		SDL_RenderPresent( renderer );

		delta = SDL_GetTicks() - ticks;
		ticks = SDL_GetTicks();
	}
}

void white_rect( SDL_Rect *rect )
{
	Uint8 r, g, b, a;

	SDL_GetRenderDrawColor( renderer, &r, &g, &b, &a );
	SDL_SetRenderDrawColor( renderer, 255, 255, 255, 255 );
	SDL_RenderFillRect( renderer, rect );
	SDL_SetRenderDrawColor( renderer, r, g, b, a );
}

void reset_ball()
{
	ball.x = ( WIN_WIDTH / 2 ) - ( BALL_SIZE / 2 );
	ball.y = ( WIN_HEIGHT / 2 ) - ( BALL_SIZE / 2 );
	ball.xv = ball.yv = 0.0;
}

void handle_ball()
{
	if( ball.x + BALL_SIZE < 0 )
	{
		player2.score ++;
		reset_ball();
		return;
	}
	if( ball.x > WIN_WIDTH )
	{
		player2.score ++;
		reset_ball();
		return;
	}

	if( ball.y + BALL_SIZE < 0 )
	{
		player1.score ++;
		reset_ball();
		return;
	}
	if( ball.y > WIN_HEIGHT )
	{
		player1.score ++;
		reset_ball();
		return;
	}

        SDL_Rect *p = NULL;

	if( SDL_HasIntersection( &ball.rect, &player1.rect[0] ) )
		p = &player1.rect[0];
	if( SDL_HasIntersection( &ball.rect, &player2.rect[0] ) )
		p = &player2.rect[0];
	if( SDL_HasIntersection( &ball.rect, &player1.rect[1] ) )
		p = &player1.rect[1];
	if( SDL_HasIntersection( &ball.rect, &player2.rect[1] ) )
		p = &player2.rect[1];

	if( p != NULL && ball.colliding == false )
	{
		ball.xv = ( ( ( ball.x + BALL_SIZE / 2.0 ) - (p->x + p->w / 2.0 ) )  );
		ball.yv = ( ( ball.y + BALL_SIZE / 2.0 ) - (p->y + p->h / 2.0 ) );
		float dist = dist_form( ball.xv, ball.yv );
		ball.xv /= dist;
		ball.yv /= dist;
		ball.xv *= BALL_SPEED;
		ball.yv *= BALL_SPEED;
		ball.colliding = true;
	}

	if ( p == NULL )
	{
		ball.colliding = false;
	}
}

bool net_bind( struct net *pnet )
{
	struct addrinfo hints;
	struct addrinfo *servinfo, *p;
	int result;

	memset( &hints, 0, sizeof( struct addrinfo ) );
	hints.ai_family = AF_UNSPEC;
	hints.ai_socktype = SOCK_DGRAM;
	hints.ai_flags = AI_PASSIVE;

	if( getaddrinfo( NULL, pnet->port, &hints, &servinfo ) != 0 )
	{
		printf( "getaddrinfo error\n" );
		return false;
	}

	for( p = servinfo; p != NULL; p = p->ai_next )
	{
	
		if( ( result = socket( p->ai_family, p->ai_socktype, p->ai_protocol ) ) == -1 )
		{
			perror( "socket" );
			continue;
		}

		if( bind( result, p->ai_addr, p->ai_addrlen ) == -1 )
		{
		        perror( "bind" );
			continue;
		}

		break;
	}

	freeaddrinfo( servinfo );

	if( p == NULL )
	{
		printf( "nothing to bind to\n" );
		return false;
	}

	pnet->socket = result;

	return true;
}

bool net_recv( struct net *pnet, void *outbuf, int buflen, int *outlen, struct sockaddr *from, int *fromlen )
{
	uint8_t buf[MAXPACKETSIZE];
	int numbytes;

	numbytes = recvfrom( pnet->socket, buf, MAXPACKETSIZE, 0, from, fromlen );
	if( numbytes == -1 || numbytes > buflen )
	{
		return false;
	}
	
	memcpy( outbuf, buf, numbytes );
	*outlen = numbytes;
	return true;
}

bool net_send( struct net *pnet, void *inbuf, int inlen, struct sockaddr *to, int tolen )
{
	int numbytes;
	char ip4[INET_ADDRSTRLEN];

	inet_ntop( AF_INET, &(((struct sockaddr_in*)to)->sin_addr), ip4, INET_ADDRSTRLEN);
	printf( "sending to %s!", ip4 );

	numbytes = sendto( pnet->socket, inbuf, inlen, 0, to, tolen );
	if( numbytes < inlen )
	{
		return false;
	}

	return true;
}

/* Send a packet without game state data (syn, ack, etc) */
bool net_simple_packet( struct net *pnet, uint8_t type, struct sockaddr *to, int tolen )
{
	return net_send( pnet, &type, 1, to, tolen );
}

void *net_thread( void *ptr )
{
	uint8_t buf[MAXPACKETSIZE];
	int buflen = MAXPACKETSIZE;
	int recvbytes;
	struct net *pnet = (struct net*)ptr;
	struct sockaddr_storage from;
	int fromlen;

	while( running )
	{
		if( !net_recv( pnet, buf, buflen, &recvbytes, (struct sockaddr*)&from, &fromlen ) )
		{
			continue;
		}

		switch( pnet->state )
		{
		case NET_STATE_WAIT_SYN:
			if( *(uint8_t*)&buf[0] == PACKET_SYN )
			{
				printf( "SYN received\n" );
				net_simple_packet( pnet, PACKET_ACK, (struct sockaddr*)&from, fromlen );
				pnet->state = NET_STATE_WAIT_SYNACK;
			}
			break;
		case NET_STATE_WAIT_ACK:
			if( *(uint8_t*)&buf[0] == PACKET_ACK )
			{
				net_simple_packet( pnet, PACKET_SYNACK, (struct sockaddr*)&from, fromlen );
				pnet->state = NET_STATE_GAME;
			}
			break;
		case NET_STATE_WAIT_SYNACK:
			if( *(uint8_t*)&buf[0] == PACKET_SYNACK )
			{
				pnet->state = NET_STATE_GAME;
			}
			break;
		case NET_STATE_GAME:
			if( *(uint8_t*)&buf[0] == PACKET_UPDATE )
			{
				//...
			}
			break;
		}

		memset( buf, 0, MAXPACKETSIZE );
	}
}

void net_create_thread( struct net *pnet )
{
	pthread_t t;

	pthread_create( &t, NULL, net_thread, pnet );
}

int main( int argc, char **argv )
{
	struct net net;
	running = true;
	struct sockaddr_in ipaddr;
	int ipaddrlen;

	if( argc > 1 )
	{
		strcpy( net.port, PORTNUM );
		if( strcmp( "join", argv[1] ) == 0 )
		{
			if( argc < 2 )
			{
				printf( "Please specify a host to connect to!\n" );
				return 1;
			}
			net.type = NET_JOIN;
		}
		else if( strcmp( "host", argv[1] ) == 0 )
		{
			net.type = NET_HOST;
			net.state = NET_STATE_WAIT_SYN;
		}
		else
		{
			printf( "Unknown argument!\n" );
			return 1;
		}

		if( !net_bind( &net ) )
		{
			printf( "Could not bind to port, exiting!\n" );
			return 1;
		}

		printf( "Bound to port %s\n", net.port );
		fflush( stdout );

		if( net.type == NET_JOIN )
		{
			ipaddr.sin_family = AF_INET;
			ipaddr.sin_addr.s_addr = inet_addr( argv[2] );
			ipaddr.sin_port = htons( (unsigned short)atoi( PORTNUM ) );
			ipaddrlen = sizeof( ipaddr );

			net.addr = (struct sockaddr*)&ipaddr;
			net.addrlen = ipaddrlen;

			if( !net_simple_packet( &net, PACKET_SYN, (struct sockaddr*)net.addr, net.addrlen ) )
			{
				printf( "simple packet send error" );
			}
			net.state = NET_STATE_WAIT_ACK;
		}
		else
		{
			net.state = NET_STATE_WAIT_SYN;
		}

		net_create_thread( &net );

		while( net.state != NET_STATE_GAME )
		{
			SDL_Delay( 10 );
		}
	}
	else
	{
		net.type = NET_LOCAL;
	}

	if( !init() )
	{
		printf( "an error occurred\n" );
		quit();
		return 1;
	}
	
	ball.xv = -BALL_SPEED;

	loop();

	quit();
	return 0;
}
