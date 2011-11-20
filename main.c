#include <stdio.h>
#include <stdint.h>
#include <unistd.h>
#include <ftdi.h>
#include <pthread.h>
#include <math.h>
#include <signal.h>

#define CLOCK 0x01		// ORANGE
#define DATA  0x02		// YELLOW

static unsigned char PORT = 0;
static struct ftdi_context ftdic;

enum {
	CPU_LOAD,
	COLOR_CYCLE,
	PULSE,
	SET_RGB,
};

static int mode  = CPU_LOAD;
static int speed = 100;
static int beat;
static int throb;
static int simulate;
static int debug;
static int red   = 255;
static int green = 255;
static int blue  = 255;
static volatile int stop;

static void writebyte( int byte, unsigned char *out, int *count )
{
	int i;
	for ( i = 0; i < 8; i++ ) {
		if ( byte & 0x80 )
			PORT |= DATA;
		else
			PORT &= ~DATA;

		out[*count] = PORT;
		( *count )++;
		PORT |= CLOCK;
		out[*count] = PORT;
		( *count )++;
		PORT &= ~CLOCK;
		out[*count] = PORT;
		( *count )++;

		byte = byte << 1;
	}
}

static void rgb_set( int r, int g, int b )
{
	if( simulate || debug ) {
		printf("\rR %3d  G %3d  B %3d  ", r, g, b );
	}
	if( simulate ) {
		return;
	}
	unsigned char out[256];
	int count = 0;

	writebyte( r, out, &count );
	writebyte( g, out, &count );
	writebyte( b, out, &count );

	ftdi_write_data( &ftdic, out, count );
}

#define RED 255
#define GRN 255
#define BLU 255

static unsigned int colors[][3] = {
	{RED, 000, 000},
	{RED, GRN, 000},
	{000, GRN, 000},
	{000, GRN, BLU},
	{000, 000, BLU},
	{RED, 000, BLU},
};

#define MAX_COLOR (sizeof( colors ) / sizeof ( int[3] ) )
#define MAX_STEP  128
#define MAX_LEVEL 10

static int color = 0;
static int step  = 0;
static int level = MAX_LEVEL;

static void change_color( void )
{
	int next = color + 1;
	if ( next == MAX_COLOR )
		next = 0;

	unsigned int r1 = colors[color][0];
	unsigned int g1 = colors[color][1];
	unsigned int b1 = colors[color][2];
	unsigned int r2 = colors[next][0];
	unsigned int g2 = colors[next][1];
	unsigned int b2 = colors[next][2];

	unsigned int r = ( r1 * ( MAX_STEP - step ) / MAX_STEP + r2 * step / MAX_STEP ) * level / MAX_LEVEL;
	unsigned int g = ( g1 * ( MAX_STEP - step ) / MAX_STEP + g2 * step / MAX_STEP ) * level / MAX_LEVEL;
	unsigned int b = ( b1 * ( MAX_STEP - step ) / MAX_STEP + b2 * step / MAX_STEP ) * level / MAX_LEVEL;

	rgb_set( r, g, b );

	if ( ++step == MAX_STEP ) {
		step = 0;
		color = next;
	}
}

int get_load( int interval )
{
	long double a[4], b[4];
	FILE *fp;

	for ( ;; ) {
		fp = fopen( "/proc/stat", "r" );
		fscanf( fp, "%*s %Lf %Lf %Lf %Lf", &a[0], &a[1], &a[2], &a[3] );
		fclose( fp );

		usleep( interval );

		fp = fopen( "/proc/stat", "r" );
		fscanf( fp, "%*s %Lf %Lf %Lf %Lf", &b[0], &b[1], &b[2], &b[3] );
		fclose( fp );

		return 100 * ( ( b[0] + b[1] + b[2] ) - ( a[0] + a[1] + a[2] ) ) / ( ( b[0] + b[1] + b[2] + b[3] ) - ( a[0] + a[1] + a[2] + a[3] ) );
	}
}

static void usage( void )
{
	fprintf(stderr, "bacon [MODE] [OPTIONS..]\n");
	fprintf(stderr, "MODE:\n");
	fprintf(stderr, "  -l           cpu load (default)\n");
	fprintf(stderr, "  -c           color cycle\n");
	fprintf(stderr, "  -p           pulse\n");
	fprintf(stderr, "  -o           one shot set color\n");
	fprintf(stderr, "OPTIONS:\n");
	fprintf(stderr, "  -b           heart beat\n");
	fprintf(stderr, "  -t           throb\n");
	fprintf(stderr, "  -s SPEED     cycle speed\n");
	fprintf(stderr, "  -r RRGGBB    set (hex) RGB value\n");
	fprintf(stderr, "  -d           debug output\n");
	fprintf(stderr, "  -S           simulate missing hardware\n");
	fprintf(stderr, "  -h           help\n");
	fprintf(stderr, "\n");
}

