#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <ctime>

#include "bitops.h"
#include "aricoder.h"
#include "ppnmtbl.h"
#include "ppnmbitlen.h"

#if defined BUILD_DLL // define BUILD_DLL from the compiler options if you want to compile a DLL!
	#define BUILD_LIB
#endif

#if defined BUILD_LIB // define BUILD_LIB from the compiler options if you want to compile a library!
	#include "packpnmlib.h"
#endif

#define INTERN static

#define INIT_MODEL_S(a,b,c) new model_s( a, b, c, 255 )
#define INIT_MODEL_B(a,b)   new model_b( a, b, 255 )

#define ABS(v1)			( (v1 < 0) ? -v1 : v1 )
#define ABSDIFF(v1,v2)	( (v1 > v2) ? (v1 - v2) : (v2 - v1) )
#define ROUND_F(v1)		( (v1 < 0) ? (int) (v1 - 0.5) : (int) (v1 + 0.5) )
#define CLAMPED(l,h,v)	( ( v < l ) ? l : ( v > h ) ? h : v )
#define SHORT_BE(d)		( ( ( (d)[0] & 0xFF ) << 8 ) | ( (d)[1] & 0xFF ) )
#define SHORT_LE(d)		( ( (d)[0] & 0xFF ) | ( ( (d)[1] & 0xFF ) << 8 ) ) 
#define INT_BE(d)		( ( SHORT_BE( d ) << 16 ) | SHORT_BE( d + 2 ) )
#define INT_LE(d)		( SHORT_LE( d ) | ( SHORT_LE( d + 2 ) << 16 ) )

#define MEM_ERRMSG	"out of memory error"
#define FRD_ERRMSG	"could not read file / file not found"
#define FWR_ERRMSG	"could not write file / file write-protected"
#define MSG_SIZE	128
#define BARLEN		36


/* -----------------------------------------------
	pjg_model class
	----------------------------------------------- */
	
class pjg_model {
	public:
		model_s* len;
		model_b* sgn;
		model_b* res;
		pjg_model( int pnmax, int nctx = 2 ) {
			int ml = BITLENB16N( pnmax );
			len = INIT_MODEL_S( ml + 1, ml + 1, nctx );
			sgn = INIT_MODEL_B( 16, 1 );
			res = INIT_MODEL_B( ml + 1, 2 );
		};
		~pjg_model() {
			delete( len );
			delete( sgn );
			delete( res );
		};
};


/* -----------------------------------------------
	cmp mask class
	----------------------------------------------- */
	
class cmp_mask {
	public:
		unsigned int m;
		int p, s;
		cmp_mask( unsigned int mask = 0 ) {
			if ( mask == 0 ) m = p = s = 0;
			else {
				for ( p = 0; ( mask & 1 ) == 0; p++, mask >>= 1 );
				m = mask;
				for ( s = 0; ( mask & 1 ) == 1; s++, mask >>= 1 );
				if ( mask > 0 ) { m = 0; p = -1; s = -1; };
			}
		};
		~cmp_mask() {};
};


/* -----------------------------------------------
	function declarations: main interface
	----------------------------------------------- */

#if !defined( BUILD_LIB )
INTERN void initialize_options( int argc, char** argv );
INTERN void process_ui( void );
INTERN inline const char* get_status( bool (*function)() );
INTERN void show_help( void );
#endif
INTERN void process_file( void );
INTERN void execute( bool (*function)() );


/* -----------------------------------------------
	function declarations: main functions
	----------------------------------------------- */

#if !defined( BUILD_LIB )
INTERN bool check_file( void );
INTERN bool swap_streams( void );
INTERN bool compare_output( void );
#endif
INTERN bool reset_buffers( void );
INTERN bool pack_ppn( void );
INTERN bool unpack_ppn( void );


/* -----------------------------------------------
	function declarations: side functions
	----------------------------------------------- */

INTERN bool ppn_encode_imgdata_rgba( aricoder* enc, iostream* stream );
INTERN bool ppn_decode_imgdata_rgba( aricoder* dec, iostream* stream );
INTERN bool ppn_encode_imgdata_mono( aricoder* enc, iostream* stream );
INTERN bool ppn_decode_imgdata_mono( aricoder* dec, iostream* stream );
INTERN bool ppn_encode_imgdata_palette( aricoder* enc, iostream* stream );
INTERN bool ppn_decode_imgdata_palette( aricoder* dec, iostream* stream );
INTERN inline void ppn_encode_pjg( aricoder* enc, pjg_model* mod, int** val, int** err, int ctx3 );
INTERN inline void ppn_decode_pjg( aricoder* dec, pjg_model* mod, int** val, int** err, int ctx3 );
INTERN inline int get_context_mono( int x, int y, int** val );
INTERN inline int plocoi( int a, int b, int c );
INTERN inline int pnm_read_line( iostream* stream, int** line );
INTERN inline int pnm_write_line( iostream* stream, int** line );
INTERN inline int hdr_decode_line_rle( iostream* stream, int** line );
INTERN inline int hdr_encode_line_rle( iostream* stream, int** line );
INTERN inline void rgb_process( unsigned int* rgb );
INTERN inline void rgb_unprocess( unsigned int* rgb );
INTERN inline void identify( const char* id, int* ft, int* st );
INTERN inline char* scan_header( iostream* stream );
INTERN inline char* scan_header_pnm( iostream* stream );
INTERN inline char* scan_header_bmp( iostream* stream );
INTERN inline char* scan_header_hdr( iostream* stream );

	
/* -----------------------------------------------
	function declarations: miscelaneous helpers
	----------------------------------------------- */

#if !defined( BUILD_LIB )
INTERN inline void progress_bar( int current, int last );
INTERN inline char* create_filename( const char* base, const char* extension );
INTERN inline char* unique_filename( const char* base, const char* extension );
INTERN inline void set_extension( const char* filename, const char* extension );
INTERN inline void add_underscore( char* filename );
#endif
INTERN inline bool file_exists( const char* filename );


/* -----------------------------------------------
	function declarations: developers functions
	----------------------------------------------- */

// these are developers functions, they are not needed
// in any way to compress or decompress files
#if !defined(BUILD_LIB) && defined(DEV_BUILD)
INTERN bool write_errfile( void );
INTERN bool dump_pgm( void );
INTERN bool dump_info( void );
#endif

/* -----------------------------------------------
	global variables: library only variables
	----------------------------------------------- */
#if defined(BUILD_LIB)
INTERN int lib_in_type  = -1;
INTERN int lib_out_type = -1;
#endif


/* -----------------------------------------------
	global variables: data storage
	----------------------------------------------- */

INTERN int imgwidth;	// width of image
INTERN int imgheight;	// height of image
INTERN int imgwidthv;	// visible width of image
INTERN int imgbpp;		// bit per pixel
INTERN int cmpc;		// component count
INTERN int endian_l;	// endianness of image data
INTERN unsigned int pnmax; // maximum pixel value (PPM/PGM only!)
INTERN cmp_mask* cmask[5]; // masking info for components
INTERN int bmpsize;		// file size according to header


/* -----------------------------------------------
	global variables: info about files
	----------------------------------------------- */
	
INTERN char*  ppnfilename = NULL;	// name of compressed file
INTERN char*  pnmfilename = NULL;	// name of uncompressed file
INTERN int    ppnfilesize;			// size of compressed file
INTERN int    pnmfilesize;			// size of uncompressed file
INTERN int    filetype;				// type of current file
INTERN int    subtype;				// sub type of file
INTERN iostream* str_in  = NULL;	// input stream
INTERN iostream* str_out = NULL;	// output stream

#if !defined( BUILD_LIB )
INTERN iostream* str_str = NULL;	// storage stream

INTERN char** filelist = NULL; 		// list of files to process 
INTERN int    file_cnt = 0;			// count of files in list
INTERN int    file_no  = 0;			// number of current file

INTERN char** err_list = NULL;		// list of error messages 
INTERN int*   err_tp   = NULL;		// list of error types
#endif


/* -----------------------------------------------
	global variables: messages
	----------------------------------------------- */

INTERN char errormessage [ 128 ];
INTERN bool (*errorfunction)();
INTERN int  errorlevel;
// meaning of errorlevel:
// -1 -> wrong input
// 0 -> no error
// 1 -> warning
// 2 -> fatal error


/* -----------------------------------------------
	global variables: settings
	----------------------------------------------- */

INTERN bool use_rle    = 0;		// use RLE compression for HDR output
#if !defined( BUILD_LIB )
INTERN int  verbosity  = -1;	// level of verbosity
INTERN bool overwrite  = false;	// overwrite files yes / no
INTERN bool wait_exit  = true;	// pause after finished yes / no
INTERN int  verify_lv  = 0;		// verification level ( none (0), simple (1), detailed output (2) )
INTERN int  err_tol    = 1;		// error threshold ( proceed on warnings yes (2) / no (1) )

INTERN bool developer  = false;	// allow developers functions yes/no
INTERN int  action     = A_COMPRESS; // what to do with files

INTERN FILE*  msgout   = stdout;	// stream for output of messages
INTERN bool   pipe_on  = false;	// use stdin/stdout instead of filelist
#else
INTERN int  err_tol    = 1;		// error threshold ( proceed on warnings yes (2) / no (1) )
INTERN int  action     = A_COMPRESS; // what to do with files
#endif


/* -----------------------------------------------
	global variables: info about program
	----------------------------------------------- */

INTERN const unsigned char appversion = 16;
INTERN const char*  subversion   = "c";
INTERN const char*  apptitle     = "packPNM";
INTERN const char*  appname      = "packPNM";
INTERN const char*  versiondate  = "01/15/2014";
INTERN const char*  author       = "Matthias Stirner";
#if !defined(BUILD_LIB)
INTERN const char*  website      = "http://www.elektronik.htw-aalen.de/packjpg/";
INTERN const char*  email        = "packjpg (at) htw-aalen.de";
INTERN const char*	copyright    = "2006-2014 HTW Aalen University & Matthias Stirner";
INTERN const char*  ppn_ext      = "ppn";
#endif


/* -----------------------------------------------
	main-function
	----------------------------------------------- */

#if !defined(BUILD_LIB)
int main( int argc, char** argv )
{	
	sprintf( errormessage, "no errormessage specified" );
	
	clock_t begin, end;
	
	int error_cnt = 0;
	int warn_cnt  = 0;
	
	double acc_pnmsize = 0;
	double acc_ppnsize = 0;
	
	int kbps;
	double cr;
	double total;
	
	errorlevel = 0;
	
	
	// read options from command line
	initialize_options( argc, argv );
	
	// write program info to screen
	fprintf( msgout,  "\n--> %s v%i.%i%s (%s) by %s <--\n",
			apptitle, appversion / 10, appversion % 10, subversion, versiondate, author );
	fprintf( msgout, "Copyright %s\nAll rights reserved\n\n", copyright );
	
	// check if user input is wrong, show help screen if it is
	if ( ( file_cnt == 0 ) ||
		( ( !developer ) && ( (action != A_COMPRESS) || (verify_lv > 1) ) ) ) {
		show_help();
		return -1;
	}
	
	// (re)set program has to be done first
	reset_buffers();
	
	// process file(s) - this is the main function routine
	begin = clock();
	for ( file_no = 0; file_no < file_cnt; file_no++ ) {	
		// process current file
		process_ui();
		// store error message and type if any
		if ( errorlevel > 0 ) {
			err_list[ file_no ] = (char*) calloc( MSG_SIZE, sizeof( char ) );
			err_tp[ file_no ] = errorlevel;
			if ( err_list[ file_no ] != NULL )
				strcpy( err_list[ file_no ], errormessage );
		}
		// count errors / warnings / file sizes
		if ( errorlevel >= err_tol ) error_cnt++;
		else {
			if ( errorlevel == 1 ) warn_cnt++;
			acc_pnmsize += pnmfilesize;
			acc_ppnsize += ppnfilesize;
		}
	}
	end = clock();
	
	// errors summary: only needed for -v2 or progress bar
	if ( ( verbosity == -1 ) || ( verbosity == 2 ) ) {
		// print summary of errors to screen
		if ( error_cnt > 0 ) {
			fprintf( stderr, "\n\nfiles with errors:\n" );
			fprintf( stderr, "------------------\n" );
			for ( file_no = 0; file_no < file_cnt; file_no++ ) {
				if ( err_tp[ file_no ] >= err_tol ) {
					fprintf( stderr, "%s (%s)\n", filelist[ file_no ], err_list[ file_no ] );
				}
			}
		}
		// print summary of warnings to screen
		if ( warn_cnt > 0 ) {
			fprintf( stderr, "\n\nfiles with warnings:\n" );
			fprintf( stderr, "------------------\n" );
			for ( file_no = 0; file_no < file_cnt; file_no++ ) {
				if ( err_tp[ file_no ] == 1 ) {
					fprintf( stderr, "%s (%s)\n", filelist[ file_no ], err_list[ file_no ] );
				}
			}
		}
	}
	
	// show statistics
	fprintf( msgout,  "\n\n-> %i file(s) processed, %i error(s), %i warning(s)\n",
		file_cnt, error_cnt, warn_cnt );
	if ( ( file_cnt > error_cnt ) && ( verbosity != 0 ) &&
	 ( action == A_COMPRESS ) ) {
		acc_pnmsize /= 1024.0; acc_ppnsize /= 1024.0;
		total = (double) ( end - begin ) / CLOCKS_PER_SEC; 
		kbps  = ( total > 0 ) ? ( acc_pnmsize / total ) : acc_pnmsize;
		cr    = ( acc_pnmsize > 0 ) ? ( 100.0 * acc_ppnsize / acc_pnmsize ) : 0;
		
		fprintf( msgout,  " -------------------------------- \n" );
		if ( total >= 0 ) {
			fprintf( msgout,  " total time        : %8.2f sec\n", total );
			fprintf( msgout,  " avrg. kbyte per s : %8i kbps\n", kbps );
		}
		else {
			fprintf( msgout,  " total time        : %8s sec\n", "N/A" );
			fprintf( msgout,  " avrg. kbyte per s : %8s kbps\n", "N/A" );
		}
		fprintf( msgout,  " avrg. comp. ratio : %8.2f %%\n", cr );		
		fprintf( msgout,  " -------------------------------- \n" );
	}
	
	// pause before exit
	if ( wait_exit && ( msgout != stderr ) ) {
		fprintf( msgout, "\n\n< press ENTER >\n" );
		fgetc( stdin );
	}
	
	
	return 0;
}
#endif

