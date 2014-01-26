#include <SDL2/SDL.h>
#include <stdio.h>
#include <stdbool.h>
#include <math.h>

#define dist_form( x, y ) ( sqrt( pow( x, 2 ) + pow( y, 2 ) ) )

bool init();
void quit();
void input( SDL_Event );
void loop();
void white_rect( SDL_Rect * );
void reset_ball();
void handle_ball();

struct player
{
	SDL_Rect rect[2];
	float offset;
	bool status[2];
	int score;
};

struct ball
{
	float x, y;
	float xv, yv;
	SDL_Rect rect;
	bool colliding;
};

const char *WINDOW_TITLE = "Pong";
const int WIN_WIDTH = 640;
const int WIN_HEIGHT = 480;
const int PADDLE_HEIGHT = 125;
const int PADDLE_WIDTH = 25;
const int PADDLE_SPEED = 200;
const int BALL_SIZE = 20;
const int BALL_SPEED = 400;
const int PADDLE_STRENGTH = 300;

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
	running = true;

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

int main( int argc, char **argv )
{
	if( !init() )
	{
		printf( "an error occurred\n" );
		quit();
		return 1;
	}

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
	ball.xv = -BALL_SPEED;

	loop();

	quit();
	return 0;
}
