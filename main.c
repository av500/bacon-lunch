#include <stdio.h>
#include <stdint.h>
#include <unistd.h>
#include <ftdi.h>
#include <pthread.h>
#include <math.h>

#define CLOCK 0x01		// ORANGE
#define DATA  0x02		// YELLOW

static unsigned char PORT = 0;
static struct ftdi_context ftdic;

enum {
	CPU_LOAD,
	COLOR_CYCLE,
};

static int mode  = CPU_LOAD;
static int speed = 100;
static int beat;
static int debug;

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
	unsigned char out[256];
	int count = 0;

	writebyte( r, out, &count );
	writebyte( g, out, &count );
	writebyte( b, out, &count );

	ftdi_write_data( &ftdic, out, count );
}

static void rgb_init( void )
{
	/* Enable bitbang mode with a single output line */
	ftdi_enable_bitbang( &ftdic, CLOCK | DATA );

	rgb_set( 0, 0, 0 );
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
	fprintf(stderr, "bacon [OPTION..]\n");
	fprintf(stderr, "  -l        cpu load\n");
	fprintf(stderr, "  -b        heart beat\n");
	fprintf(stderr, "  -c        color cycle\n");
	fprintf(stderr, "  -s SPEED  cycle speed\n");
	fprintf(stderr, "  -d        debug output\n");
	fprintf(stderr, "  -h        help\n");
	fprintf(stderr, "\n");
}

void parse_opt( int argc, char **argv )
{
	int opt;
	while ((opt = getopt(argc, argv, "lcs:db")) != -1) {
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
		case 'd':
			debug = 1;
			break;
		case 'b':
			beat = 1;
			break;
		default: /* '?' */
			usage();
			exit(-1);
		}
	}

	if( !speed )
		speed = 1;
		
	if( debug )
		printf("mode %d  speed %d  beat %d\n", mode, speed, beat);
}

static int cpu_load;

void set_load( int load, int blue )
{
	int r = 255 *         load   / 100;
	int g = 255 * ( 100 - load ) / 100;

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
	
	for ( ;; ) {
		if( cpu_load > load ) {
			load ++;
		}
		else if ( cpu_load < load ) {
			load --;
		}	
		int blue = beat ? 8 * (sin( (double)tick++ / 10 ) + 1) : 0;
		if( debug ) 
			printf( "cpu load: %3d%%  %3d%%  %3d\n", cpu_load, load, blue );
		set_load( load, blue );
		usleep( 10000 );
	}
}

int main( int argc, char **argv )
{
	parse_opt( argc, argv );
	
	int ret;
	if ( ftdi_init( &ftdic ) < 0 ) {
		fprintf( stderr, "ftdi_init failed\n" );
		return -1;
	}

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

	rgb_init();

	if( mode == CPU_LOAD ) {
		do_cpu_load();
	} else {
		for ( ;; ) {
			change_color(  );
			usleep( 1000000 / speed );
		}	
	}

	if ( ( ret = ftdi_usb_close( &ftdic ) ) < 0 ) {
		fprintf( stderr, "unable to close ftdi device: %d (%s)\n", ret, ftdi_get_error_string( &ftdic ) );
		return -1;
	}

	ftdi_deinit( &ftdic );

	return 0;
}
