#include <SDL.h>
#include <SDL_net.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <math.h>
#include <stdlib.h>
#include <errno.h>

#define MAXPACKETSIZE 512
#define PORTNUM 1200

#define dist_form( x, y ) ( sqrt( ( x * x ) + ( y * y ) ) )

struct player
{
	SDL_Rect rect[2];
	float offset;
	int status[2];
	int score;
	SDL_mutex *mutex;
};

struct ball
{
	float x, y;
	float xv, yv;
	SDL_Rect rect;
	int colliding;
	SDL_mutex *mutex;
};

struct net
{
	UDPsocket socket;
	IPaddress addr;
	int state;
	int type;
};

int init();
void quit();
void input( SDL_Event );
void loop();
void white_rect( SDL_Rect * );
void reset_ball();
void handle_ball();
int net_bind( struct net * );
int net_recv( struct net *pnet, void *outbuf, int buflen, int *outlen, IPaddress *ip );
int net_send( struct net *pnet, void *inbuf, int inlen, IPaddress to );
int net_simple_packet( struct net *pnet, uint8_t type, IPaddress to );
int net_thread( void * );
int net_send_update( struct net *pnet, IPaddress to );

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
int running;
Uint32 delta;
struct net net;

struct player player1, player2;
struct ball ball;

int init()
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

	ball.colliding = 0;

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
		running = 0;
		break;
		
	case SDL_KEYDOWN:
		if( event.key.keysym.sym == SDLK_DOWN )
		{
			player1.status[1] = 1;
		}
		if( event.key.keysym.sym == SDLK_UP )
		{
			player1.status[0] = 1;
		}
		if( event.key.keysym.sym == SDLK_a )
		{
			player2.status[1] = 1;
		}
		if( event.key.keysym.sym == SDLK_d )
		{
			player2.status[0] = 1;
		}

		break;

	case SDL_KEYUP:
		if( event.key.keysym.sym == SDLK_DOWN )
		{
			player1.status[1] = 0;
		}
		if( event.key.keysym.sym == SDLK_UP )
		{
			player1.status[0] = 0;
		}

		if( event.key.keysym.sym == SDLK_a )
		{
			player2.status[1] = 0;
		}
		if( event.key.keysym.sym == SDLK_d )
		{
			player2.status[0] = 0;
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

		if( net.type == NET_LOCAL || net.type == NET_HOST )
		{
	  		if( player1.status[0] )
			{
				player1.offset -= PADDLE_SPEED * ( delta / 1000.f );
			}
			
			if( player1.status[1] )
			{
				player1.offset += PADDLE_SPEED * ( delta / 1000.f );
			}
		}

		if( net.type == NET_LOCAL || net.type == NET_JOIN )
		{	
			if( player2.status[1] )
			{
				player2.offset -= PADDLE_SPEED * ( delta / 1000.f );
			}
			
			if( player2.status[0] )
			{
				player2.offset += PADDLE_SPEED * ( delta / 1000.f );
			}
		}

		player1.rect[0].y = player1.offset;
		player2.rect[0].x = player2.offset;
		player1.rect[1].y = player1.offset;
		player2.rect[1].x = player2.offset;

		if( net.type == NET_LOCAL || net.type == NET_HOST )
		{
			ball.x += ball.xv * ( delta / 1000.f );
			ball.y += ball.yv * ( delta / 1000.f );
		}

		ball.rect.x = ball.x;
		ball.rect.y = ball.y;

		if( net.type == NET_LOCAL || net.type == NET_HOST )
		{
			handle_ball();
		}

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
	SDL_Rect *p;
	float dist;
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

    p = (SDL_Rect*)NULL;

	if( SDL_HasIntersection( &ball.rect, &player1.rect[0] ) )
		p = &player1.rect[0];
	if( SDL_HasIntersection( &ball.rect, &player2.rect[0] ) )
		p = &player2.rect[0];
	if( SDL_HasIntersection( &ball.rect, &player1.rect[1] ) )
		p = &player1.rect[1];
	if( SDL_HasIntersection( &ball.rect, &player2.rect[1] ) )
		p = &player2.rect[1];

	if( p != NULL && ball.colliding == 0 )
	{
		ball.xv = ( ( ( ball.x + BALL_SIZE / 2.0 ) - (p->x + p->w / 2.0 ) )  );
		ball.yv = ( ( ball.y + BALL_SIZE / 2.0 ) - (p->y + p->h / 2.0 ) );
		dist = dist_form( ball.xv, ball.yv );
		ball.xv /= dist;
		ball.yv /= dist;
		ball.xv *= BALL_SPEED;
		ball.yv *= BALL_SPEED;
		ball.colliding = 1;
	}

	if ( p == NULL )
	{
		ball.colliding = 0;
	}
}

int net_bind( struct net *pnet )
{
	SDLNet_ResolveHost( &pnet->addr, NULL, PORTNUM );
	pnet->socket = SDLNet_UDP_Open( PORTNUM );
	if( pnet->socket == 0 )
	{
		printf( "%s", SDLNet_GetError() );
		return 0;
	}
	
	printf( "Bound on port: %u\n", ( SDL_BYTEORDER == SDL_LIL_ENDIAN ) ? SDL_Swap16( pnet->addr.port ) : pnet->addr.port );

	return 1;
}

