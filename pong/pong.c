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

#pragma pack(push, 4)
struct player
{
	SDL_Rect rect[2];
	float offset;
	int status[2];
	int score;
};

struct ball
{
	float x, y;
	float xv, yv;
	SDL_Rect rect;
	int colliding;
};
#pragma pack(pop)

struct net
{
	UDPsocket socket;
	IPaddress addr;
	int state;
	int type;
};

#pragma pack(push, 4)
struct cmd
{
	int type;
	uint32_t time;
	union
	{
		float offset; /* player movement */
		int direction; /* serve direction */
	} data;
};
#pragma pack(pop)

struct cmd_buf
{
	struct cmd *cmds;
	unsigned len;
	unsigned maxlen;
};

#pragma pack(push, 4)
struct cmd_net_buf
{
	uint32_t len;
	struct cmd cmds[1];
};
#pragma pack(pop)

int init();
void quit();
void input( SDL_Event );
void loop();
void white_rect( SDL_Rect * );
void reset_ball( struct ball *pball );
void handle_ball( struct ball *pball, struct player *p1p, struct player *p2p );
struct cmd_buf *init_cmd_buf( unsigned size );
void free_cmd_buf( struct cmd_buf *p );
void player_move_cmd( struct cmd *out, int type, float offset );
int add_to_cmd_buf( struct cmd_buf *buf, struct cmd cmd );
void clear_cmd_buf( struct cmd_buf *p );
struct cmd_net_buf *cmd_to_net( struct cmd_buf *in );
void advance_gamestate( uint32_t start, uint32_t duration, uint32_t timestep, struct ball *pball, struct player *p1p, struct player *p2p, struct cmd_buf *buf );
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

enum
{
	CMD_PLAYER1_MOVE = 1,
	CMD_PLAYER1_SERVE = 2,
	CMD_PLAYER2_MOVE = 3,
	CMD_PLAYER2_SERVE = 4
};

SDL_Window *window;
SDL_Renderer *renderer;
int running;
uint32_t start_time;
uint32_t current_time;
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

	reset_ball( &ball );

	player1.rect[1].x = WIN_WIDTH - player1.rect[1].w;
	player2.rect[1].y = WIN_HEIGHT - player2.rect[1].h;

	ball.colliding = 0;

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
			reset_ball( &ball );
			ball.xv = -BALL_SPEED;
		}

		break;
	}
}

void loop()
{
	struct cmd_buf *cb;
	struct cmd tc;
	SDL_Event event;
	Uint32 ticks = SDL_GetTicks();
	start_time = ticks;

	cb = init_cmd_buf( 512 );

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
				player_move_cmd( &tc, CMD_PLAYER1_MOVE, -PADDLE_SPEED * ( delta / 1000.f ) );
				add_to_cmd_buf( cb, tc );
			}
			
			if( player1.status[1] )
			{
				player_move_cmd( &tc, CMD_PLAYER1_MOVE, PADDLE_SPEED * ( delta / 1000.f ) );
				add_to_cmd_buf( cb, tc );
			}
		}

		if( net.type == NET_LOCAL || net.type == NET_JOIN )
		{	
			if( player2.status[1] )
			{
				player_move_cmd( &tc, CMD_PLAYER2_MOVE, -PADDLE_SPEED * ( delta / 1000.f ) );
				add_to_cmd_buf( cb, tc );
			}
			
			if( player2.status[0] )
			{
				player_move_cmd( &tc, CMD_PLAYER2_MOVE, PADDLE_SPEED * ( delta / 1000.f ) );
				add_to_cmd_buf( cb, tc );
			}
		}

		advance_gamestate( current_time, delta, 10, &ball, &player1, &player2, cb );

		clear_cmd_buf( cb );

		SDL_RenderClear( renderer );

		white_rect( &player1.rect[0] );
		white_rect( &player2.rect[0] );
		white_rect( &player1.rect[1] );
		white_rect( &player2.rect[1] );

		white_rect( &ball.rect );

		SDL_RenderPresent( renderer );

		delta = SDL_GetTicks() - ticks;
		current_time = SDL_GetTicks() - start_time;
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

void reset_ball( struct ball *pball )
{
	pball->x = ( WIN_WIDTH / 2 ) - ( BALL_SIZE / 2 );
	pball->y = ( WIN_HEIGHT / 2 ) - ( BALL_SIZE / 2 );
	pball->xv = pball->yv = 0.0;
}