/* ----------------------- Begin of library only functions -------------------------- */

/* -----------------------------------------------
	DLL export converter function
	----------------------------------------------- */
	
#if defined(BUILD_LIB)
EXPORT bool ppnlib_convert_stream2stream( char* msg )
{
	// process in main function
	return ppnlib_convert_stream2mem( NULL, NULL, msg ); 
}
#endif


/* -----------------------------------------------
	DLL export converter function
	----------------------------------------------- */

#if defined(BUILD_LIB)
EXPORT bool ppnlib_convert_file2file( char* in, char* out, char* msg )
{
	// init streams
	ppnlib_init_streams( (void*) in, 0, 0, (void*) out, 0 );
	
	// process in main function
	return ppnlib_convert_stream2mem( NULL, NULL, msg ); 
}
#endif


/* -----------------------------------------------
	DLL export converter function
	----------------------------------------------- */
	
#if defined(BUILD_LIB)
EXPORT bool ppnlib_convert_stream2mem( unsigned char** out_file, unsigned int* out_size, char* msg )
{
	clock_t begin, end;
	int total;
	float cr;	
	
	
	// (re)set buffers
	reset_buffers();
	action = A_COMPRESS;
	
	// main compression / decompression routines
	begin = clock();
	
	// process one file
	process_file();
	
	// fetch pointer and size of output (only for memory output)
	if ( ( errorlevel < err_tol ) && ( lib_out_type == 1 ) &&
		 ( out_file != NULL ) && ( out_size != NULL ) ) {
		*out_size = str_out->getsize();
		*out_file = str_out->getptr();
	}
	
	// close iostreams
	if ( str_in  != NULL ) delete( str_in  ); str_in  = NULL;
	if ( str_out != NULL ) delete( str_out ); str_out = NULL;
	
	end = clock();
	
	// copy errormessage / remove files if error (and output is file)
	if ( errorlevel >= err_tol ) {
		if ( lib_out_type == 0 ) {
			if ( filetype == F_PNM ) {
				if ( file_exists( ppnfilename ) ) remove( ppnfilename );
			} else if ( filetype == F_PPN ) {
				if ( file_exists( pnmfilename ) ) remove( pnmfilename );
			}
		}
		if ( msg != NULL ) strcpy( msg, errormessage );
		return false;
	}
	
	// get compression info
	total = (int) ( (double) (( end - begin ) * 1000) / CLOCKS_PER_SEC );
	cr    = ( pnmfilesize > 0 ) ? ( 100.0 * ppnfilesize / pnmfilesize ) : 0;
	
	// write success message else
	if ( msg != NULL ) {
		switch( filetype )
		{
			case F_PNM:
				sprintf( msg, "Compressed to %s (%.2f%%) in %ims",
					ppnfilename, cr, ( total >= 0 ) ? total : -1 );
				break;
			case F_PPN:
				sprintf( msg, "Decompressed to %s (%.2f%%) in %ims",
					pnmfilename, cr, ( total >= 0 ) ? total : -1 );
				break;	
		}
	}
	
	
	return true;
}
#endif


/* -----------------------------------------------
	DLL export init input (file/mem)
	----------------------------------------------- */
	
#if defined(BUILD_LIB)
EXPORT void ppnlib_init_streams( void* in_src, int in_type, int in_size, void* out_dest, int out_type )
{
	/* a short reminder about input/output stream types:
	
	if input is file
	----------------
	in_scr -> name of input file
	in_type -> 0
	in_size -> ignore
	
	if input is memory
	------------------
	in_scr -> array containg data
	in_type -> 1
	in_size -> size of data array
	
	if input is *FILE (f.e. stdin)
	------------------------------
	in_src -> stream pointer
	in_type -> 2
	in_size -> ignore
	
	vice versa for output streams! */
	
	char buffer[ 2 ];
	
	
	// (re)set errorlevel
	errorfunction = NULL;
	errorlevel = 0;
	pnmfilesize = 0;
	ppnfilesize = 0;
	
	// open input stream, check for errors
	str_in = new iostream( in_src, in_type, in_size, 0 );
	if ( str_in->chkerr() ) {
		sprintf( errormessage, "error opening input stream" );
		errorlevel = 2;
		return;
	}	
	
	// open output stream, check for errors
	str_out = new iostream( out_dest, out_type, 0, 1 );
	if ( str_out->chkerr() ) {
		sprintf( errormessage, "error opening output stream" );
		errorlevel = 2;
		return;
	}
	
	// free memory from filenames if needed
	if ( pnmfilename != NULL ) free( pnmfilename ); pnmfilename = NULL;
	if ( ppnfilename != NULL ) free( ppnfilename ); ppnfilename = NULL;
	
	// check input stream
	str_in->read( buffer, 1, 2 );
	
	// rewind (need to start from the beginning)
	if ( str_in->rewind() != 0 ) {
		sprintf( errormessage, FRD_ERRMSG );
		errorlevel = 2;
		return;
	}
	
	// check file id, determine filetype
	identify( buffer, &filetype, &subtype );	
	if ( filetype == F_UNK ) {
		// file is of unknown type
		sprintf( errormessage, "filetype of input stream is unknown" );
		errorlevel = 2;
		return;
	}
	else if ( filetype == F_PNM ) {
		// file is PBM/PGM/PPM
		// copy filenames
		pnmfilename = (char*) calloc( (  in_type == 0 ) ? strlen( (char*) in_src   ) + 1 : 32, sizeof( char ) );
		ppnfilename = (char*) calloc( ( out_type == 0 ) ? strlen( (char*) out_dest ) + 1 : 32, sizeof( char ) );
		strcpy( pnmfilename, (  in_type == 0 ) ? (char*) in_src   : "PNM in memory" );
		strcpy( ppnfilename, ( out_type == 0 ) ? (char*) out_dest : "PPN in memory" );
	}
	else if ( filetype == F_PPN ) {
		// file is PPN
		// copy filenames
		ppnfilename = (char*) calloc( (  in_type == 0 ) ? strlen( (char*) in_src   ) + 1 : 32, sizeof( char ) );
		pnmfilename = (char*) calloc( ( out_type == 0 ) ? strlen( (char*) out_dest ) + 1 : 32, sizeof( char ) );
		strcpy( ppnfilename, (  in_type == 0 ) ? (char*) in_src   : "PPN in memory" );
		strcpy( pnmfilename, ( out_type == 0 ) ? (char*) out_dest : "PNM in memory" );
	}
	
	// store types of in-/output
	lib_in_type  = in_type;
	lib_out_type = out_type;
}
#endif


/* -----------------------------------------------
	DLL export version information
	----------------------------------------------- */
	
#if defined(BUILD_LIB)
EXPORT const char* ppnlib_version_info( void )
{
	static char v_info[ 256 ];
	
	// copy version info to string
	sprintf( v_info, "--> %s library v%i.%i%s (%s) by %s <--",
			apptitle, appversion / 10, appversion % 10, subversion, versiondate, author );
			
	return (const char*) v_info;
}
#endif


/* -----------------------------------------------
	DLL export version information
	----------------------------------------------- */
	
#if defined(BUILD_LIB)
EXPORT const char* ppnlib_short_name( void )
{
	static char v_name[ 256 ];
	
	// copy version info to string
	sprintf( v_name, "%s v%i.%i%s",
			apptitle, appversion / 10, appversion % 10, subversion );
			
	return (const char*) v_name;
}
#endif

/* ----------------------- End of libary only functions -------------------------- */

/* ----------------------- Begin of main interface functions -------------------------- */


/* -----------------------------------------------
	reads in commandline arguments
	----------------------------------------------- */
	
#if !defined(BUILD_LIB)	
INTERN void initialize_options( int argc, char** argv )
{	
	int tmp_val;
	char** tmp_flp;
	int i;
	
	
	// get memory for filelist & preset with NULL
	filelist = (char**) calloc( argc, sizeof( char* ) );
	for ( i = 0; i < argc; i++ )
		filelist[ i ] = NULL;
	
	// preset temporary filelist pointer
	tmp_flp = filelist;
	
	
	// read in arguments
	while ( --argc > 0 ) {
		argv++;
		// switches begin with '-'
		if ( strcmp((*argv), "-p" ) == 0 ) {
			err_tol = 2;
		}
		else if ( strcmp((*argv), "-ver" ) == 0 ) {
			verify_lv = ( verify_lv < 1 ) ? 1 : verify_lv;
		}
		else if ( sscanf( (*argv), "-v%i", &tmp_val ) == 1 ){
			verbosity = tmp_val;
			verbosity = ( verbosity < 0 ) ? 0 : verbosity;
			verbosity = ( verbosity > 2 ) ? 2 : verbosity;			
		}
		else if ( strcmp((*argv), "-vp" ) == 0 ) {
			verbosity = -1;
		}
		else if ( strcmp((*argv), "-np" ) == 0 ) {
			wait_exit = false;
		}
		else if ( strcmp((*argv), "-o" ) == 0 ) {
			overwrite = true;
		}
		else if ( strcmp((*argv), "-rle" ) == 0 ) {
			use_rle = true;
		}
		#if defined(DEV_BUILD)
		else if ( strcmp((*argv), "-dev") == 0 ) {
			developer = true;
		}
		else if ( strcmp((*argv), "-test") == 0 ) {
			verify_lv = 2;
		}
		else if ( strcmp((*argv), "-dump") == 0 ) {
			action = A_PGM_DUMP;
		}
		else if ( strcmp((*argv), "-info") == 0 ) {
			action = A_NFO_DUMP;
		}
		else if (	( strcmp((*argv), "-ppn") == 0 ) ||
					( strcmp((*argv), "-pnm") == 0 ) ||
					( strcmp((*argv), "-comp") == 0) ) {
			action = A_COMPRESS;
		}
		#endif
		else if ( strcmp((*argv), "-") == 0 ) {
			// switch standard message out stream
			msgout = stderr;
			// use "-" as placeholder for stdin
			*(tmp_flp++) = (char*) "-";
		}
		else {
			// if argument is not switch, it's a filename
			*(tmp_flp++) = *argv;
		}		
	}
	
	// count number of files (or filenames) in filelist
	for ( file_cnt = 0; filelist[ file_cnt ] != NULL; file_cnt++ );
	
	// alloc arrays for error messages and types storage
	err_list = (char**) calloc( file_cnt, sizeof( char* ) );
	err_tp   = (int*) calloc( file_cnt, sizeof( int ) );
}
#endif


/* -----------------------------------------------
	UI for processing one file
	----------------------------------------------- */
	