int net_recv( struct net *pnet, void *outbuf, int buflen, int *outlen, IPaddress *ip )
{
	UDPpacket *p;
	int err;

	p = SDLNet_AllocPacket( MAXPACKETSIZE );

	if( ( err = SDLNet_UDP_Recv( pnet->socket, p ) ) > 0 )
	{
		memcpy( outbuf, p->data, p->len );
		*outlen = p->len;
		*ip = p->address;
		return 1;
	}
	else if( err == -1 )
	{
		printf( "%s", SDLNet_GetError() );
		return 0;
	}

	SDLNet_FreePacket( p );
	
	return 0;
}

int net_send( struct net *pnet, void *inbuf, int inlen, IPaddress to )
{
	UDPpacket *p;
	int err;

	p = SDLNet_AllocPacket( MAXPACKETSIZE );

	p->address.host = to.host;
	p->address.port = to.port;

	memcpy( p->data, inbuf, inlen );

	p->len = inlen;

	err = SDLNet_UDP_Send( pnet->socket, -1, p );
	if( err == 0 )
	{
		printf( "Failed to send packet: %s", SDLNet_GetError() );
		fflush( stdout );
	}

	SDLNet_FreePacket( p );

	return err;
}

/* Send a packet without game state data (syn, ack, etc) */
int net_simple_packet( struct net *pnet, uint8_t type, IPaddress to )
{
	return net_send( pnet, (void*)&type, 1, to );
}

int net_send_update( struct net *pnet, IPaddress to )
{
	float *fp;
	uint8_t buf[sizeof(char) + ( sizeof(float) * 6 )];
	buf[0] = PACKET_UPDATE;

	fp = (float*)( buf+1 );
	fp[0] = player1.offset;
	fp[1] = player2.offset;

	fp[2] = ball.x;
	fp[3] = ball.y;
	fp[4] = ball.xv;
	fp[5] = ball.yv;

	return net_send( pnet, buf, sizeof(buf), to );
}

int net_thread( void *ptr )
{
	uint8_t buf[MAXPACKETSIZE];
	int buflen = MAXPACKETSIZE;
	int recvbytes;
	struct net *pnet = (struct net*)ptr;
	float *fp;
	IPaddress ip;

	while( running )
	{
		if( !net_recv( pnet, (void*)buf, buflen, &recvbytes, &ip ) )
		{
			continue;
		}

/*		printf( "Packet received!\n" );  */

		switch( pnet->state )
		{
		case NET_STATE_WAIT_SYN:
			if( *(uint8_t*)&buf[0] == PACKET_SYN )
			{
				printf( "SYN received, sending ACK\n" );
				pnet->addr.host = ip.host;
				net_simple_packet( pnet, PACKET_ACK, pnet->addr );
				pnet->state = NET_STATE_WAIT_SYNACK;
			}
			break;
		case NET_STATE_WAIT_ACK:
			if( *(uint8_t*)&buf[0] == PACKET_ACK )
			{
				printf( "ACK received, sending SYNACK\n" );
				net_simple_packet( pnet, PACKET_SYNACK, pnet->addr );
				pnet->state = NET_STATE_GAME;
			}
			break;
		case NET_STATE_WAIT_SYNACK:
			if( *(uint8_t*)&buf[0] == PACKET_SYNACK )
			{
				printf( "Got SYNACK\n" );
				pnet->state = NET_STATE_GAME;
				net_send_update( pnet, pnet->addr );
			}
			break;
		case NET_STATE_GAME:
			if( *(uint8_t*)&buf[0] == PACKET_UPDATE )
			{
				/*printf( "Got update\n" );*/
				fp = (float*)( buf+1 );
				
				if( net.type == NET_HOST )
				{
					player2.offset = fp[1];
				}

				if( net.type == NET_JOIN )
				{
					player1.offset = fp[0];

					ball.x = fp[2];
					ball.y = fp[3];
					ball.xv = fp[4];
					ball.yv = fp[5];
				}

				if( net.type == NET_HOST )
				{
					net_send_update( pnet, pnet->addr );
				} 
				else
				{
					net_send_update( pnet, pnet->addr );
				}
			}
			break;
		}

		memset( buf, 0, MAXPACKETSIZE );
	}

	return 0;
}

void net_create_thread( struct net *pnet )
{
	SDL_CreateThread(net_thread, "net", pnet);
}

int main( int argc, char **argv )
{
	running = 1;

	if( SDLNet_Init() < 0 )
	{
		printf( "Couldn't start SDLNet!" );
		return 1;
	}

	if( argc > 1 )
	{
		if( strcmp( "join", argv[1] ) == 0 )
		{
			if( argc < 3 )
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

		if( net.type == NET_JOIN )
		{
			SDLNet_ResolveHost( &net.addr, argv[2], PORTNUM );

			if( !net_simple_packet( &net, PACKET_SYN, net.addr ) )
			{
				printf( "simple packet send error: %s", SDLNet_GetError() );
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