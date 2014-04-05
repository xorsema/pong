#include <SDL.h>
#include <SDL_net.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <math.h>
#include <stdlib.h>
#include <errno.h>

#define MAXPACKETSIZE 0xFFFF
#define PORTNUM 1200

#define dist_form( x, y ) ( sqrt( ( x * x ) + ( y * y ) ) )

#pragma pack(push, 4)
struct player
{
	SDL_Rect rect[2];
	float offset;
	int score;
};

struct ball
{
	float x, y;
	float xv, yv;
	SDL_Rect rect;
	int colliding;
};

struct gamestate
{
	uint32_t time;
	struct player players[2];
	struct ball ball;
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
	uint32_t type;
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

struct simple_packet
{
	uint32_t type;
};

struct cmd_packet
{
	uint32_t type;
	struct cmd_net_buf buf;
};

struct update_packet
{
	uint32_t type;
	struct gamestate state;
};
#pragma pack(pop)

int init();
int net_init();
void net_wait_for_game( struct net *pnet );
void quit();
void input( SDL_Event );
void local_loop();
void client_loop();
void server_loop();
void init_gamestate( struct gamestate *g );
void render_gamestate( struct gamestate *g );
void white_rect( SDL_Rect * );
void reset_ball( struct ball *pball );
void handle_ball( struct ball *pball, struct player *p1p, struct player *p2p );
struct cmd_buf *init_cmd_buf( unsigned size );
void free_cmd_buf( struct cmd_buf *p );
void player_move_cmd( struct cmd *out, int type, float offset );
int add_to_cmd_buf( struct cmd_buf *buf, struct cmd cmd );
void clear_cmd_buf( struct cmd_buf *p );
struct cmd_net_buf *cmd_to_net( struct cmd_buf *in );
void advance_gamestate( uint32_t start, uint32_t duration, uint32_t timestep, struct gamestate *gs, struct cmd_buf *buf );
int net_bind( struct net * );
int net_recv( struct net *pnet, void *outbuf, int buflen, int *outlen, IPaddress *ip );
int net_send( struct net *pnet, void *inbuf, int inlen, IPaddress to );
int net_simple_packet( struct net *pnet, struct simple_packet* packet, IPaddress to );
/*int net_thread( void * );*/
int net_send_update( struct net *pnet, IPaddress to, struct gamestate *gs );

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
	PACKET_UPDATE = 4,
	PACKET_CMD = 5
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
int input_status[4];
struct net net;
struct gamestate local_state;
struct cmd_buf *local_cmd_buf;

int init()
{
	window = SDL_CreateWindow( WINDOW_TITLE, SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, WIN_WIDTH, WIN_HEIGHT, 0 );
	renderer = SDL_CreateRenderer( window, -1, SDL_RENDERER_ACCELERATED );

	init_gamestate( &local_state );

	return ( ( window != NULL ) && ( renderer != NULL ) );
}

int net_init()
{
	if( SDLNet_Init() < 0 )
	{
		printf( "Couldn't start SDLNet!\n" );
		return 0;
	}

	if( !net_bind( &net ) )
	{
		printf( "Could not bind to port!\n" );
		return 0;
	}
}

/* Should be called after a socket has been created */
void net_wait_for_game( struct net *pnet )
{
	uint8_t buf[MAXPACKETSIZE];
	int buflen = MAXPACKETSIZE;
	int recvbytes;
	IPaddress ip;
	struct simple_packet *sp;
	struct simple_packet syn_packet;
	struct simple_packet ack_packet;
	struct simple_packet synack_packet;

	syn_packet.type = PACKET_SYN;
	ack_packet.type = PACKET_ACK;
	synack_packet.type = PACKET_SYNACK;

	if( pnet->type == NET_JOIN )
	{
		net_simple_packet( pnet, &syn_packet, pnet->addr );
		pnet->state = NET_STATE_WAIT_ACK;
	}
	else if( pnet->type == NET_HOST )
	{
		pnet->state = NET_STATE_WAIT_SYN;
	}
	else
	{
		return;
	}


	while( pnet->state != NET_STATE_GAME )
	{
		if( !net_recv( pnet, (void*)buf, buflen, &recvbytes, &ip ) )
		{
			continue;
		}

		sp = (struct simple_packet*)buf;

		switch( pnet->state )
		{
		case NET_STATE_WAIT_SYN:
			if( sp->type == PACKET_SYN )
			{
				printf( "SYN received, sending ACK\n" );
				pnet->addr.host = ip.host;
				net_simple_packet( pnet, &ack_packet, pnet->addr );
				pnet->state = NET_STATE_WAIT_SYNACK;
			}
			break;
		case NET_STATE_WAIT_ACK:
			if( sp->type == PACKET_ACK )
			{
				printf( "ACK received, sending SYNACK\n" );
				net_simple_packet( pnet, &synack_packet, pnet->addr );
				pnet->state = NET_STATE_GAME;
			}
			break;
		case NET_STATE_WAIT_SYNACK:
			if( sp->type == PACKET_SYNACK )
			{
				printf( "Got SYNACK\n" );
				pnet->state = NET_STATE_GAME;
			}
			break;
		default:
			break;
		}

		memset( buf, 0, MAXPACKETSIZE );
	}
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
			input_status[1] = 1;
		}
		if( event.key.keysym.sym == SDLK_UP )
		{
			input_status[0] = 1;
		}
		if( event.key.keysym.sym == SDLK_a )
		{
			input_status[3] = 1;
		}
		if( event.key.keysym.sym == SDLK_d )
		{
			input_status[2] = 1;
		}

