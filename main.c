#include <stdio.h>
#include <stdint.h>
#include <ftdi.h>

#define CLOCK 0x01		// ORANGE
#define DATA  0x02		// YELLOW

static unsigned char PORT = 0;
static struct ftdi_context ftdic;

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

void set_load( int load )
{
	int r = 255 *         load   / 100;
	int g = 255 * ( 100 - load ) / 100;
	int b = 0;

	rgb_set( r, g, b );
}

int main( void )
{
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
			printf( "FTDI chipid: %X\n", chipid );
		}
	}

	rgb_init();

	for ( ;; ) {
#if 1
		int load = get_load( 300000 );
		printf( "cpu load: %2d%%\n", load );
		set_load( load );
#else
		change_color(  );
		usleep( 10000 );
#endif
	}

	if ( ( ret = ftdi_usb_close( &ftdic ) ) < 0 ) {
		fprintf( stderr, "unable to close ftdi device: %d (%s)\n", ret, ftdi_get_error_string( &ftdic ) );
		return -1;
	}

	ftdi_deinit( &ftdic );

	return 0;
}