#if !defined(BUILD_LIB)
INTERN void process_ui( void )
{
	clock_t begin, end;
	const char* actionmsg  = NULL;
	const char* errtypemsg = NULL;
	int total, bpms;
	float cr;	
	
	
	errorfunction = NULL;
	errorlevel = 0;
	pnmfilesize = 0;
	ppnfilesize = 0;	
	#if !defined(DEV_BUILD)
	action = A_COMPRESS;
	#endif
	
	// compare file name, set pipe if needed
	if ( ( strcmp( filelist[ file_no ], "-" ) == 0 ) && ( action == A_COMPRESS ) ) {
		pipe_on = true;
		filelist[ file_no ] = (char*) "STDIN";
	}
	else {		
		pipe_on = false;
	}
	
	if ( verbosity >= 0 ) { // standard UI
		fprintf( msgout,  "\nProcessing file %i of %i \"%s\" -> ",
					file_no + 1, file_cnt, filelist[ file_no ] );
		
		if ( verbosity > 1 )
			fprintf( msgout,  "\n----------------------------------------" );
		
		// check input file and determine filetype
		execute( check_file );
		
		// get specific action message
		switch ( action ) {
			case A_COMPRESS: ( filetype == F_PNM ) ? actionmsg = "Compressing" : actionmsg = "Decompressing";
				break;
			case A_PGM_DUMP: actionmsg = "Dumping"; break;
		}
		
		if ( verbosity < 2 ) fprintf( msgout, "%s -> ", actionmsg );
	}
	else { // progress bar UI
		// update progress message
		fprintf( msgout, "Processing file %2i of %2i ", file_no + 1, file_cnt );
		progress_bar( file_no, file_cnt );
		fprintf( msgout, "\r" );
		execute( check_file );
	}
	fflush( msgout );
	
	
	// main function routine
	begin = clock();
	
	// streams are initiated, start processing file
	process_file();
	
	// close iostreams
	if ( str_in  != NULL ) delete( str_in  ); str_in  = NULL;
	if ( str_out != NULL ) delete( str_out ); str_out = NULL;
	if ( str_str != NULL ) delete( str_str ); str_str = NULL;
	// delete if broken or if output not needed
	if ( ( !pipe_on ) && ( ( errorlevel >= err_tol ) || ( action != A_COMPRESS ) ) ) {
		if ( filetype == F_PNM ) {
			if ( file_exists( ppnfilename ) ) remove( ppnfilename );
		} else if ( filetype == F_PPN ) {
			if ( file_exists( pnmfilename ) ) remove( pnmfilename );
		}
	}
	
	end = clock();	
	
	// speed and compression ratio calculation
	total = (int) ( (double) (( end - begin ) * 1000) / CLOCKS_PER_SEC );
	bpms  = ( total > 0 ) ? ( pnmfilesize / total ) : pnmfilesize;
	cr    = ( pnmfilesize > 0 ) ? ( 100.0 * ppnfilesize / pnmfilesize ) : 0;

	
	if ( verbosity >= 0 ) { // standard UI
		if ( verbosity > 1 )
			fprintf( msgout,  "\n----------------------------------------" );
		
		// display success/failure message
		switch ( verbosity ) {
			case 0:			
				if ( errorlevel < err_tol ) {
					if ( action == A_COMPRESS ) fprintf( msgout,  "%.2f%%", cr );
					else fprintf( msgout, "DONE" );
				}
				else fprintf( msgout,  "ERROR" );
				if ( errorlevel > 0 ) fprintf( msgout,  "\n" );
				break;
			
			case 1:
				fprintf( msgout, "%s\n",  ( errorlevel < err_tol ) ? "DONE" : "ERROR" );
				break;
			
			case 2:
				if ( errorlevel < err_tol ) fprintf( msgout,  "\n-> %s OK\n", actionmsg );
				else  fprintf( msgout,  "\n-> %s ERROR\n", actionmsg );
				break;
		}
		
		// set type of error message
		switch ( errorlevel ) {
			case 0:	errtypemsg = "none"; break;
			case 1: ( err_tol > 1 ) ?  errtypemsg = "warning (ignored)" : errtypemsg = "warning (skipped file)"; break;
			case 2: errtypemsg = "fatal error"; break;
		}
		
		// error/ warning message
		if ( errorlevel > 0 ) {
			fprintf( msgout, " %s -> %s:\n", get_status( errorfunction ), errtypemsg  );
			fprintf( msgout, " %s\n", errormessage );
		}
		if ( (verbosity > 0) && (errorlevel < err_tol) && (action == A_COMPRESS) ) {
			if ( total >= 0 ) {
				fprintf( msgout,  " time taken  : %7i msec\n", total );
				fprintf( msgout,  " byte per ms : %7i byte\n", bpms );
			}
			else {
				fprintf( msgout,  " time taken  : %7s msec\n", "N/A" );
				fprintf( msgout,  " byte per ms : %7s byte\n", "N/A" );
			}
			fprintf( msgout,  " comp. ratio : %7.2f %%\n", cr );		
		}	
		if ( ( verbosity > 1 ) && ( action == A_COMPRESS ) )
			fprintf( msgout,  "\n" );
	}
	else { // progress bar UI
		// if this is the last file, update progress bar one last time
		if ( file_no + 1 == file_cnt ) {
			// update progress message
			fprintf( msgout, "Processed %2i of %2i files ", file_no + 1, file_cnt );
			progress_bar( 1, 1 );
			fprintf( msgout, "\r" );
		}	
	}
}
#endif


/* -----------------------------------------------
	gets statusmessage for function
	----------------------------------------------- */
	
#if !defined(BUILD_LIB)
INTERN inline const char* get_status( bool (*function)() )
{	
	if ( function == NULL ) {
		return "unknown action";
	} else if ( function == *check_file ) {
		return "Determining filetype";
	} else if ( function == *pack_ppn ) {
		return "Converting PNM/BMP/HDR to PPN";
	} else if ( function == *unpack_ppn ) {
		return "Converting PPN to PNM/BMP/HDR";
	} 	else if ( function == *swap_streams ) {
		return "Swapping input/output streams";
	} else if ( function == *compare_output ) {
		return "Verifying output stream";
	} else if ( function == *reset_buffers ) {
		return "Resetting program";
	}
	#if defined(DEV_BUILD)
	else if ( function == *dump_pgm ) {
		return "Dumping RAW PGM";
	} else if ( function == *dump_pgm ) {
		return "Dumping NFO file";
	}
	#endif
	else {
		return "Function description missing!";
	}
}
#endif


/* -----------------------------------------------
	shows help in case of wrong input
	----------------------------------------------- */
	
#if !defined(BUILD_LIB)
INTERN void show_help( void )
{	
	fprintf( msgout, "\n" );
	fprintf( msgout, "Website: %s\n", website );
	fprintf( msgout, "Email  : %s\n", email );
	fprintf( msgout, "\n" );
	fprintf( msgout, "Usage: packpnm [switches] [filename(s)]" );
	fprintf( msgout, "\n" );
	fprintf( msgout, "\n" );
	fprintf( msgout, " [-ver]   verify files after processing\n" );
	fprintf( msgout, " [-v?]    set level of verbosity (max: 2) (def: 0)\n" );
	fprintf( msgout, " [-o]     overwrite existing files\n" );
	fprintf( msgout, " [-p]     proceed on warnings\n" );
	fprintf( msgout, " [-rle]   use RLE for writing HDR output\n" );
	#if defined(DEV_BUILD)
	if ( developer ) {
	fprintf( msgout, "\n" );
	fprintf( msgout, " [-dump]  dump raw PGM file\n" );
	fprintf( msgout, " [-info]  dump info file\n" );
	fprintf( msgout, " [-test]  test algorithms, alert if error\n" );
	}
	#endif
	fprintf( msgout, "\n" );
	fprintf( msgout, "Examples: \"packpgm baboon.pgm\"\n" );
	fprintf( msgout, "          \"packpgm *.ppm\"\n" );
	fprintf( msgout, "          \"packpgm kodim??.ppn\"\n" );	
}
#endif


/* -----------------------------------------------
	processes one file
	----------------------------------------------- */

INTERN void process_file( void )
{	
	if ( filetype == F_PNM ) {
		switch ( action ) {
			case A_COMPRESS:
				execute( pack_ppn );
				#if !defined(BUILD_LIB)	
				if ( verify_lv > 0 ) { // verifcation
					execute( reset_buffers );
					execute( swap_streams );
					execute( unpack_ppn );
					execute( compare_output );
				}
				#endif
				break;
				
			#if !defined(BUILD_LIB) && defined(DEV_BUILD)
			case A_PGM_DUMP:
				execute( dump_pgm );
				break;
				
			case A_NFO_DUMP:
				execute( dump_info );
				break;
			#else
			default:
				break;
			#endif
		}
	}
	else if ( filetype == F_PPN )	{
		switch ( action )
		{
			case A_COMPRESS:
				execute( unpack_ppn );
				#if !defined(BUILD_LIB)
				// this does not work yet!
				// and it's not even needed
				if ( verify_lv > 0 ) { // verify
					execute( reset_buffers );
					execute( swap_streams );
					execute( pack_ppn );
					execute( compare_output );
				}
				#endif
				break;
				
			#if !defined(BUILD_LIB) && defined(DEV_BUILD)
			case A_NFO_DUMP:
				execute( dump_info );
				break;
			#else
			default:
				break;
			#endif
		}
	}	
	#if !defined(BUILD_LIB) && defined(DEV_BUILD)
	// write error file if verify lv > 1
	if ( ( verify_lv > 1 ) && ( errorlevel >= err_tol ) )
		write_errfile();
	#endif
	// reset buffers
	reset_buffers();
}


/* -----------------------------------------------
	main-function execution routine
	----------------------------------------------- */

INTERN void execute( bool (*function)() )
{	
	if ( errorlevel < err_tol ) {
		#if !defined BUILD_LIB
		clock_t begin, end;
		bool success;
		int total;
		
		// write statusmessage
		if ( verbosity == 2 ) {
			fprintf( msgout,  "\n%s ", get_status( function ) );
			for ( int i = strlen( get_status( function ) ); i <= 30; i++ )
				fprintf( msgout,  " " );			
		}
		
		// set starttime
		begin = clock();
		// call function
		success = ( *function )();
		// set endtime
		end = clock();
		
		if ( ( errorlevel > 0 ) && ( errorfunction == NULL ) )
			errorfunction = function;
		
		// write time or failure notice
		if ( success ) {
			total = (int) ( (double) (( end - begin ) * 1000) / CLOCKS_PER_SEC );
			if ( verbosity == 2 ) fprintf( msgout,  "%6ims", ( total >= 0 ) ? total : -1 );
		}
		else {
			errorfunction = function;
			if ( verbosity == 2 ) fprintf( msgout,  "%8s", "ERROR" );
		}
		#else
		// call function
		( *function )();
		
		// store errorfunction if needed
		if ( ( errorlevel > 0 ) && ( errorfunction == NULL ) )
			errorfunction = function;
		#endif
	}
}

/* ----------------------- End of main interface functions -------------------------- */

/* ----------------------- Begin of main functions -------------------------- */


/* -----------------------------------------------
	check file and determine filetype
	----------------------------------------------- */

#if !defined(BUILD_LIB)
INTERN bool check_file( void )
{	
	char fileid[ 2 ] = { 0, 0 };
	const char* filename = filelist[ file_no ];
	const char* pnm_ext;
	
	// open input stream, check for errors
	str_in = new iostream( (void*) filename, ( !pipe_on ) ? 0 : 2, 0, 0 );
	if ( str_in->chkerr() ) {
		sprintf( errormessage, FRD_ERRMSG );
		errorlevel = 2;
		return false;
	}
	
	// free memory from filenames if needed
	if ( pnmfilename != NULL ) free( pnmfilename ); pnmfilename = NULL;
	if ( ppnfilename != NULL ) free( ppnfilename ); ppnfilename = NULL;
	
	// immediately return error if 2 bytes can't be read
	if ( str_in->read( fileid, 1, 2 ) != 2 ) {
		filetype = F_UNK;
		sprintf( errormessage, "file doesn't contain enough data" );
		errorlevel = 2;
		return false;
	}
	
	// rewind (need to start from the beginning)
	if ( str_in->rewind() != 0 ) {
		sprintf( errormessage, FRD_ERRMSG );
		errorlevel = 2;
		return false;
	}
	
	// check file id, determine filetype
	identify( fileid, &filetype, &subtype );
	if ( filetype == F_UNK ) {
		// file is neither
		sprintf( errormessage, "format of file \"%s\" is not supported", filelist[ file_no ] );
		errorlevel = 2;
		return false;
	} else if ( filetype == F_PNM ) {
		// file is PBM / PGM / PPM 
		// create filenames
		if ( !pipe_on ) {
			pnmfilename = (char*) calloc( strlen( filename ) + 1, sizeof( char ) );
			strcpy( pnmfilename, filename );
			ppnfilename = ( overwrite ) ?
				create_filename( filename, (char*) ppn_ext ) :
				unique_filename( filename, (char*) ppn_ext );
		}
		else {
			pnmfilename = create_filename( "STDIN", NULL );
			ppnfilename = create_filename( "STDOUT", NULL );
		}
		// open output stream, check for errors
		str_out = new iostream( (void*) ppnfilename, ( !pipe_on ) ? 0 : 2, 0, 1 );
		if ( str_out->chkerr() ) {
			sprintf( errormessage, FWR_ERRMSG );
			errorlevel = 2;
			return false;
		}
	} else if ( filetype == F_PPN ) {
		// file is PPN
		// create filenames
		if ( !pipe_on ) {
			switch ( subtype ) {
				case S_PBM: pnm_ext = "pbm"; break;
				case S_PGM: pnm_ext = "pgm"; break;
				case S_PPM: pnm_ext = "ppm"; break;
				case S_BMP: pnm_ext = "bmp"; break;
				case S_HDR: pnm_ext = "hdr"; break;
				default: pnm_ext = "unk"; break;
			} 
			ppnfilename = (char*) calloc( strlen( filename ) + 1, sizeof( char ) );
			strcpy( ppnfilename, filename );
			pnmfilename = ( overwrite ) ?
				create_filename( filename, (char*) pnm_ext ) :
				unique_filename( filename, (char*) pnm_ext );
		}
		else {
			pnmfilename = create_filename( "STDOUT", NULL );
			ppnfilename = create_filename( "STDIN", NULL );
		}
		// open output stream, check for errors
		str_out = new iostream( (void*) pnmfilename, ( !pipe_on ) ? 0 : 2, 0, 1 );
		if ( str_out->chkerr() ) {
			sprintf( errormessage, FWR_ERRMSG );
			errorlevel = 2;
			return false;
		}
	}
	
	
	return true;
}
#endif