void handle_ball( struct ball *pball, struct player *p1p, struct player *p2p )
{
	SDL_Rect *p;
	float dist;
	if( pball->x + BALL_SIZE < 0 )
	{
		p2p->score ++;
		reset_ball( pball );
		return;
	}
	if( pball->x > WIN_WIDTH )
	{
		p2p->score ++;
		reset_ball( pball );
		return;
	}

	if( pball->y + BALL_SIZE < 0 )
	{
		p1p->score ++;
		reset_ball( pball );
		return;
	}
	if( pball->y > WIN_HEIGHT )
	{
		p1p->score ++;
		reset_ball( pball );
		return;
	}

    p = (SDL_Rect*)NULL;

	if( SDL_HasIntersection( &pball->rect, &p1p->rect[0] ) )
		p = &p1p->rect[0];
	if( SDL_HasIntersection( &pball->rect, &p2p->rect[0] ) )
		p = &p2p->rect[0];
	if( SDL_HasIntersection( &pball->rect, &p1p->rect[1] ) )
		p = &p1p->rect[1];
	if( SDL_HasIntersection( &pball->rect, &p2p->rect[1] ) )
		p = &p2p->rect[1];

	if( p != NULL && pball->colliding == 0 )
	{
		pball->xv = ( ( ( pball->x + BALL_SIZE / 2.0 ) - (p->x + p->w / 2.0 ) )  );
		pball->yv = ( ( pball->y + BALL_SIZE / 2.0 ) - (p->y + p->h / 2.0 ) );
		dist = dist_form( pball->xv, pball->yv );
		pball->xv /= dist;
		pball->yv /= dist;
		pball->xv *= BALL_SPEED;
		pball->yv *= BALL_SPEED;
		pball->colliding = 1;
	}

	if ( p == NULL )
	{
		pball->colliding = 0;
	}
}

struct cmd_buf *init_cmd_buf( unsigned size )
{
	struct cmd_buf *r = (struct cmd_buf*)malloc( sizeof(struct cmd_buf) );
	r->cmds = (struct cmd*)malloc( sizeof(struct cmd) * size );
	r->maxlen = size;
	r->len = 0;
}

void free_cmd_buf( struct cmd_buf *p )
{
	free( p->cmds );
	free( p );
}

void clear_cmd_buf( struct cmd_buf *p )
{
	memset( p->cmds, 0, sizeof(struct cmd) * p->len );
	p->len = 0;
}

void player_move_cmd( struct cmd *out, int type, float offset )
{
	out->data.offset = offset;
	out->type = type;
	out->time = SDL_GetTicks() - start_time;
}

int add_to_cmd_buf( struct cmd_buf *buf, struct cmd cmd )
{
	if( buf->len == buf->maxlen )
		return 0;

	buf->len += 1;
	buf->cmds[ buf->len-1 ] = cmd;
	return 1;
}

struct cmd_net_buf *cmd_to_net( struct cmd_buf *in )
{
	uint8_t *p;
	struct cmd_net_buf *r = (struct cmd_net_buf*)malloc( sizeof( uint32_t ) + ( in->len * sizeof( struct cmd ) ) );
	r->len = in->len;
	p = (uint8_t*) (((uint32_t*)r)+1);
	memcpy( p, in->cmds, in->len * sizeof(struct cmd) );
	return r;
}

void advance_gamestate( uint32_t start, uint32_t duration, uint32_t timestep, struct ball *pball, struct player *p1p, struct player *p2p, struct cmd_buf *buf )
{
	uint32_t i;
	unsigned ci;

	for( i = 0; i < duration; i++ )
	{
		for( ci = 0; ci < buf->len; ci++ )
		{
			if( ( i + start ) == buf->cmds[ci].time )
			{
				switch( buf->cmds[ci].type )
				{
				case CMD_PLAYER1_MOVE:
					p1p->offset += buf->cmds[ci].data.offset;
					break;

				case CMD_PLAYER2_MOVE:
					p2p->offset += buf->cmds[ci].data.offset;
					break;
				}
			}
		}

		p1p->rect[0].y = p1p->offset;
		p2p->rect[0].x = p2p->offset;
		p1p->rect[1].y = p1p->offset;
		p2p->rect[1].x = p2p->offset;

		if( ( i + start ) % timestep == 0 )
		{
			pball->x += pball->xv * ( timestep / 1000.f );
			pball->y += pball->yv * ( timestep / 1000.f );

			pball->rect.x = pball->x;
			pball->rect.y = pball->y;

			handle_ball( pball, p1p, p2p );
		}
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