		break;

	case SDL_KEYUP:
		if( event.key.keysym.sym == SDLK_DOWN )
		{
			input_status[1] = 0;
		}
		if( event.key.keysym.sym == SDLK_UP )
		{
			input_status[0] = 0;
		}

		if( event.key.keysym.sym == SDLK_a )
		{
			input_status[3] = 0;
		}
		if( event.key.keysym.sym == SDLK_d )
		{
			input_status[2] = 0;
		}

		if( event.key.keysym.sym == SDLK_r )
		{
			reset_ball( &local_state.ball );
			local_state.ball.xv = -BALL_SPEED;
		}

		break;
	}
}

void client_loop()
{
	uint8_t buf[MAXPACKETSIZE];
	int buflen = MAXPACKETSIZE;
	int recvbytes;
	IPaddress ip;
	struct update_packet *up;
	struct cmd tc;
	SDL_Event event;
	Uint32 ticks = SDL_GetTicks();
	start_time = ticks;

	local_cmd_buf = init_cmd_buf( 0xFFF );

	while( running )
	{
		while( SDL_PollEvent( &event ) )
		{
			input( event );
		}

		if( net_recv( &net, (void*)buf, buflen, &recvbytes, &ip ) && ((struct simple_packet*)buf)->type == PACKET_UPDATE )
		{
			up = (struct update_packet*)buf;
			local_state = up->state;
		}

	  	/*if( input_status[0] )
		{
			player_move_cmd( &tc, CMD_PLAYER1_MOVE, -PADDLE_SPEED * ( delta / 1000.f ) );
			add_to_cmd_buf( local_cmd_buf, tc );
		}
		
		if( input_status[1] )
		{
			player_move_cmd( &tc, CMD_PLAYER1_MOVE, PADDLE_SPEED * ( delta / 1000.f ) );
			add_to_cmd_buf( local_cmd_buf, tc );
		}*/
			
		if( input_status[3] )
		{
			player_move_cmd( &tc, CMD_PLAYER2_MOVE, -PADDLE_SPEED * ( delta / 1000.f ) );
			add_to_cmd_buf( local_cmd_buf, tc );
		}
		
		if( input_status[2] )
		{
			player_move_cmd( &tc, CMD_PLAYER2_MOVE, PADDLE_SPEED * ( delta / 1000.f ) );
			add_to_cmd_buf( local_cmd_buf, tc );
		}
		
		net_send_cmd_buf( &net, net.addr, local_cmd_buf );

		if( local_state.time < current_time )
		{
			advance_gamestate( local_state.time, current_time - local_state.time, 10, &local_state, local_cmd_buf );
		}
		else
			advance_gamestate( local_state.time, delta, 10, &local_state, local_cmd_buf );

		clear_cmd_buf( local_cmd_buf );

		SDL_RenderClear( renderer );

		render_gamestate( &local_state );

		SDL_RenderPresent( renderer );

		delta = SDL_GetTicks() - ticks;
		current_time = SDL_GetTicks() - start_time;
		local_state.time = current_time;
		ticks = SDL_GetTicks();
	}
}