/* -----------------------------------------------
	swap streams / init verification
	----------------------------------------------- */
#if !defined(BUILD_LIB)
INTERN bool swap_streams( void )	
{
	// store input stream
	str_str = str_in;
	str_str->rewind();
	
	// replace input stream by output stream / switch mode for reading
	str_in = str_out;
	str_in->switch_mode();
	
	// open new stream for output / check for errors
	str_out = new iostream( NULL, 1, 0, 1 );
	if ( str_out->chkerr() ) {
		sprintf( errormessage, "error opening comparison stream" );
		errorlevel = 2;
		return false;
	}
	
	
	return true;
}
#endif


/* -----------------------------------------------
	comparison between input & output
	----------------------------------------------- */
#if !defined( BUILD_LIB )
INTERN bool compare_output( void )
{
	unsigned char* buff_ori;
	unsigned char* buff_cmp;
	int bsize = 1024;
	int dsize;
	int i, b;
	
	
	// reset error level
	errorlevel = 0;
	
	// init buffer arrays
	buff_ori = ( unsigned char* ) calloc( bsize, sizeof( char ) );
	buff_cmp = ( unsigned char* ) calloc( bsize, sizeof( char ) );
	if ( ( buff_ori == NULL ) || ( buff_cmp == NULL ) ) {
		if ( buff_ori != NULL ) free( buff_ori );
		if ( buff_cmp != NULL ) free( buff_cmp );
		sprintf( errormessage, MEM_ERRMSG );
		errorlevel = 2;
		return false;
	}
	
	// switch output stream mode / check for stream errors
	str_out->switch_mode();
	while ( true ) {
		if ( str_out->chkerr() )
			sprintf( errormessage, "error in comparison stream" );
		else if ( str_in->chkerr() )
			sprintf( errormessage, "error in output stream" );
		else if ( str_str->chkerr() )
			sprintf( errormessage, "error in input stream" );
		else break;
		errorlevel = 2;
		return false;
	}
	
	// compare sizes
	dsize = str_str->getsize();
	if ( str_out->getsize() != dsize ) {
		if ( str_out->getsize() < dsize ) dsize = str_out->getsize();
		sprintf( errormessage, "file sizes do not match" );
		errorlevel = 1;
	}
	
	// compare files byte by byte
	for ( i = 0; i < dsize; i++ ) {
		b = i % bsize;
		if ( b == 0 ) {
			str_str->read( buff_ori, 1, bsize );
			str_out->read( buff_cmp, 1, bsize );
		}
		if ( buff_ori[ b ] != buff_cmp[ b ] ) {
			sprintf( errormessage, "difference found at 0x%X", i );
			errorlevel = 2;
			return false;
		}
	}
	
	
	return true;
}
#endif


/* -----------------------------------------------
	set each variable to its initial value
	----------------------------------------------- */

INTERN bool reset_buffers( void )
{
	imgwidth = 0;	// width of image
	imgheight = 0;	// height of image
	imgwidthv = 0;	// visible width of image
	imgbpp = 0;		// bit per pixel
	cmpc = 0;		// component count
	pnmax = 0; 		// maximum pixel value
	endian_l = 0;	// endianness
	bmpsize = 0;	// filesize according to header
	for ( int i = 0; i < 5; i++ ) {
		if ( cmask[i] != NULL ) delete( cmask[i] );
		cmask[i] = NULL;
	}
	
	return true;
}


/* -----------------------------------------------
	packs all parts to compressed pgs
	----------------------------------------------- */
	
INTERN bool pack_ppn( void )
{
	char* imghdr = NULL;
	bool error = false;
	aricoder* encoder;
	
	
	// parse PNM file header
	imghdr = scan_header( str_in );
	if ( imghdr == NULL ) {
		errorlevel = 2;
		return false;
	}
	
	// write PPN file header
	imghdr[0] = 'S';
	str_out->write( imghdr, 1, ( subtype != S_BMP ) ? strlen( imghdr ) : INT_LE( imghdr + 0x0A ) );
	str_out->write( ( void* ) &appversion, 1, 1 );
		
	// init arithmetic compression
	encoder = new aricoder( str_out, 1 );
	
	// arithmetic encode image data (select method)
	switch ( imgbpp ) {
		case  1:
			if ( !(ppn_encode_imgdata_mono( encoder, str_in )) ) error = true;
			break;
		case  4:
		case  8:
			if ( subtype == S_BMP ) {
				if ( !(ppn_encode_imgdata_palette( encoder, str_in )) ) error = true;
			} else if ( !(ppn_encode_imgdata_rgba( encoder, str_in )) ) error = true;
			break;
		case 16:
		case 24:
		case 32:
		case 48:
			if ( !(ppn_encode_imgdata_rgba( encoder, str_in )) ) error = true;
			break;
		default: sprintf( errormessage, "%ibpp is not supported", imgbpp ); error = true; break;
	}
	
	// finalize arithmetic compression
	delete( encoder );
	
	// error flag set?
	if ( error ) return false;
	
	// read BMP junk data
	if ( subtype == S_BMP ) for ( char bt = 0; str_in->getpos() < bmpsize; ) {
		if ( str_in->read( &bt, 1, 1 ) != 1 ) {
			sprintf( errormessage, "incorrect BMP size in header" );
			errorlevel = 1;
			break;
		} else if ( bt != 0 ) {
			sprintf( errormessage, "junk data after BMP end-of-image" );
			errorlevel = 1;
			break;
		}
	}
	
	// check for junk data
	if ( str_in->getpos() != str_in->getsize() ) {
		sprintf( errormessage, "junk data after end-of-image" );
		errorlevel = 1;		
	}
	
	// errormessage if write error
	if ( str_out->chkerr() ) {
		sprintf( errormessage, "write error, possibly drive is full" );
		errorlevel = 2;		
		return false;
	}
	
	// get filesizes
	pnmfilesize = str_in->getsize();
	ppnfilesize = str_out->getsize();
	
	
	return true;
}


/* -----------------------------------------------
	unpacks compressed pgs
	----------------------------------------------- */
INTERN bool unpack_ppn( void )
{
	char* imghdr = NULL;
	bool error = false;
	unsigned char ver;
	aricoder* decoder;
	
	
	// parse PPN header
	imghdr = scan_header( str_in );
	if ( imghdr == NULL ) {
		errorlevel = 2;
		return false;
	}
	
	// check packPNM version
	str_in->read( (void*) &ver, 1, 1 );
	if ( ver == 14 ) {
		endian_l = E_LITTLE; // bad (mixed endianness!) (!!!)
		ver = 16; // compatibility hack for v1.4
	}
	if ( ver != appversion ) {
		sprintf( errormessage, "incompatible file, use %s v%i.%i",
			appname, ver / 10, ver % 10 );
		errorlevel = 2;
		return false;
	}
	
	// write PNM file header
	imghdr[0] = ( subtype == S_BMP ) ? 'B' : ( subtype == S_HDR ) ? '#' : 'P';
	str_out->write( imghdr, 1, ( subtype != S_BMP ) ? strlen( imghdr ) : INT_LE( imghdr + 0x0A ) );
	
	// init arithmetic compression
	decoder = new aricoder( str_in, 0 );
	
	// arithmetic decode image data (select method)
	switch ( imgbpp ) {
		case  1:
			if ( !(ppn_decode_imgdata_mono( decoder, str_out )) ) error = true;
			break;
		case  4:
		case  8:
			if ( subtype == S_BMP ) {
				if ( !(ppn_decode_imgdata_palette( decoder, str_out )) ) error = true;
			} else if ( !(ppn_decode_imgdata_rgba( decoder, str_out )) ) error = true;
			break;
		case 16:
		case 24:
		case 32:
		case 48:
			if ( !(ppn_decode_imgdata_rgba( decoder, str_out )) ) error = true;
			break;
		default: error = true;
	}
	
	// finalize arithmetic decompression
	delete( decoder );
	
	// error flag set?
	if ( error ) return false;
	
	// write BMP junk data
	if ( subtype == S_BMP ) for ( char bt = 0; str_out->getpos() < bmpsize; )
		str_out->write( &bt, 1, 1 );
	
	// errormessage if write error
	if ( str_out->chkerr() ) {
		sprintf( errormessage, "write error, possibly drive is full" );
		errorlevel = 2;	
		return false;
	}
	
	// get filesizes
	ppnfilesize = str_in->getsize();
	pnmfilesize = str_out->getsize();	
	
	
	return true;
}

/* ----------------------- End of main functions -------------------------- */

/* ----------------------- Begin of side functions -------------------------- */


/* -----------------------------------------------
	PPN PJG type RGBA/E encoding
	----------------------------------------------- */
INTERN bool ppn_encode_imgdata_rgba( aricoder* enc, iostream* stream )
{
	pjg_model* mod[4];
	int* storage; // storage array
	int* val[4][2] = { { NULL } };
	int* err[4][2] = { { NULL } };
	int* dta[4] = { NULL };
	int c, x, y;
	
	
	// init models
	for ( c = 0; c < cmpc; c++ )
		mod[c] = new pjg_model(
			( pnmax == 0 ) ? cmask[c]->m : pnmax,
			( ( c < 3 ) && ( cmpc >= 3 ) ) ? 3 : 2 );
	
	// allocate storage memory
	storage = (int*) calloc( ( imgwidth + 2 ) * 4 * cmpc, sizeof( int ) );
	if ( storage == NULL ) {
		sprintf( errormessage, MEM_ERRMSG );
		errorlevel = 2;
		return false;
	}
	
	// arithmetic compression loop
	for ( y = 0; y < imgheight; y++ ) {
		// set pointers
		for ( c = 0; c < cmpc; c++ ) {
			val[c][0] = storage + 2 + ( ( imgwidth + 2 ) * ( 0 + (4*c) + ( (y+0) % 2 ) ) );
			val[c][1] = storage + 2 + ( ( imgwidth + 2 ) * ( 0 + (4*c) + ( (y+1) % 2 ) ) );
			err[c][0] = storage + 2 + ( ( imgwidth + 2 ) * ( 2 + (4*c) + ( (y+0) % 2 ) ) );
			err[c][1] = storage + 2 + ( ( imgwidth + 2 ) * ( 2 + (4*c) + ( (y+1) % 2 ) ) );
			dta[c] = val[c][0];
		}
		// read line
		errorlevel = pnm_read_line( stream, dta );
		if ( errorlevel == 1 ) sprintf( errormessage, ( subtype == S_HDR ) ?
			"bitwise reconstruction of HDR RLE not guaranteed" : "excess data or bad data found"  );
		else if ( errorlevel == 2 ) {
			sprintf( errormessage, "unexpected end of file"  );
			free( storage );
			for ( c = 0; c < cmpc; c++ ) delete( mod[c] );
			return false;
		}
		// encode pixel values
		for ( x = 0; x < imgwidth; x++ ) {
			if ( cmpc == 1 ) {
				ppn_encode_pjg( enc, mod[0], val[0], err[0], -1 );
			} else { // cmpc >= 3
				if ( cmpc > 3 ) ppn_encode_pjg( enc, mod[3], val[3], err[3], -1 );
				ppn_encode_pjg( enc, mod[0], val[0], err[0], BITLENB16N(err[2][0][-1]) );
				ppn_encode_pjg( enc, mod[2], val[2], err[2], BITLENB16N(err[0][0][0]) );
				ppn_encode_pjg( enc, mod[1], val[1], err[1],
					( BITLENB16N(err[0][0][0]) + BITLENB16N(err[2][0][0]) + 1 ) / 2 );
			}
			// advance values
			for ( c = 0; c < cmpc; c++ ) {
				val[c][0]++; val[c][1]++; err[c][0]++; err[c][1]++;
			}
		}
	}	
	
	// free storage / clear models
	free( storage );
	for ( c = 0; c < cmpc; c++ ) delete( mod[c] );
	
	
	return true;
}


/* -----------------------------------------------
	PPN PJG type RGBA/E decoding
	----------------------------------------------- */