void parse_opt( int argc, char **argv )
{
	int opt;
	int rgb;
	while ((opt = getopt(argc, argv, "lcs:r:poSdbth")) != -1) {
		switch (opt) {
		case 'l':
			mode = CPU_LOAD;
			break;
		case 'c':
			mode = COLOR_CYCLE;
			break;
		case 's':
			speed = atoi(optarg);
			break;
		case 'r':
			sscanf(optarg, "%06x", &rgb );
			red   = (rgb >> 16) & 0xFF;
			green = (rgb >>  8) & 0xFF;
			blue  =  rgb        & 0xFF;
			break;
		case 'o':
			mode = SET_RGB;
			break;
		case 'p':
			mode = PULSE;
			break;
		case 'S':
			simulate = 1;
			break;
		case 'd':
			debug = 1;
			break;
		case 'b':
			beat = 1;
			break;
		case 't':
			throb = 1;
			break;
		case 'h':
		default: /* '?' */
			usage();
			exit(-1);
		}
	}

	if( !speed )
		speed = 1;
		
	if( debug )
		printf("mode %d  speed %d  beat %d  rgb %02X %02X %02X\n", 
		        mode, speed, beat, red, green, blue);
}

static int cpu_load;

void set_load( int load, int blue, int level )
{
	int r = level *         load   / 100;
	int g = level * ( 100 - load ) / 100;

	rgb_set( r, g, blue );
}

void *get_cpu_load( void *data ) 
{
	int wait = (int)data;
	
	for ( ;; ) {
		cpu_load = get_load( wait );
	}
	
	return NULL;	
}

void do_cpu_load( void )
{
	int load = 0;
	int tick;
	pthread_t cpu_thread;

	pthread_create( &cpu_thread, NULL, get_cpu_load, (void*)300000);
	
	while (!stop) {
		if( cpu_load > load ) {
			load ++;
		} else if ( cpu_load < load ) {
			load --;
		}	
		int blue  = beat  ? 8 * (sin( (double)tick++ / 10 ) + 1) : 0;
		int t_rate = (100 - cpu_load) / 5;
		if(t_rate < 5) 
			t_rate = 5;
		int level = throb ? 191 + 32 * (sin( (double)tick++ / (double)t_rate ) + 1) : 255;
		if( debug ) 
			printf( "cpu load: %3d%%  %3d%%  %3d  t %2d  lvl %3d\n", cpu_load, load, blue, t_rate, level );
		set_load( load, blue, level );
		do_sleep( 10000 );
	}
}

void do_pulse( void )
{
	int tick;
	while (!stop) {
		double lvl = (sin( (double)tick++ / speed ) + 1) / 2;
		int r = red   * lvl;
		int g = green * lvl;
		int b = blue  * lvl;

		rgb_set( r, g, b );

		usleep( 10000 );
	}
}

void do_color_cycle( void )
{
	while (!stop) {
		change_color(  );
		usleep( 1000000 / speed );
	}	
}

void signal_handler( int signal )
{
	stop = 1;
}

int main( int argc, char **argv )
{
	parse_opt( argc, argv );
	
	if( !simulate ) {
		if ( ftdi_init( &ftdic ) < 0 ) {
			fprintf( stderr, "ftdi_init failed\n" );
			return -1;
		}

		int ret;
		if ( ( ret = ftdi_usb_open_desc( &ftdic, 0x0403, 0x6001, "TTL232R-3V3", NULL ) ) < 0 ) {
			fprintf( stderr, "unable to open ftdi device: %d (%s)\n", ret, ftdi_get_error_string( &ftdic ) );
			return -1;
		}

		// Read out FTDIChip-ID of R type chips
		if ( ftdic.type == TYPE_R ) {
			unsigned int chipid;
			if( !ftdi_read_chipid( &ftdic, &chipid ) ) {
				if( debug )
					printf( "FTDI chipid: %X\n", chipid );
			}
		}

		/* Enable bitbang mode with a single output line */
		ftdi_enable_bitbang( &ftdic, CLOCK | DATA );
	}
	
	signal( SIGINT, signal_handler );
	
	switch( mode ) {
	case CPU_LOAD:
		do_cpu_load();
		break;
	case COLOR_CYCLE:
		do_color_cycle();
		break;
	case PULSE:
		do_pulse();
		break;
	case SET_RGB:
		rgb_set( red, green, blue );
		usleep( 100000 );
		rgb_set( red, green, blue );
		break;
	}

	if( simulate ) {
		printf("\n");
	} else {
		int ret;
		if ( ( ret = ftdi_usb_close( &ftdic ) ) < 0 ) {
			fprintf( stderr, "unable to close ftdi device: %d (%s)\n", ret, ftdi_get_error_string( &ftdic ) );
			return -1;
		}

		ftdi_deinit( &ftdic );
	}
	
	return 0;
}