void server_loop()
{
	uint8_t buf[MAXPACKETSIZE];
	int buflen = MAXPACKETSIZE;
	int recvbytes;
	IPaddress ip;
	struct cmd_packet *cp;
	struct cmd tc;
	SDL_Event event;
	int i;
	Uint32 ticks = SDL_GetTicks();
	start_time = ticks;

	local_cmd_buf = init_cmd_buf( 0xFFF );

	while( running )
	{
		while( SDL_PollEvent( &event ) )
		{
			input( event );
		}

		if( net_recv( &net, (void*)buf, buflen, &recvbytes, &ip ) && ((struct simple_packet*)buf)->type == PACKET_CMD )
		{
			cp = (struct cmd_packet*)buf;
			for( i = 0; i < cp->buf.len; i++ )
			{
				add_to_cmd_buf( local_cmd_buf, cp->buf.cmds[i] );
			}
		}

	  	if( input_status[0] )
		{
			player_move_cmd( &tc, CMD_PLAYER1_MOVE, -PADDLE_SPEED * ( delta / 1000.f ) );
			add_to_cmd_buf( local_cmd_buf, tc );
		}
		
		if( input_status[1] )
		{
			player_move_cmd( &tc, CMD_PLAYER1_MOVE, PADDLE_SPEED * ( delta / 1000.f ) );
			add_to_cmd_buf( local_cmd_buf, tc );
		}
			
		/*if( input_status[3] )
		{
			player_move_cmd( &tc, CMD_PLAYER2_MOVE, -PADDLE_SPEED * ( delta / 1000.f ) );
			add_to_cmd_buf( local_cmd_buf, tc );
		}
		
		if( input_status[2] )
		{
			player_move_cmd( &tc, CMD_PLAYER2_MOVE, PADDLE_SPEED * ( delta / 1000.f ) );
			add_to_cmd_buf( local_cmd_buf, tc );
		}*/
		
		advance_gamestate( local_state.time, delta, 10, &local_state, local_cmd_buf );

		clear_cmd_buf( local_cmd_buf );

		SDL_RenderClear( renderer );

		render_gamestate( &local_state );

		SDL_RenderPresent( renderer );

		net_send_update( &net, net.addr, &local_state );

		delta = SDL_GetTicks() - ticks;
		current_time = SDL_GetTicks() - start_time;
		local_state.time = current_time;
		ticks = SDL_GetTicks();
	}
}