INTERN bool ppn_decode_imgdata_rgba( aricoder* dec, iostream* stream )
{
	pjg_model* mod[4];
	int* storage; // storage array
	int* val[4][2] = { { NULL } };
	int* err[4][2] = { { NULL } };
	int* dta[4] = { NULL };
	int c, x, y;
	
	
	// init models
	for ( c = 0; c < cmpc; c++ )
		mod[c] = new pjg_model(
			( pnmax == 0 ) ? cmask[c]->m : pnmax,
			( ( c < 3 ) && ( cmpc >= 3 ) ) ? 3 : 2 );
	
	// allocate storage memory
	storage = (int*) calloc( ( imgwidth + 2 ) * 4 * cmpc, sizeof( int ) );
	if ( storage == NULL ) {
		sprintf( errormessage, MEM_ERRMSG );
		errorlevel = 2;
		return false;
	}
	
	// arithmetic compression loop
	for ( y = 0; y < imgheight; y++ ) {
		// set pointers
		for ( c = 0; c < cmpc; c++ ) {
			val[c][0] = storage + 2 + ( ( imgwidth + 2 ) * ( 0 + (4*c) + ( (y+0) % 2 ) ) );
			val[c][1] = storage + 2 + ( ( imgwidth + 2 ) * ( 0 + (4*c) + ( (y+1) % 2 ) ) );
			err[c][0] = storage + 2 + ( ( imgwidth + 2 ) * ( 2 + (4*c) + ( (y+0) % 2 ) ) );
			err[c][1] = storage + 2 + ( ( imgwidth + 2 ) * ( 2 + (4*c) + ( (y+1) % 2 ) ) );
			dta[c] = val[c][0];
		}
		// decode pixel values
		for ( x = 0; x < imgwidth; x++ ) {
			if ( cmpc == 1 ) {
				ppn_decode_pjg( dec, mod[0], val[0], err[0], -1 );
			} else { // cmpc >= 3
				if ( cmpc > 3 ) ppn_decode_pjg( dec, mod[3], val[3], err[3], -1 );
				ppn_decode_pjg( dec, mod[0], val[0], err[0], BITLENB16N(err[2][0][-1]) );
				ppn_decode_pjg( dec, mod[2], val[2], err[2], BITLENB16N(err[0][0][0]) );
				ppn_decode_pjg( dec, mod[1], val[1], err[1],
					( BITLENB16N(err[0][0][0]) + BITLENB16N(err[2][0][0]) + 1 ) / 2 );
			}
			// advance values
			for ( c = 0; c < cmpc; c++ ) {
				val[c][0]++; val[c][1]++; err[c][0]++; err[c][1]++;
			}
		}
		// write line
		pnm_write_line( stream, dta );
	}	
	
	// free storage / clear models
	free( storage );
	for ( c = 0; c < cmpc; c++ ) delete( mod[c] );
	
	
	return true;
}


/* -----------------------------------------------
	PPN special mono encoding
	----------------------------------------------- */
INTERN bool ppn_encode_imgdata_mono( aricoder* enc, iostream* stream )
{
	model_b* mod;
	int* storage; // storage array
	int* val[3];
	int* dta;
	int x, y;
	
	
	// init model
	mod = INIT_MODEL_B( ( 1 << 12 ), 1 );
	
	// allocate storage memory
	storage = (int*) calloc( ( imgwidth + 5 ) * 3, sizeof( int ) );
	if ( storage == NULL ) {
		sprintf( errormessage, MEM_ERRMSG );
		errorlevel = 2;
		return false;
	}
	
	// arithmetic compression loop
	for ( y = 0; y < imgheight; y++ ) {
		// set pointers
		val[0] = storage + 3 + ( ( imgwidth + 2 + 3 ) * ( (y+2) % 3 ) );
		val[1] = storage + 3 + ( ( imgwidth + 2 + 3 ) * ( (y+1) % 3 ) );
		val[2] = storage + 3 + ( ( imgwidth + 2 + 3 ) * ( (y+0) % 3 ) );
		dta = val[0];
		// read line
		if ( pnm_read_line( stream, &dta ) != 0 ) {
			sprintf( errormessage, "unexpected end of file"  );
			errorlevel = 2;
			free( storage );
			delete( mod );
			return false;
		}
		// encode pixel values
		for ( x = 0; x < imgwidth; x++, val[0]++, val[1]++, val[2]++ ) {
			mod->shift_context( get_context_mono( x, y, val ) );
			encode_ari( enc, mod, **val );
		}
	}	
	
	// free storage / clear models
	free( storage );
	delete( mod );
	
	
	return true;
}


/* -----------------------------------------------
	PPN special mono decoding
	----------------------------------------------- */
INTERN bool ppn_decode_imgdata_mono( aricoder* dec, iostream* stream )
{
	model_b* mod;
	int* storage; // storage array
	int* val[3];
	int* dta;
	int x, y;
	
	
	// init model
	mod = INIT_MODEL_B( ( 1 << 12 ), 1 );
	
	// allocate storage memory
	storage = (int*) calloc( ( imgwidth + 5 ) * 3, sizeof( int ) );
	if ( storage == NULL ) {
		sprintf( errormessage, MEM_ERRMSG );
		errorlevel = 2;
		return false;
	}
	
	// arithmetic compression loop
	for ( y = 0; y < imgheight; y++ ) {
		// set pointers
		val[0] = storage + 3 + ( ( imgwidth + 2 + 3 ) * ( (y+2) % 3 ) );
		val[1] = storage + 3 + ( ( imgwidth + 2 + 3 ) * ( (y+1) % 3 ) );
		val[2] = storage + 3 + ( ( imgwidth + 2 + 3 ) * ( (y+0) % 3 ) );
		dta = val[0];
		// decode pixel values
		for ( x = 0; x < imgwidth; x++, val[0]++, val[1]++, val[2]++ ) {
			mod->shift_context( get_context_mono( x, y, val ) );
			**val = decode_ari( dec, mod );
		}
		// write line
		pnm_write_line( stream, &dta );
	}	
	
	// free storage / clear models
	free( storage );
	delete( mod );
	
	
	return true;
}


/* -----------------------------------------------
	PPN encoding for palette based image data
	----------------------------------------------- */
INTERN bool ppn_encode_imgdata_palette( aricoder* enc, iostream* stream )
{
	model_s* mod;
	int* storage; // storage array
	int* val[2];
	int* dta;
	int x, y;
	
	
	// init model
	mod = ( pnmax ) ? INIT_MODEL_S( pnmax + 1, pnmax + 1, 2 ) :
		INIT_MODEL_S( cmask[0]->m + 1, cmask[0]->m + 1, 2 );
	
	// allocate storage memory
	storage = (int*) calloc( ( imgwidth + 1 ) * 2, sizeof( int ) );
	if ( storage == NULL ) {
		sprintf( errormessage, MEM_ERRMSG );
		errorlevel = 2;
		return false;
	}
	
	// arithmetic compression loop
	for ( y = 0; y < imgheight; y++ ) {
		// set pointers
		val[0] = storage + 1 + ( ( imgwidth + 1 ) * ( (y+0) % 2 ) );
		val[1] = storage + 1 + ( ( imgwidth + 1 ) * ( (y+1) % 2 ) );
		dta = val[0];
		// read line
		errorlevel = pnm_read_line( stream, &dta );
		if ( errorlevel == 1 ) sprintf( errormessage, "excess data or bad data found"  );
		else if ( errorlevel == 2 ) {
			sprintf( errormessage, "unexpected end of file"  );
			free( storage );
			delete( mod );
			return false;
		}
		// encode pixel values
		for ( x = 0; x < imgwidth; x++, val[0]++, val[1]++ ) {
			shift_model( mod, val[0][-1], val[1][0] ); // shift in context
			encode_ari( enc, mod, **val );
		}
	}	
	
	// free storage / clear models
	free( storage );
	delete( mod );
	
	
	return true;
}


/* -----------------------------------------------
	PPN decoding for palette based image data
	----------------------------------------------- */
INTERN bool ppn_decode_imgdata_palette( aricoder* dec, iostream* stream )
{
	model_s* mod;
	int* storage; // storage array
	int* val[2];
	int* dta;
	int x, y;
	
	
	// init model
	mod = ( pnmax ) ? INIT_MODEL_S( pnmax + 1, pnmax + 1, 2 ) :
		INIT_MODEL_S( cmask[0]->m + 1, cmask[0]->m + 1, 2 );
	
	// allocate storage memory
	storage = (int*) calloc( ( imgwidth + 1 ) * 2, sizeof( int ) );
	if ( storage == NULL ) {
		sprintf( errormessage, MEM_ERRMSG );
		errorlevel = 2;
		return false;
	}
	
	// arithmetic compression loop
	for ( y = 0; y < imgheight; y++ ) {
		// set pointers
		val[0] = storage + 1 + ( ( imgwidth + 1 ) * ( (y+0) % 2 ) );
		val[1] = storage + 1 + ( ( imgwidth + 1 ) * ( (y+1) % 2 ) );
		dta = val[0];
		// decode pixel values
		for ( x = 0; x < imgwidth; x++, val[0]++, val[1]++ ) {
			shift_model( mod, val[0][-1], val[1][0] ); // shift in context
			**val = decode_ari( dec, mod );
		}
		// write line
		pnm_write_line( stream, &dta );
	}	
	
	// free storage / clear models
	free( storage );
	delete( mod );
	
	
	return true;
}


/* -----------------------------------------------
	PPN packJPG type encoding
	----------------------------------------------- */
INTERN inline void ppn_encode_pjg( aricoder* enc, pjg_model* mod, int** val, int** err, int ctx3 ) {
	int ctx_sgn; // context for sign
	int clen, absv, sgn;
	int bt, bp;

	// calculate prediction error
	**err = **val - plocoi( val[0][-1], val[1][0], val[1][-1] );
	
	// encode bit length
	clen = BITLENB16N( **err );
	if ( ctx3 < 0 ) shift_model( mod->len, BITLENB16N( err[0][-1] ), BITLENB16N( err[1][0] ) );
	else shift_model( mod->len, BITLENB16N( err[0][-1] ), BITLENB16N( err[1][0] ), ctx3 );
	encode_ari( enc, mod->len, clen );
	
	// encode residual, sign only if bit length > 0
	if ( clen > 0 )	{
		// compute absolute value, sign
		absv = ABS( **err );
		sgn = ( **err > 0 ) ? 0 : 1;
		// compute sign context
		ctx_sgn = 0x0;
		if ( err[0][-2] > 0 ) ctx_sgn |= 0x1 << 0;
		if ( err[1][-1] > 0 ) ctx_sgn |= 0x1 << 1;
		if ( err[0][-1] > 0 ) ctx_sgn |= 0x1 << 2;
		if ( err[1][ 0] > 0 ) ctx_sgn |= 0x1 << 3;
		// first set bit must be 1, so we start at clen - 2
		for ( bp = clen - 2; bp >= 0; bp-- ) {
			shift_model( mod->res, clen, bp ); // shift in context
			// encode/get bit
			bt = BITN( absv, bp );
			encode_ari( enc, mod->res, bt );
		}			
		// encode sign
		mod->sgn->shift_context( ctx_sgn );
		encode_ari( enc, mod->sgn, sgn );
	}
}


/* -----------------------------------------------
	PPN packJPG type decoding
	----------------------------------------------- */
INTERN inline void ppn_decode_pjg( aricoder* dec, pjg_model* mod, int** val, int** err, int ctx3 )
{
	int ctx_sgn; // context for sign
	int clen, absv, sgn;
	int bt, bp;
	
	
	// decode bit length (of prediction error)
	if ( ctx3 < 0 ) shift_model( mod->len, BITLENB16N( err[0][-1] ), BITLENB16N( err[1][0] ) );
	else shift_model( mod->len, BITLENB16N( err[0][-1] ), BITLENB16N( err[1][0] ), ctx3 );
	clen = decode_ari( dec, mod->len );
	
	// decode residual, sign only if bit length > 0
	if ( clen > 0 )	{
		// compute sign context
		ctx_sgn = 0x0;
		if ( err[0][-2] > 0 ) ctx_sgn |= 0x1 << 0;
		if ( err[1][-1] > 0 ) ctx_sgn |= 0x1 << 1;
		if ( err[0][-1] > 0 ) ctx_sgn |= 0x1 << 2;
		if ( err[1][ 0] > 0 ) ctx_sgn |= 0x1 << 3;
		// first set bit must be 1, so we start at clen - 2
		for ( bp = clen - 2, absv = 1; bp >= 0; bp-- ) {
			shift_model( mod->res, clen, bp ); // shift in context
			// decode bit
			bt = decode_ari( dec, mod->res );
			// update value
			absv = absv << 1;
			if ( bt ) absv |= 1; 
		}
		// decode sign
		mod->sgn->shift_context( ctx_sgn );
		sgn = decode_ari( dec, mod->sgn );
		// store data
		**err = ( sgn == 0 ) ? absv : -absv;
	} else **err = 0;
	
	// decode prediction error
	**val = **err + plocoi( val[0][-1], val[1][0], val[1][-1] );
}


/* -----------------------------------------------
	special context for mono color space
	----------------------------------------------- */
INTERN inline int get_context_mono( int x, int y, int** val )
{
	int ctx_mono = 0;
	
	
	// this is the context modelling used here:
	//			[i]	[f]	[j]
	//		[g]	[c]	[b]	[d]	[h]
	//	[k]	[e]	[a]	[x]
	// [x] is to be encoded	
	// this function calculates and returns coordinates for a simple 2D context
	
	// base context calculation
	ctx_mono <<= 1; if ( val[0][-1] ) ctx_mono |= 1; // a
	ctx_mono <<= 1; if ( val[1][ 0] ) ctx_mono |= 1; // b
	ctx_mono <<= 1; if ( val[1][-1] ) ctx_mono |= 1; // c
	ctx_mono <<= 1; if ( val[1][ 1] ) ctx_mono |= 1; // d
	ctx_mono <<= 1; if ( val[0][-2] ) ctx_mono |= 1; // e
	ctx_mono <<= 1; if ( val[2][ 0] ) ctx_mono |= 1; // f
	ctx_mono <<= 1; if ( val[1][-2] ) ctx_mono |= 1; // g
	ctx_mono <<= 1; if ( val[1][ 2] ) ctx_mono |= 1; // h
	ctx_mono <<= 1; if ( val[2][-1] ) ctx_mono |= 1; // i
	ctx_mono <<= 1; if ( val[2][ 1] ) ctx_mono |= 1; // j
	ctx_mono <<= 1; if ( val[0][-3] ) ctx_mono |= 1; // k
	
	// special flags (edge treatment)
	if ( x <= 2 ) {
		if ( x == 0 )
			 ctx_mono |= 0x804; // bits 11 (z) &  3 (c)
		else if ( x == 1 )
			 ctx_mono |= 0x840; // bits 11 (z) &  6 (g)
		else ctx_mono |= 0xC00; // bits 11 (z) & 10 (k)
	} else if ( y <= 1 ) {
		if ( y == 0 )
			 ctx_mono |= 0x810; // bits 11 (z) &  4 (d)
		else ctx_mono |= 0xA00; // bits 11 (z) &  9 (j)
	}
	
	
	return ctx_mono;
}


/* -----------------------------------------------
	loco-i predictor
	----------------------------------------------- */
INTERN inline int plocoi( int a, int b, int c )
{
	// a -> left; b -> above; c -> above-left
	int min, max;
	
	min = ( a < b ) ? a : b;
	max = ( a > b ) ? a : b;
	
	if ( c >= max ) return min;
	if ( c <= min ) return max;
	
	return a + b - c;
}


/* -----------------------------------------------
	PNM read line
	----------------------------------------------- */
INTERN inline int pnm_read_line( iostream* stream, int** line )
{
	unsigned int rgb[ 4 ] = { 0, 0, 0, 0 }; // RGB + A 
	unsigned char bt = 0;
	unsigned int dt = 0;
	int w_excess = 0;
	int x, n, c;
	
	
	if ( cmpc == 1 ) switch ( imgbpp ) { // single component data
		case  1:
			for ( x = 0; x < imgwidth; x += 8 ) {
				if ( stream->read( &bt, 1, 1 ) != 1 ) return 2;
				for ( n = 8 - 1; n >= 0; n--, bt >>= 1 ) line[0][x+n] = bt & 0x1;
			}
			break;
			
		case  4:
			for ( x = 0; x < imgwidth; x += 2 ) {
				if ( stream->read( &bt, 1, 1 ) != 1 ) return 2;
				line[0][x+1] = (bt>>0) & 0xF;
				line[0][x+0] = (bt>>4) & 0xF;
				if ( pnmax > 0 ) {
					if ( line[0][x+0] > (signed) pnmax ) { line[0][x+0] = pnmax; w_excess = 1; }
					if ( line[0][x+1] > (signed) pnmax ) { line[0][x+1] = pnmax; w_excess = 1; }
				}
			}
			break;
			
		case  8:
			for ( x = 0; x < imgwidth; x++ ) {
				if ( stream->read( &bt, 1, 1 ) != 1 ) return 2;
				if ( pnmax > 0 ) if ( bt > pnmax ) { bt = pnmax; w_excess = 1; }
				line[0][x] = bt;
			}
			break;
			
		case 16:
			for ( x = 0; x < imgwidth; x++ ) {
				for ( n = 0, dt = 0; n < 16; n += 8 ) {
					if ( stream->read( &bt, 1, 1 ) != 1 ) return 2;
					dt = ( endian_l ) ? dt | ( bt << n ) : ( dt << 8 ) | bt;
				}
				if ( pnmax > 0 ) if ( dt > pnmax ) { dt = pnmax; w_excess = 1; }
				line[0][x] = dt;
			}
			break;
	} else { // multi component data
		for ( x = 0; x < imgwidth; x++ ) {
			if ( subtype == S_BMP ) { // 16/24/32bpp BMP
				for ( n = 0, dt = 0; n < imgbpp; n += 8 ) {
					if ( stream->read( &bt, 1, 1 ) != 1 ) return 2;
					dt = ( endian_l ) ? dt | ( bt << n ) : ( dt << 8 ) | bt;
				}
				for ( c = 0; c < cmpc; c++ ) {
					rgb[c] = ( dt >> cmask[c]->p ) & cmask[c]->m;
					if ( pnmax > 0 ) if ( rgb[c] > pnmax ) { rgb[c] = pnmax; w_excess = 1; }
				}
				if ( cmask[4] != NULL ) if ( ( dt >> cmask[4]->p ) & cmask[4]->m ) w_excess = 1;
			} else if ( subtype == S_PPM ) { // 24/48bpp PPM
				for ( c = 0; c < 3; c++ ) {
					if ( imgbpp == 48 ) for ( n = 0, dt = 0; n < 16; n += 8 ) {
						if ( stream->read( &bt, 1, 1 ) != 1 ) return 2;
						dt = ( endian_l ) ? dt | ( bt << n ) : ( dt << 8 ) | bt;
					} else { // imgbpp == 24
						if ( stream->read( &bt, 1, 1 ) != 1 ) return 2;
						dt = bt;
					}
					if ( pnmax > 0 ) if ( dt > pnmax ) { dt = pnmax; w_excess = 1; }
					rgb[c] = dt;
				}
			} else { // 32bpp HDR
				for ( c = 0; c < 4; c++ ) { // try uncompressed reading
					if ( stream->read( &bt, 1, 1 ) != 1 ) return 2;
					rgb[c] = bt;
				}
				if ( x == 0 ) { // check for RLE compression
					for ( c = 0, dt = 0; c < 4; dt = ( dt << 8 ) | rgb[c++] );
					if ( dt == (unsigned) ( 0x02020000 | imgwidth ) )
						return hdr_decode_line_rle( stream, line );
				}
			}
			// RGB color component prediction & copy to line
			rgb_process( rgb );
			for ( c = 0; c < cmpc; c++ ) line[c][x] = rgb[c];
		}
	}
	
	// bmp line alignment at 4 byte
	if ( subtype == S_BMP )	{
		for ( x = imgwidth * imgbpp; ( x % 32 ) != 0; x += 8 ) {
			if ( stream->read( &bt, 1, 1 ) != 1 ) return 2;
			if ( bt ) w_excess = 1;
		}
	}		
	
	
	return w_excess;
}


/* -----------------------------------------------
	PNM write line
	----------------------------------------------- */
INTERN inline int pnm_write_line( iostream* stream, int** line )
{
	unsigned int rgb[ 4 ]; // RGB + A
	unsigned char bt = 0;
	unsigned int dt = 0;
	int x, n, c;
	
	
	if ( cmpc == 1 ) switch ( imgbpp ) { // single component data
		case  1:
			for ( x = 0; x < imgwidth; x += 8 ) {
				for ( n = 0, bt = 0; n < 8; n++ ) {
					bt <<= 1; if ( line[0][x+n] ) bt |= 0x1; 
				}
				stream->write( &bt, 1, 1 );
			}
			break;
			
		case  4:
			for ( x = 0; x < imgwidth; x += 2 ) {
				bt = ( line[0][x+0] << 4 ) | line[0][x+1];
				stream->write( &bt, 1, 1 );
			}
			break;
			
		case  8:
			for ( x = 0; x < imgwidth; x++ ) {
				bt = line[0][x];
				stream->write( &bt, 1, 1 );
			}
			break;
			
		case 16:
			for ( x = 0; x < imgwidth; x++ ) {
				for ( n = 0, dt = line[0][x]; n < 16; n += 8 ) {
					bt = ( ( endian_l ) ? ( dt >> n ) : ( dt >> ( ( 16 - 8 ) - n ) ) ) & 0xFF;
					stream->write( &bt, 1, 1 );
				}
			}
			break;
	} else { // multi component data
		if ( use_rle && ( subtype == S_HDR ) ) { // HDR RLE encoding
			bt = 0x02; stream->write( &bt, 1, 1 ); stream->write( &bt, 1, 1 );
			bt = ( imgwidth >> 8 ) & 0xFF; stream->write( &bt, 1, 1 );
			bt = imgwidth & 0xFF; stream->write( &bt, 1, 1 );
			return hdr_encode_line_rle( stream, line );			
		}
		for ( x = 0; x < imgwidth; x++ ) {
			// copy & RGB color component prediction undo
			for ( c = 0; c < cmpc; c++ ) rgb[c] = line[c][x];
			// RGB color component prediction & copy to line
			rgb_unprocess( rgb );
			if ( subtype == S_BMP ) { // 16/24/32bpp BMP
				for ( c = 0, dt = 0; c < cmpc; c++ ) dt |= rgb[c] << cmask[c]->p;
				for ( n = 0; n < imgbpp; n += 8 ) {
					bt = ( ( endian_l ) ? ( dt >> n ) : ( dt >> ( ( imgbpp - 8 ) - n ) ) ) & 0xFF;
					stream->write( &bt, 1, 1 );
				}
			} else if ( subtype == S_PPM ) { // 24/48bpp PPM
				for ( c = 0; c < 3; c++ ) {
					if ( imgbpp == 48 ) for ( n = 0, dt = rgb[c]; n < 16; n += 8 ) {
						bt = ( ( endian_l ) ? ( dt >> n ) : ( dt >> ( ( 16 - 8 ) - n ) ) ) & 0xFF;
						stream->write( &bt, 1, 1 );
					} else { // imgbpp == 24
						bt = rgb[c];
						stream->write( &bt, 1, 1 );
					}
				}
			} else { // 32bpp HDR
				for ( c = 0; c < 4; c++ ) {
					bt = rgb[c];
					stream->write( &bt, 1, 1 );
				}
			}
		}
	}
	
	// bmp line alignment at 4 byte
	if ( subtype == S_BMP )	{
		for ( x = imgwidth * imgbpp, bt = 0; ( x % 32 ) != 0; x += 8 )
			stream->write( &bt, 1, 1 );
	}
	
	
	return 0;
}


/* -----------------------------------------------
	HDR decode RLE
	----------------------------------------------- */
INTERN inline int hdr_decode_line_rle( iostream* stream, int** line )
{
	static unsigned int* data = NULL;
	static int prev_width = 0;	
	unsigned int* rgb; // RGB + E
	unsigned char bt = 0;
	int r, rl;
	int x, c;
	
	
	// allocate memory for line storage
	if ( prev_width != imgwidth ) {
		prev_width = imgwidth;
		data = ( unsigned int* ) calloc( imgwidth * 4, sizeof( int ) );
		if ( data == NULL ) return 2; // bad, but unlikely to happen anyways
	}
	
	// RLE compressed reading
	for ( c = 0; c < 4; c++ ) {
		for ( x = 0, rgb = data+c; x < imgwidth; x += rl ) {
			if ( stream->read( &bt, 1, 1 ) != 1 ) return 2;
			if ( bt > 0x80 ) { // run of value
				rl = bt ^ 0x80; // run length: absolute of bt
				if ( x + rl > imgwidth ) return 2; // sanity check
				if ( stream->read( &bt, 1, 1 ) != 1 ) return 2;
				for ( r = 0; r < rl; r++, rgb += 4 ) *rgb = bt;
			} else { // dump
				rl = bt; // dump length: same as bt
				if ( x + rl > imgwidth ) return 2; // sanity check
				for ( r = 0; r < rl; r++, rgb += 4 ) {
					if ( stream->read( &bt, 1, 1 ) != 1 ) return 2;
					*rgb = bt;
				}
			}
		}
	}
	
	// prediction and copy
	for ( x = 0, rgb = data; x < imgwidth; x++, rgb += 4 ) {
		rgb_process( rgb ); // prediction
		for ( c = 0; c < 4; c++ ) line[c][x] = rgb[c];
	}
	
	
	return 1;
}


/* -----------------------------------------------
	HDR encode RLE
	----------------------------------------------- */
INTERN inline int hdr_encode_line_rle( iostream* stream, int** line )
{
	static unsigned int* data = NULL;
	static int prev_width = 0;	
	unsigned int* rgb; // RGB + E
	unsigned int* dt;
	unsigned char bt = 0;
	int rl, nd;
	int x, rm, c;
	
	
	// allocate memory for line storage
	if ( prev_width != imgwidth ) {
		prev_width = imgwidth;
		data = ( unsigned int* ) calloc( imgwidth * 4, sizeof( int ) );
		if ( data == NULL ) return 2; // bad, but unlikely to happen anyways
	}
	
	// undo prediction and copy
	for ( x = 0, rgb = data; x < imgwidth; x++, rgb += 4 ) {
		for ( c = 0; c < 4; c++ ) rgb[c] = line[c][x];
		rgb_unprocess( rgb ); // prediction undo
	}
	
	// RLE compressed writing
	for ( c = 0; c < 4; c++ ) {
		for ( rm = imgwidth, dt = rgb = data+c; rm; ) {
			if ( rm > 1 ) {
				rm -= 2; dt += 8; 
				if ( *(dt-8) == *(dt-4) ) { // start of a run
					for ( rl = 2;
						( rm ) && ( rl < 0x7F ) && ( *dt == *(dt-4) );
						rl++, rm--, dt += 4 );
					bt = rl | 0x80; stream->write( &bt, 1, 1 );
					bt = *rgb; stream->write( &bt, 1, 1 );
					rgb = dt; 
				} else { // start of a dump
					for ( nd = 2, rl = 1;
						( rm ) && ( nd < 0x80 );
						nd++, rm--, dt += 4 ) {
						if ( *dt == *(dt-4) ) {
							if ( ++rl > 2 ) {
								nd -= (rl-1);
								rm += (rl-1);
								dt -= ((rl-1)*4);
								break;
							}
						} else rl = 1;
					}
					bt = nd; stream->write( &bt, 1, 1 );
					for ( ; rgb < dt; rgb += 4 ) {
						bt = *rgb; stream->write( rgb, 1, 1 );
					}
				}
			} else { // only one remains
				bt = 0x01; stream->write( &bt, 1, 1 );
				bt = *dt; stream->write( &bt, 1, 1 );
				break;
			}
		}
	}
	
	
	return 0;
}