void local_loop()
{
	SDL_Event event;
	Uint32 ticks = SDL_GetTicks();
	start_time = ticks;

	while( running )
	{
		while( SDL_PollEvent( &event ) )
		{
			input( event );
		}

		
	  	if( input_status[0] )
		{
			local_state.players[0].offset -= PADDLE_SPEED * ( delta / 1000.f );
		}
		
		if( input_status[1] )
		{
			local_state.players[0].offset += PADDLE_SPEED * ( delta / 1000.f );
		}
		
		if( input_status[3] )
		{
			local_state.players[1].offset -= PADDLE_SPEED * ( delta / 1000.f );
		}

		if( input_status[2] )
		{
			local_state.players[1].offset += PADDLE_SPEED * ( delta / 1000.f );
		}
		

		local_state.players[0].rect[0].y = local_state.players[0].offset;
		local_state.players[1].rect[0].x = local_state.players[1].offset;
		local_state.players[0].rect[1].y = local_state.players[0].offset;
		local_state.players[1].rect[1].x = local_state.players[1].offset;

		
		local_state.ball.x += local_state.ball.xv * ( delta / 1000.f );
		local_state.ball.y += local_state.ball.yv * ( delta / 1000.f );
		

		local_state.ball.rect.x = local_state.ball.x;
		local_state.ball.rect.y = local_state.ball.y;

		
		handle_ball( &local_state.ball, &local_state.players[0], &local_state.players[1] );

		SDL_RenderClear( renderer );

		render_gamestate( &local_state );

		SDL_RenderPresent( renderer );

		delta = SDL_GetTicks() - ticks;
		current_time = SDL_GetTicks() - start_time;
		local_state.time = current_time;
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

void init_gamestate( struct gamestate *g )
{
	g->time = 0;

	g->players[0].rect[0].w = PADDLE_WIDTH;
	g->players[0].rect[0].h = PADDLE_HEIGHT;

	g->players[0].rect[1].w = PADDLE_WIDTH;
	g->players[0].rect[1].h = PADDLE_HEIGHT;

	g->players[1].rect[0].w = PADDLE_HEIGHT;
	g->players[1].rect[0].h = PADDLE_WIDTH;

	g->players[1].rect[1].w = PADDLE_HEIGHT;
	g->players[1].rect[1].h = PADDLE_WIDTH;

	g->ball.rect.w = BALL_SIZE;
	g->ball.rect.h = BALL_SIZE;

	reset_ball( &g->ball );

	g->players[0].rect[1].x = WIN_WIDTH - g->players[0].rect[1].w;
	g->players[1].rect[1].y = WIN_HEIGHT - g->players[1].rect[1].h;

	g->ball.colliding = 0;
}

void render_gamestate( struct gamestate *g )
{
	white_rect( &g->players[0].rect[0] );
	white_rect( &g->players[1].rect[0] );
	white_rect( &g->players[0].rect[1] );
	white_rect( &g->players[1].rect[1] );

	white_rect( &g->ball.rect );
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
	struct cmd_buf *r;
	
	printf( "init_cmd_buf called!\n" );

	r = (struct cmd_buf*)malloc( sizeof(struct cmd_buf) );
	r->cmds = (struct cmd*)malloc( sizeof(struct cmd) * size );
	r->maxlen = size;
	r->len = 0;

	return r;
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

void advance_gamestate( uint32_t start, uint32_t duration, uint32_t timestep, struct gamestate *gs, struct cmd_buf *buf )
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
					gs->players[0].offset += buf->cmds[ci].data.offset;
					break;

				case CMD_PLAYER2_MOVE:
					gs->players[1].offset += buf->cmds[ci].data.offset;
					break;
				}
			}
		}

		gs->players[0].rect[0].y = gs->players[0].offset;
		gs->players[1].rect[0].x = gs->players[1].offset;
		gs->players[0].rect[1].y = gs->players[0].offset;
		gs->players[1].rect[1].x = gs->players[1].offset;

		if( ( i + start ) % timestep == 0 )
		{
			gs->ball.x += gs->ball.xv * ( timestep / 1000.f );
			gs->ball.y += gs->ball.yv * ( timestep / 1000.f );

			gs->ball.rect.x = gs->ball.x;
			gs->ball.rect.y = gs->ball.y;

			handle_ball( &gs->ball, &gs->players[0], &gs->players[1] );
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
int net_simple_packet( struct net *pnet, struct simple_packet *packet, IPaddress to )
{
	return net_send( pnet, (void*)packet, sizeof(struct simple_packet), to );
}

int net_send_update( struct net *pnet, IPaddress to, struct gamestate *gs )
{
	struct update_packet up;
	up.state = *gs;
	up.type = PACKET_UPDATE;
	return net_send( pnet, &up, sizeof(struct update_packet), to );
}

int net_send_cmd_buf( struct net *pnet, IPaddress to, struct cmd_buf *in )
{
	struct cmd_packet *cp;
	struct cmd_net_buf *nb;
	unsigned size;
	int err;

	size = sizeof( struct cmd_packet ) + ( sizeof( struct cmd ) * ( in->len - 1 ) );
	cp = (struct cmd_packet *)malloc( size );
	nb = cmd_to_net( in );

	memcpy( &cp->buf, nb, sizeof( struct cmd_net_buf ) + ( sizeof( struct cmd ) * ( in->len - 1 ) ) );

	cp->type = PACKET_CMD;

	err = net_send( pnet, cp, size, to );

	free( nb );
	return err;
}

/*int net_thread( void *ptr )
{
	uint8_t buf[MAXPACKETSIZE];
	int buflen = MAXPACKETSIZE;
	int recvbytes;
	struct net *pnet = (struct net*)ptr;
	IPaddress ip;
	struct simple_packet *sp;
	struct update_packet *up;
	struct cmd_packet *cp;
	int i;

	while( running )
	{
		if( !net_recv( pnet, (void*)buf, buflen, &recvbytes, &ip ) )
		{
			continue;
		}

		sp = (struct simple_packet*)buf;

		printf( "Packet received!\n" );  

		switch( pnet->state )
		{
		case NET_STATE_WAIT_SYN:
			if( sp->type == PACKET_SYN )
			{
				printf( "SYN received, sending ACK\n" );
				pnet->addr.host = ip.host;
				net_simple_packet( pnet, PACKET_ACK, pnet->addr );
				pnet->state = NET_STATE_WAIT_SYNACK;
			}
			break;
		case NET_STATE_WAIT_ACK:
			if( sp->type == PACKET_ACK )
			{
				printf( "ACK received, sending SYNACK\n" );
				net_simple_packet( pnet, PACKET_SYNACK, pnet->addr );
				pnet->state = NET_STATE_GAME;
			}
			break;
		case NET_STATE_WAIT_SYNACK:
			if( sp->type == PACKET_SYNACK )
			{
				printf( "Got SYNACK\n" );
				pnet->state = NET_STATE_GAME;
				net_send_update( pnet, pnet->addr );
			}
			break;
		case NET_STATE_GAME:
			if( pnet->state == NET_JOIN && sp->type == PACKET_UPDATE )
			{
				up = (struct update_packet*)sp;
				local_state = up->state;
			}

			if( pnet->state == NET_HOST && sp->type == PACKET_CMD )
			{
				cp = (struct cmd_packet*)sp;
				for( i = 0; i < cp->buf.len; i++ )
				{
					add_to_cmd_buf( local_cmd_buf, cp->buf.cmds[i] );
				}
			}
			break;
		}

		memset( buf, 0, MAXPACKETSIZE );
	}

	return 0;
}*/

/*void net_create_thread( struct net *pnet )
{
	SDL_CreateThread(net_thread, "net", pnet);
}*/

int main( int argc, char **argv )
{
	struct simple_packet sp;

	sp.type = PACKET_SYN;
	running = 1;

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
		}
		else
		{
			printf( "Unknown argument!\n" );
			return 1;
		}

		if( !net_init() )
		{
			printf( "Could not init network, exiting!\n" );
			return 1;
		}

		if( net.type == NET_JOIN )
		{
			SDLNet_ResolveHost( &net.addr, argv[2], PORTNUM );
		}

		net_wait_for_game( &net );
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
	
	switch( net.type )
	{
	case NET_LOCAL:
		local_loop();
		break;

	case NET_HOST:
		server_loop();
		break;

	case NET_JOIN:
		client_loop();
		break;
	}

	quit();
	return 0;
}