/* -----------------------------------------------
	apply RGB prediction
	----------------------------------------------- */
INTERN inline void rgb_process( unsigned int* rgb ) {
	// RGB color component prediction
	for ( int c = 0; c < 3; c++ ) if ( c != 1 ) {
		if ( pnmax == 0 ) {
			rgb[c] |= 0x40000000;
			rgb[c] -= ( rgb[1] >> ( cmask[1]->s - cmask[c]->s ) );
			rgb[c] &= cmask[c]->m;
		} else {
			rgb[c] -= rgb[1];
			if ( rgb[c] < 0 ) rgb[c] += pnmax + 1;
		}
	}
}


/* -----------------------------------------------
	undo RGB prediction
	----------------------------------------------- */
INTERN inline void rgb_unprocess( unsigned int* rgb )
{
	// RGB color component prediction undo
	for ( int c = 0; c < 3; c++ ) if ( c != 1 ) {
		if ( pnmax == 0 ) {
			rgb[c] += ( rgb[1] >> ( cmask[1]->s - cmask[c]->s ) );
			rgb[c] &= cmask[c]->m;
		} else {
			rgb[c] += rgb[1];
			if ( rgb[c] > pnmax ) rgb[c] -= pnmax + 1;
		}
	}
}


/* -----------------------------------------------
	identify file from 2 bytes
	----------------------------------------------- */
INTERN inline void identify( const char* id, int* ft, int* st )
{
	*ft = F_UNK; *st = S_UNK;
	switch ( id[0] ) {
		case 'S':
			switch ( id[1] ) {
				case '4': *st = S_PBM; break;
				case '5': *st = S_PGM; break;
				case '6': *st = S_PPM; break;
				case 'M': *st = S_BMP; break;
				case '?': *st = S_HDR; break;
			}
			if ( *st != S_UNK ) *ft = F_PPN;
			break;
			
		case 'P':
			switch ( id[1] ) {
				case '4': *st = S_PBM; break;
				case '5': *st = S_PGM; break;
				case '6': *st = S_PPM; break;
			}
			if ( *st != S_UNK ) *ft = F_PNM;
			break;
			
		case 'B':
			if ( id[1] == 'M' ) {
				*st = S_BMP;
				*ft = F_PNM;
			}
			break;
			
		case '#':
			if ( id[1] == '?' ) {
				*st = S_HDR;
				*ft = F_PNM;
			}
			break;
	}
}


/* -----------------------------------------------
	scans headers of input filetypes (decision)
	----------------------------------------------- */
INTERN char* scan_header( iostream* stream )
{
	char* imghdr;
	// char id[2];
	
	// identify (again)
	// stream->read( &id, 2, 1 );
	// stream->rewind();
	// identify( id, &filetype, &subtype );
	
	// do the actual work
	switch( subtype ) {
		case S_PBM:
		case S_PGM:
		case S_PPM:
			imghdr = scan_header_pnm( stream );
			break;
		case S_BMP:
			imghdr = scan_header_bmp( stream );
			break;
		case S_HDR:
			imghdr = scan_header_hdr( stream );
			break;
		default: imghdr = NULL;
	}
	
	// check image dimensions	
	if ( ( errorlevel < 2 ) && ( ( imgwidth <= 0 ) || ( imgheight <= 0 ) ) ) {
		sprintf( errormessage, "image contains no data" );
		errorlevel = 2;
		free ( imghdr );
		return NULL;
	}
	
	return imghdr;
}


/* -----------------------------------------------
	scans headers of input filetypes (PNM)
	----------------------------------------------- */	
INTERN char* scan_header_pnm( iostream* stream )
{
	char* imghdr;
	char* ptr0;
	char* ptr1;
	char ws;
	int u, c;
	
	
	// endianness
	endian_l = E_BIG;
	
	// preset image header (empty string)
	imghdr = ( char* ) calloc( 1024, sizeof( char ) );
	if ( imghdr == NULL ) {
		sprintf( errormessage, MEM_ERRMSG );
		errorlevel = 2;
		return NULL;
	}	

	// parse and store header
	for ( ptr0 = imghdr, ptr1 = imghdr, u = 0;
		 ( stream->read( ptr1, 1, 1 ) == 1 ) && ( ptr1 - imghdr < 1023 );
		 ptr1++ ) {
		if ( ( ( *ptr1 == ' ' ) && ( *ptr0 != '#' ) ) || ( *ptr1 == '\n' ) || ( *ptr1 == '\r' ) ) {
			ws = *ptr1;
			*ptr1 = '\0';
			if ( ( strlen( ptr0 ) > 0 ) && ( *ptr0 != '#' ) ) {
				switch ( u ) {
					case 0: // first item (f.e. "P5")
						if ( strlen( ptr0 ) == 2 ) {
							switch ( ptr0[1] ) {
								case '4': imgbpp =  1; cmpc = 1; u++; break;
								case '5': imgbpp =  8; cmpc = 1; u++; break;
								case '6': imgbpp = 24; cmpc = 3; u++; break;
								default: u = -1; break;
							}
						} else u = -1;
						break;
					case 1: // image width
						( sscanf( ptr0, "%i", &imgwidthv ) == 1 ) ? u++ : u = -1; break;
					case 2: // image height
						( sscanf( ptr0, "%i", &imgheight ) == 1 ) ? u++ : u = -1; break;
					case 3: // maximum pixel value
						( sscanf( ptr0, "%ui", &pnmax ) == 1 ) ? u++ : u = -1; break;
				}
				if ( ( u == 3 ) && ( imgbpp == 1 ) ) u = 4;
				else if ( u == -1 ) break;
			}
			*ptr1 = ws;
			ptr0 = ptr1 + 1;
			if ( u == 4 ) break;
		}
	}
	
	// check data for trouble
	if ( u != 4 ) {
		sprintf( errormessage, "error in header structure (#%i)", u );
		errorlevel = 2;
		free ( imghdr );
		return NULL;
	} else *ptr0 = '\0';
	
	// process data - actual line width
	imgwidth = ( imgbpp == 1 ) ? ( (( imgwidthv + 7 ) / 8) * 8 ) : imgwidthv;
	
	// process data - pixel maximum value
	if ( pnmax > 0 ) {
		if ( pnmax >= 65536 ) {
			sprintf( errormessage, "maximum value (%i) not supported", (int) pnmax );
			errorlevel = 2;
			free ( imghdr );
			return NULL;
		}
		if ( pnmax >= 256 ) imgbpp *= 2;
		if ( ( pnmax == 0xFF ) || ( pnmax == 0xFFFF ) ) pnmax = 0;
	}
	
	// process data - masks
	switch ( imgbpp ) {
		case  1: cmask[0] = new cmp_mask( 0x1 ); break;
		case  8: cmask[0] = new cmp_mask( 0xFF ); break;
		case 16: cmask[0] = new cmp_mask( 0xFFFF ); break;
		case 24:
			cmask[0] = new cmp_mask( 0xFF0000 );
			cmask[1] = new cmp_mask( 0x00FF00 );
			cmask[2] = new cmp_mask( 0x0000FF );
			break;
		case 48:
			 for ( c = 0; c < 3; c++ ) { // bpp == 48
				cmask[c] = new cmp_mask();
				cmask[c]->m = 0xFFFF;
				cmask[c]->s = 16;
				cmask[c]->p = ( 2 - c ) * 16;
			}
			break;
		default: break;
	}
	
	
	return imghdr;
}


/* -----------------------------------------------
	scans headers of input filetypes (BMP)
	----------------------------------------------- */	
INTERN char* scan_header_bmp( iostream* stream )
{
	unsigned int bmask[4] = {
		0x00FF0000,
		0x0000FF00,
		0x000000FF,
		0xFF000000
	};
	unsigned int xmask = 0x00000000;
	char* imghdr;
	int bmpv = 0;
	int offs = 0;
	int hdrs = 0;
	int cmode = 0;
	int c;
	
	
	// endianness
	endian_l = E_LITTLE;
	
	// preset image header (empty array)
	imghdr = ( char* ) calloc( 2048, sizeof( char ) );
	if ( imghdr == NULL ) {
		sprintf( errormessage, MEM_ERRMSG );
		errorlevel = 2;
		return NULL;
	}
	
	// read the first 18 byte, determine BMP version and validity
	if ( stream->read( imghdr, 1, 18 ) == 18 ) {
		offs = INT_LE( imghdr + 0x0A );
		hdrs = INT_LE( imghdr + 0x0E );
		switch ( hdrs ) {
			case 12:  bmpv = 2; break;
			case 40:  bmpv = 3; break;
			case 56:  // special v4
			case 108: bmpv = 4; break;
			case 124: bmpv = 5; break;
			default:  bmpv = 0; break;
		}
	}
	
	// check plausibility
	if ( ( bmpv == 0 ) || ( offs > 2048 ) || ( offs < hdrs ) || ( stream->getsize() < offs ) ) {
		sprintf( errormessage, "unknown BMP version or unsupported file type" );
		errorlevel = 2;
		free ( imghdr );
		return NULL;
	}
	
	// read remainder data
	stream->read( imghdr + 18, 1, offs - 18 );
	if ( bmpv == 2 ) { // BMP version 2
		imgwidthv = SHORT_LE( imghdr + 0x12 );
		imgheight = SHORT_LE( imghdr + 0x14 );
		imgbpp = SHORT_LE( imghdr + 0x18 );
		cmode = 0;
	} else { // BMP versions 3 and 4
		imgwidthv = INT_LE( imghdr + 0x12 );
		imgheight = INT_LE( imghdr + 0x16 );
		imgbpp = SHORT_LE( imghdr + 0x1C );
		cmode = SHORT_LE( imghdr + 0x1E );
		bmpsize = INT_LE( imghdr + 0x22 ) + offs;
		pnmax = INT_LE( imghdr + 0x2E );
	}	
	
	// actual width and height
	if ( imgheight < 0 ) imgheight = -imgheight;
	switch ( imgbpp ) {
		//case 1: imgwidth = ( ( imgwidthv + 7 ) / 8 ) * 8; break;
		//case 4: imgwidth = ( ( imgwidthv + 1 ) / 2 ) * 2; break;
		case  1: imgwidth = ( ( imgwidthv + 31 ) / 32 ) * 32; break;
		case  4: imgwidth = ( ( imgwidthv +  7 ) /  8 ) *  8; break;
		case  8: imgwidth = ( ( imgwidthv +  3 ) /  4 ) *  4; break;
		case 16: imgwidth = ( ( imgwidthv +  1 ) /  2 ) *  2; break;
		default: imgwidth = imgwidthv; break;
	}
	
	// BMP compatibility check
	if ( ( ( cmode != 0 ) && ( cmode != 3 ) ) ||
		( ( cmode == 3 ) && ( imgbpp <= 8 ) ) ||
		( ( cmode == 0 ) && ( imgbpp == 16 ) ) ) {
		switch ( cmode ) {
			case 1: sprintf( errormessage, "BMP RLE8 decoding is not supported" ); break;
			case 2: sprintf( errormessage, "BMP RLE4 decoding is not supported" ); break;
			default: sprintf( errormessage, "probably not a proper BMP file" ); break;
		}
		errorlevel = 2;
		free ( imghdr );
		return NULL;
	}
	
	// read and check component masks
	cmpc = ( imgbpp <= 8 ) ? 1 : ( ( imgbpp < 32 ) ? 3 : 4 );
	if ( ( cmode == 3 ) && ( imgbpp > 8 ) ) {
		for ( c = 0; c < ( ( bmpv == 3 ) ? 3 : 4 ); c++ )
			bmask[c] = INT_LE( imghdr + 0x36 + ( 4 * c ) );
		cmpc = ( bmask[3] ) ? 4 : 3;
		// check mask integers
		for ( c = 0, xmask = 0; c < cmpc; xmask |= bmask[c++] );
		if ( pnmax > xmask ) {
			sprintf( errormessage, "bad BMP \"color used\" value: %i", (int) pnmax );
			errorlevel = 2;
			free ( imghdr );
			return NULL;
		}
		xmask ^= 0xFFFFFFFF >> ( 32 - imgbpp );
	}
	
	// generate masks
	if ( imgbpp <= 8 ) cmask[0] = new cmp_mask( ( imgbpp == 1 ) ? 0x1 : ( imgbpp == 4 ) ? 0xF : 0xFF );
	else for ( c = 0; c < cmpc; c++ ) {
		cmask[c] = new cmp_mask( bmask[c] );
		if ( ( cmask[c]->p == -1 ) || ( ( cmask[c]->p + cmask[c]->s ) > imgbpp ) || ( cmask[c]->s > 16 ) ) {
			sprintf( errormessage, "corrupted BMP component mask %i:%08X", c, bmask[c] );
			errorlevel = 2;
			free ( imghdr );
			return NULL;
		}
	}
	if ( xmask ) cmask[4] = new cmp_mask( xmask );

	
	return imghdr;
}


/* -----------------------------------------------
	scans headers of input filetypes (HDR)
	----------------------------------------------- */	
INTERN inline char* scan_header_hdr( iostream* stream )
{
	char* imghdr;
	char* ptr0;
	char* ptr1;
	char res[4];
	char ws;
	
	
	// preset image header (empty string)
	imghdr = ( char* ) calloc( 4096, sizeof( char ) );
	if ( imghdr == NULL ) {
		sprintf( errormessage, MEM_ERRMSG );
		errorlevel = 2;
		return NULL;
	}	

	// parse and store header
	for ( ptr0 = ptr1 = imghdr;; ptr1++ ) {
		if ( ( stream->read( ptr1, 1, 1 ) != 1 ) || ( ptr1 - imghdr >= 4096 ) ) {
			sprintf( errormessage, "unknown file or corrupted HDR header" );
			break;
		}
		if ( *ptr1 == '\n' ) {
			ws = *ptr1;
			*ptr1 = '\0';
			if ( ptr0 == imghdr ) { // check magic number
				if ( strncmp( ptr0+1, "?RADIANCE", 9 ) != 0 ) {
					sprintf( errormessage, "looked like HDR, but unknown" );
					break;
				}
			} else {
				if ( strncmp( ptr0, "FORMAT", 6 ) == 0 ) { // check format
					for ( ; ( ptr0 < ptr1 ) && ( *ptr0 != '=' ); ptr0++ );					
					for ( ptr0++; ( ptr0 < ptr1 ) && ( *ptr0 == ' ' ); ptr0++ );
					if ( ptr0 >= ptr1 ) {
						sprintf( errormessage, "bad HDR FORMAT string" );
						break;
					}
					if ( ( strncmp( ptr0, "32-bit_rle_rgbe", 15 ) != 0 ) &&
						( strncmp( ptr0, "32-bit_rle_xyze", 15 ) != 0 ) ) {
						sprintf( errormessage, "unknown HDR format: %s", ptr0 );
						break;
					}						
				} else if ( ( ( ptr0[0] == '-' ) || ( ptr0[0] == '+' ) ) &&
					( ( ptr0[1] == 'X' ) || ( ptr0[1] == 'Y' ) ) ) {
					if ( sscanf( ptr0, "%c%c %i %c%c %i",
						&res[0], &res[1], &imgheight,
						&res[2], &res[3], &imgwidthv ) == 6 )
						imgwidth = imgwidthv;
					if ( ( !imgwidth ) || ( ( res[2] != '-' ) && ( res[2] != '+' ) ) ||
						( ( res[3] != 'X' ) && ( res[3] != 'Y' ) ) || ( res[1] == res[3] ) ) {
						sprintf( errormessage, "bad HDR resolution string: %s", ptr0 );
						imgwidth = 0;
						break;
					}	
				}
			}
			*ptr1 = ws;
			ptr0 = ptr1 + 1;
			if ( imgwidth ) {
				*ptr0 = '\0';
				break;
			}
		}
	}
	
	// check for trouble
	if ( !imgwidth ) {
		errorlevel = 2;
		free ( imghdr );
		return NULL;
	}
	
	// imgbpp, cmpc, endianness
	imgbpp = 32;
	cmpc = 4;
	endian_l = E_BIG;
	
	// build masks
	cmask[0] = new cmp_mask( 0xFF000000 );
	cmask[1] = new cmp_mask( 0x00FF0000 );
	cmask[2] = new cmp_mask( 0x0000FF00 );
	cmask[3] = new cmp_mask( 0x000000FF );
	
	
	return imghdr;
}

/* ----------------------- End of side functions -------------------------- */

/* ----------------------- Begin of miscellaneous helper functions -------------------------- */


/* -----------------------------------------------
	displays progress bar on screen
	----------------------------------------------- */
#if !defined(BUILD_LIB)
INTERN inline void progress_bar( int current, int last )
{
	int barpos = ( ( current * BARLEN ) + ( last / 2 ) ) / last;
	int i;
	
	
	// generate progress bar
	fprintf( msgout, "[" );
	#if defined(_WIN32)
	for ( i = 0; i < barpos; i++ )
		fprintf( msgout, "\xFE" );
	#else
	for ( i = 0; i < barpos; i++ )
		fprintf( msgout, "X" );
	#endif
	for (  ; i < BARLEN; i++ )
		fprintf( msgout, " " );
	fprintf( msgout, "]" );
}
#endif

/* -----------------------------------------------
	creates filename, callocs memory for it
	----------------------------------------------- */
#if !defined(BUILD_LIB)
INTERN inline char* create_filename( const char* base, const char* extension )
{
	int len = strlen( base ) + ( ( extension == NULL ) ? 0 : strlen( extension ) + 1 ) + 1;	
	char* filename = (char*) calloc( len, sizeof( char ) );	
	
	// create a filename from base & extension
	strcpy( filename, base );
	set_extension( filename, extension );
	
	return filename;
}
#endif

/* -----------------------------------------------
	creates filename, callocs memory for it
	----------------------------------------------- */
#if !defined(BUILD_LIB)
INTERN inline char* unique_filename( const char* base, const char* extension )
{
	int len = strlen( base ) + ( ( extension == NULL ) ? 0 : strlen( extension ) + 1 ) + 1;	
	char* filename = (char*) calloc( len, sizeof( char ) );	
	
	// create a unique filename using underscores
	strcpy( filename, base );
	set_extension( filename, extension );
	while ( file_exists( filename ) ) {
		len += sizeof( char );
		filename = (char*) realloc( filename, len );
		add_underscore( filename );
	}
	
	return filename;
}
#endif

/* -----------------------------------------------
	changes extension of filename
	----------------------------------------------- */
#if !defined(BUILD_LIB)
INTERN inline void set_extension( const char* filename, const char* extension )
{
	char* extstr;
	
	// find position of extension in filename	
	extstr = ( strrchr( filename, '.' ) == NULL ) ?
		strrchr( filename, '\0' ) : strrchr( filename, '.' );
	
	// set new extension
	if ( extension != NULL ) {
		(*extstr++) = '.';
		strcpy( extstr, extension );
	}
	else
		(*extstr) = '\0';
}
#endif

/* -----------------------------------------------
	adds underscore after filename
	----------------------------------------------- */
#if !defined(BUILD_LIB)
INTERN inline void add_underscore( char* filename )
{
	char* tmpname = (char*) calloc( strlen( filename ) + 1, sizeof( char ) );
	char* extstr;
	
	// copy filename to tmpname
	strcpy( tmpname, filename );
	// search extension in filename
	extstr = strrchr( filename, '.' );
	
	// add underscore before extension
	if ( extstr != NULL ) {
		(*extstr++) = '_';
		strcpy( extstr, strrchr( tmpname, '.' ) );
	}
	else
		sprintf( filename, "%s_", tmpname );
		
	// free memory
	free( tmpname );
}
#endif

/* -----------------------------------------------
	checks if a file exists
	----------------------------------------------- */
INTERN inline bool file_exists( const char* filename )
{
	// needed for both, executable and library
	FILE* fp = fopen( filename, "rb" );
	
	if ( fp == NULL ) return false;
	else {
		fclose( fp );
		return true;
	}
}

/* ----------------------- End of miscellaneous helper functions -------------------------- */

/* ----------------------- Begin of developers functions -------------------------- */


#if !defined(BUILD_LIB) && defined(DEV_BUILD)
/* -----------------------------------------------
	Writes error info file
	----------------------------------------------- */
INTERN bool write_errfile( void )
{
	FILE* fp;
	char* fn;
	
	
	// return immediately if theres no error
	if ( errorlevel == 0 ) return true;
	
	// create filename based on errorlevel
	if ( errorlevel == 1 ) {
		fn = create_filename( filelist[ file_no ], "wrn.nfo" );
	}
	else {
		fn = create_filename( filelist[ file_no ], "err.nfo" );
	}
	
	// open file for output
	fp = fopen( fn, "w" );
	if ( fp == NULL ){
		sprintf( errormessage, FWR_ERRMSG );
		errorlevel = 2;
		return false;
	}
	free( fn );
	
	// write status and errormessage to file
	fprintf( fp, "--> error (level %i) in file \"%s\" <--\n", errorlevel, filelist[ file_no ] );
	fprintf( fp, "\n" );
	// write error specification to file
	fprintf( fp, " %s -> %s:\n", get_status( errorfunction ),
			( errorlevel == 1 ) ? "warning" : "error" );
	fprintf( fp, " %s\n", errormessage );
	
	// done, close file
	fclose( fp );
	
	
	return true;
}
#endif


#if !defined(BUILD_LIB) && defined(DEV_BUILD)
/* -----------------------------------------------
	Dumps image data to PGM
	----------------------------------------------- */
INTERN bool dump_pgm( void )
{
	FILE* fp;
	char* fn;
	
	char* imghdr = scan_header( str_in );
	int* data;
	int* ptr[4] = { NULL };
	char bt;
	
	
	// parse PNM file header
	if ( imghdr == NULL ) {
		errorlevel = 2;
		return false;
	}
	
	// allocate memory for data
	data = ( int* ) calloc( cmpc * imgwidth * imgheight, sizeof( int ) );
	if ( data == NULL ) {
		sprintf( errormessage, MEM_ERRMSG );
		errorlevel = 2;
		return false;
	}
	
	// open output file
	fn = create_filename( filelist[ file_no ], "dump.pgm" );
	fp = fopen( fn, "wb" );
	if ( fp == NULL ){
		free( data );
		sprintf( errormessage, FWR_ERRMSG );
		errorlevel = 2;
		return false;
	}
	free( fn );
	
	// read all data from input file...
	for ( int y = 0; y < imgheight; y++ ) {
		for ( int c = 0; c < cmpc; c++ )
			ptr[c] = data + ( c * imgheight + y ) * imgwidth;
		pnm_read_line( str_in, ptr );
	}
	
	// ...and dump it all to one file
	fprintf( fp, "P5\n%i %i\n255\n", imgwidth, imgheight * cmpc );
	for ( int c = 0; c < cmpc; c++ ) for ( int p = 0; p < imgheight * imgwidth; p++ ) {
		bt = data[ ( c * imgheight * imgwidth ) + p ] << ( 8 - cmask[c]->s );
		fwrite( &bt, 1, 1, fp );
	}
	
	// close file and free data
	fclose( fp );
	free( imghdr );
	free( data );
	
	
	return true;
}
#endif


#if !defined(BUILD_LIB) && defined(DEV_BUILD)
/* -----------------------------------------------
	Dumps info about image file
	----------------------------------------------- */
INTERN bool dump_info( void ) {
	FILE* fp;
	char* fn;
	
	char* imghdr = scan_header( str_in );
	
	
	// parse PNM file header
	if ( imghdr == NULL ) {
		errorlevel = 2;
		return false;
	}
	
	// open output file
	fn = create_filename( filelist[ file_no ], "nfo" );
	fp = fopen( fn, "wb" );
		if ( fp == NULL ){
		sprintf( errormessage, FWR_ERRMSG );
		errorlevel = 2;
		return false;
	}
	free( fn );
	
	// dump data to NFO file
	fprintf( fp, "<%s>\n", filelist[ file_no ] );
	fprintf( fp, "type : %scompressed\n", ( filetype == F_PPN ) ? "" : "un" );
	fprintf( fp, "sub  : " );
	switch ( subtype ) {
		case S_PBM: fprintf( fp, "PBM\n" ); break;
		case S_PGM: fprintf( fp, "PGM\n" ); break;
		case S_PPM: fprintf( fp, "PPM\n" ); break;
		case S_BMP: fprintf( fp, "BMP\n" ); break;
		case S_HDR: fprintf( fp, "HDR\n" ); break;
	}
	fprintf( fp, "dim  : %ix%ipx / %ibpp\n", imgwidth, imgheight, imgbpp );
	fprintf( fp, "vis  : %ix%ipx\n", imgwidthv, imgheight );
	fprintf( fp, "max  : %s%i\n", ( pnmax ) ? "" : "not set / ", (int) pnmax );	
	fprintf( fp, "#ch  : %i\n", cmpc );
	fprintf( fp, "\n" );
	for ( int c = 0; c < 5; c++ ) if ( cmask[c] != NULL ) {
		fprintf( fp, "channel #%i\n", c );
		fprintf( fp, "col  : " );
		switch ( c ) {
			case 0: fprintf( fp, ( cmpc == 1 ) ? "LUM\n" : "RED\n" ); break;
			case 1: fprintf( fp, "GREEN\n" ); break;
			case 2: fprintf( fp, "BLUE\n" ); break;
			case 3: fprintf( fp, ( subtype == S_HDR ) ? "EXP\n" : "ALPHA\n" ); break;
			case 4: fprintf( fp, "RES\n" ); break;
		}
		fprintf( fp, "mask : 0x%X << %i\n", cmask[c]->m, cmask[c]->p );
		fprintf( fp, "\n" );
	}
	
	// close file and free data
	fclose( fp );
	free( imghdr );
	
	
	return true;
}
#endif

/* ----------------------- End of developers functions -------------------------- */

/* ----------------------- End of file -------------------------- */
