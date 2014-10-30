/*
    Buffer.  Very fast reblocking filter speedy writing of tapes.
    Copyright (C) 1990,1991  Lee McLoughlin

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 1, or (at your option)
    any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program; if not, write to the Free Software
    Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.

    Lee McLoughlin.
    Dept of Computing, Imperial College,
    180 Queens Gate, London, SW7 2BZ, UK.

    Email: L.McLoughlin@doc.ic.ac.uk
*/

/* This is a reblocking process, designed to try and read from stdin
 * and write to stdout - but to always try and keep the writing side
 * busy.  It is meant to try and stream tape writes.
 *
 * This program runs in two parts.  The reader and the writer.  They
 * communicate using shared memory with semaphores locking the access.
 * The shared memory implements a circular list of blocks of data.
 *
 * L.McLoughlin, Imperial College, 1990
 *
 * $Log: buffer.c,v $
 * Revision 1.19  1995/08/24  17:46:28  lmjm
 * Be more careful abour EINTR errors
 * Ingnore child processes dying.
 *
 * Revision 1.18  1993/08/25  19:07:31  lmjm
 * Added Brad Isleys patchs to read/sigchld handling.
 *
 * Revision 1.17  1993/06/04  10:26:39  lmjm
 * Cleaned up error reporting.
 * Spot when the child terminating is not mine but inherited from via exec.
 * Use only one semaphore group.
 * Print out why writer died on error.
 *
 * Revision 1.16  1993/05/28  10:47:32  lmjm
 * Debug shutdown sequence.
 *
 * Revision 1.15  1992/11/23  23:32:58  lmjm
 * Oops!  This should be outside the ifdef
 *
 * Revision 1.14  1992/11/23  23:29:58  lmjm
 * allow MAX_BLOCKSIZE and DEF_SHMEM to be configured
 *
 * Revision 1.13  1992/11/23  23:22:29  lmjm
 * Printf's use %lu where appropriate.
 *
 * Revision 1.12  1992/11/23  23:17:55  lmjm
 * Got rid of floats and use Kbyte counters instead.
 *
 * Revision 1.11  1992/11/03  23:11:51  lmjm
 * Forgot Andi Karrer on the patch list.
 *
 * Revision 1.10  1992/11/03  22:58:41  lmjm
 * Cleaned up the debugging prints.
 *
 * Revision 1.9  1992/11/03  22:53:00  lmjm
 * Corrected stdin, stout and showevery use.
 *
 * Revision 1.8  1992/11/03  22:41:34  lmjm
 * Added 2Gig patches from:
 * Andi Karrer <karrer@bernina.ethz.ch>
 * Rumi Zahir <rumi@iis.ethz.ch>
 * Christoph Wicki <wicki@iis.ethz.ch>
 *
 * Revision 1.7  1992/07/23  20:42:03  lmjm
 * Added 't' option to print total written at end.
 *
 * Revision 1.6  1992/04/07  19:57:30  lmjm
 * Added Kevins -B and -p options.
 * Turn off buffering to make -S output appear ok.
 * Added GPL.
 *
 * Revision 1.5  90/07/22  18:46:38  lmjm
 * Added system 5 support.
 * 
 * Revision 1.4  90/07/22  18:29:48  lmjm
 * Updated arg handling to be more consistent.
 * Make sofar printing size an option.
 * 
 * Revision 1.3  90/05/15  23:27:46  lmjm
 * Added -S option (show how much has been written).
 * Added -m option to specify how much shared memory to grab.
 * Now tries to fill this with blocks.
 * reader waits for writer to terminate and then frees the shared mem and sems.
 * 
 * Revision 1.2  90/01/20  21:37:59  lmjm
 * Reset default number of  blocks and blocksize for best thruput of
 * standard tar 10K Allow.
 * blocks number of blocks to be changed.
 * Don't need a hole in the circular queue since the semaphores prevent block
 * clash.
 * 
 * Revision 1.1  90/01/17  11:30:23  lmjm
 * Initial revision
 * 
 */
#include <stdlib.h>
#include <string.h>
#include <limits.h>
#include <unistd.h>
#include <stdio.h>
#include <signal.h>
#include <fcntl.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/ipc.h>
#include <sys/shm.h>
#include <sys/sem.h>
#include <sys/wait.h>
#include <sys/time.h>
#include "sem.h"

#ifndef lint
static char *rcsid = "$Header: /a/swan/home/swan/staff/csg/lmjm/src/buffer/RCS/buffer.c,v 1.19 1995/08/24 17:46:28 lmjm Exp lmjm $";
#endif

#if !(defined(__linux__) || defined(__GLIBC__) || defined(__GNU__))
extern char *shmat();
#endif

/* General macros */
#define TRUE 1
#define FALSE 0
#define K *1024
#define M *1024*1024

#if defined __GNUC__ || __STDC_VERSION__ >= 199901L
#define NUM_K_TYPE unsigned long long
#define NUM_K_FMT "llu"
#else
#define NUM_K_TYPE unsigned long
#define NUM_K_FMT "lu"
#endif

/* Some forward declarations */
void byee();
void start_reader_and_writer();
void parse_args();
void set_handlers();
void buffer_allocate();
void report_proc();
int do_size();
void get_buffer();
void reader();
void writer();
void writer_end();
void wait_for_writer_end();
void get_next_free_block();
void test_writer();
int fill_block();
void get_next_filled_block();
int data_to_write();
void write_blocks_to_stdout();
void write_block_to_stdout();
void pr_out();
void end_writer();

/* When showing print a note every this many bytes written */
int showevery = 0;
#define PRINT_EVERY 10 K

/* Pause after every write */
unsigned write_pause;

/* This is the inter-process buffer - it implements a circular list
 * of blocks. */

#ifdef AMPEX
#define MAX_BLOCKSIZE (4 M)
#define DEF_BLOCKSIZE MAX_BLOCKSIZE
#define DEF_SHMEM (32 M)
#endif


#ifndef MAX_BLOCKSIZE
#define MAX_BLOCKSIZE (512 K)
#endif
#ifndef DEF_BLOCKSIZE
#define DEF_BLOCKSIZE (10 K)
#endif

int blocksize = DEF_BLOCKSIZE;

/* Which process... in error reports*/
char *proc_string = "buffer";

/* Numbers of blocks in the queue. 
 */
#define MAX_BLOCKS 2048
int blocks = 1;
/* Circular increment of a buffer index */
#define INC(i) (((i)+1) == blocks ? 0 : ((i)+1))

/* Max amount of shared memory you can allocate - can't see a way to look
 * this up.
 */
#ifndef DEF_SHMEM
#define DEF_SHMEM (1 K K)
#endif
int max_shmem = DEF_SHMEM;

/* Just a flag to show unfilled */
#define NONE (-1)

/* the shared memory id of the buffer */
int buffer_id = NONE;
struct block {
	int bytes;
	char *data;
} *curr_block;

#define NO_BUFFER ((struct buffer *)-1)
struct buffer {
	/* Id of the semaphore group */
	int semid;

	/* writer will hang trying to lock this till reader fills in a block */
	int blocks_used_lock;
	/* reader will hang trying to lock this till writer empties a block */
	int blocks_free_lock;

	int next_block_in;
	int next_block_out;

	struct block block[ MAX_BLOCKS ];

	/* These actual space for the blocks is here - the array extends
	 * pass 1 */
	char data_space[ 1 ];
} *pbuffer = NO_BUFFER;
int buffer_size;

int fdin	= 0;
int fdout	= 1;
int in_ISCHR	= 0;
int out_ISCHR	= 0;
int padblock	= FALSE;
int writer_pid	= 0;
int reader_pid	= 0;
int free_shm	= 1;
int percent	= 0;
int debug	= 0;
int Zflag	= 0;
int writer_status = 0;
char *progname = "buffer";

char print_total = 0;
/* Number of K output */
NUM_K_TYPE outk = 0;

struct timeval starttime;

int
main( argc, argv )
	int argc;
	char **argv;
{
	parse_args( argc, argv );

	set_handlers();

	buffer_allocate();
	
	gettimeofday(&starttime, NULL);

	start_reader_and_writer();

	byee( 0 );

	/* NOTREACHED */
	exit( 0 );
}

void
parse_args( argc, argv )
	int argc;
	char **argv;
{
	int c;
	int iflag = 0;
	int oflag = 0;
	int zflag = 0;
	extern char *optarg;
	char blocks_given = FALSE;
	struct stat buf;


	while( (c = getopt( argc, argv, "BS:Zdm:s:b:p:u:ti:o:z:" )) != -1 ){
		switch( c ){
		case 't': /* Print to stderr the total no of bytes written */
			print_total++;
			break;
		case 'u': /* pause after write for given microseconds */
			write_pause = atoi( optarg );
			break;
		case 'B':   /* Pad last block */
			padblock = TRUE;
			break;
		case 'Z':   /* Zero by lseek on the tape device */
			Zflag = TRUE;
			break;
		case 'i': /* Input file */
			iflag++;
			if( iflag > 1 ){
				report_proc();
				fprintf( stderr, "-i given twice\n" );
				byee( -1 );
			}
			if( (fdin = open( optarg, O_RDONLY )) < 0 ){
				report_proc();
				perror( "cannot open input file" );
				fprintf( stderr, "filename: %s\n", optarg );
				byee ( -1 );
			}
			break;
		case 'o': /* Output file */
			oflag++;
			if( oflag > 1 ){
				report_proc();
				fprintf( stderr, "-o given twice\n" );
				byee( -1 );
			}
			if( (fdout = open( optarg, O_WRONLY | O_CREAT | O_TRUNC, 0666 )) < 0 ){
				report_proc();
				perror( "cannot open output file" );
				fprintf( stderr, "filename: %s\n", optarg );
				byee ( -1 );
			}
			break;
		case 'S':
			/* Show every once in a while how much is printed */
			showevery = do_size( optarg );
			if( showevery <= 0 )
				showevery = PRINT_EVERY;
			break;
		case 'd':	/* debug */
			debug++;
			if( debug == 1 ){
				setbuf( stdout, NULL );
				setbuf( stderr, NULL );
				fprintf( stderr, "debugging turned on\n" );
			}
			break;
		case 'm':
			/* Max size of shared memory lump */
			max_shmem = do_size( optarg );

			if( max_shmem < (sizeof( struct buffer ) + (blocksize * blocks)) ){
				fprintf( stderr, "max_shmem %d too low\n", max_shmem );
				byee( -1 );
			}
			break;
		case 'b':
			/* Number of blocks */
			blocks_given = TRUE;
			blocks = atoi( optarg );
			if( (blocks <= 0) || (MAX_BLOCKS < blocks) ){
				fprintf( stderr, "blocks %d out of range\n", blocks );
				byee( -1 );
			}
			break;
		case 'p':	/* percent to wait before dumping */
			percent = atoi( optarg );

			if( (percent < 0) || (100 < percent) ){
				fprintf( stderr, "percent %d out of range\n", percent );
				byee( -1 );
			}
			if( debug )
				fprintf( stderr, "percent set to %d\n", percent );
			break;
		case 'z':
			zflag++;
			/* FALL THRU */
		case 's':	/* Size of a block */
			blocksize = do_size( optarg );

			if( (blocksize <= 0) || (MAX_BLOCKSIZE < blocksize) ){
				fprintf( stderr, "blocksize %d out of range\n", blocksize );
				byee( -1 );
			}
			break;
		default:
			fprintf( stderr, "Usage: %s [-B] [-t] [-S size] [-m memsize] [-b blocks] [-p percent] [-s blocksize] [-u pause] [-i infile] [-o outfile] [-z size] [-Z] [-d]\n",
				progname );
			fprintf( stderr, "-B = blocked device - pad out last block\n" );
			fprintf( stderr, "-t = show total amount written at end\n" );
			fprintf( stderr, "-S size = show amount written every size bytes\n" );
			fprintf( stderr, "-m size = size of shared mem chunk to grab\n" );
			fprintf( stderr, "-b num = number of blocks in queue\n" );
			fprintf( stderr, "-p percent = don't start writing until percent blocks filled\n" );
			fprintf( stderr, "-s size = size of a block\n" );
			fprintf( stderr, "-u usecs = microseconds to sleep after each write\n" );
			fprintf( stderr, "-i infile = file to read from\n" );
			fprintf( stderr, "-o outfile = file to write to\n" );
			fprintf( stderr, "-z size = combined -S/-s flag\n" );
			fprintf( stderr, "-Z = seek to beginning of output after each 1GB (for some tape drives)\n" );
			fprintf( stderr, "-d = print debug information to stderr\n" );
			byee( -1 );
		}
	}
	
	if (argc > optind) {
		fprintf( stderr, "too many arguments\n" );
		byee( -1 );
	}

	if (zflag) showevery = blocksize;

	/* If -b was not given try and work out the max buffer size */
	if( !blocks_given ){
		blocks = (max_shmem - sizeof( struct buffer )) / blocksize;
		if( blocks <= 0 ){
			fprintf( stderr, "Cannot handle blocks that big, aborting!\n" );
			byee( -1 );
		}
		if( MAX_BLOCKS < blocks  ){
			fprintf( stderr, "Cannot handle that many blocks, aborting!\n" );
			byee( -1 );
		}
	}

	/* check if fdin or fdout are character special files */
	if( fstat( fdin, &buf ) != 0 ){
		report_proc();
		perror( "can't stat input file" );
		byee( -1 );
	}
	in_ISCHR = S_ISCHR( buf.st_mode );
	if( fstat( fdout, &buf ) != 0 ){
		report_proc();
		perror( "can't stat output file" );
		byee( -1 );
	}
	out_ISCHR = S_ISCHR( buf.st_mode );
}

/* The interrupt handler */
void
shutdown()
{
	static int shutting;
	if( shutting ){
		if( debug )
			fprintf( stderr, "%s: ALREADY SHUTTING!\n", proc_string );
		return;
	}
	shutting = 1;
	if( debug )
		fprintf( stderr, "%s: shutdown on signal\n", proc_string );

	byee( -1 );
}

/* Shutdown because the child has ended */
void
child_shutdown()
{
	/* Find out which child has died.  (They may not be my
	 * children if buffer was exec'd on top of something that had
	 * childred.)
	 */
	int deadpid;

	while( (deadpid = waitpid( -1, &writer_status, WNOHANG )) &&
		deadpid != -1 && deadpid != 0 ){
		if( debug > 2 )
			fprintf( stderr, "child_shutdown %d: 0x%04x\n", deadpid, writer_status );
		if( deadpid == writer_pid ){
			if( debug > 2 )
				fprintf( stderr, "writer has ended\n" );
			writer_pid = 0;
			byee( 0 );
		}
	}
}

void
set_handlers()
{
	if( debug )
		fprintf( stderr, "%s: setting handlers\n", proc_string );

	signal( SIGHUP, shutdown );
	signal( SIGINT, shutdown );
	signal( SIGQUIT, shutdown );
	signal( SIGTERM, shutdown );
#ifdef SIGCHLD
	signal( SIGCHLD, child_shutdown );
#else
#ifdef SIGCLD
	signal( SIGCLD, child_shutdown );
#endif
#endif
}

void
buffer_allocate()
{
	/* Allow for the data space */
	buffer_size = sizeof( struct buffer ) +
		((blocks * blocksize) - sizeof( char ));

	/* Create the space for the buffer */
	buffer_id = shmget( IPC_PRIVATE,
			   buffer_size,
			   IPC_CREAT|S_IREAD|S_IWRITE );
	if( buffer_id < 0 ){
		report_proc();
		perror( "couldn't create shared memory segment" );
		byee( -1 );
	}

	get_buffer();

	if( debug )
		fprintf( stderr, "%s pbuffer is 0x%08lx, buffer_size is %d [%d x %d]\n",
			proc_string,
			(unsigned long)pbuffer, buffer_size, blocks, blocksize );

#ifdef SYS5
	memset( pbuffer->data_space, '\0', blocks * blocksize );
#else
	bzero( pbuffer->data_space, blocks * blocksize );
#endif
	pbuffer->semid = -1;
	pbuffer->blocks_used_lock = -1;
	pbuffer->blocks_free_lock = -1;

	pbuffer->semid = new_sems( 2 );	/* Get a read and a write sem */
	pbuffer->blocks_used_lock = 0;
	/* Start it off locked - it is unlocked when a buffer gets filled in */
	lock( pbuffer->semid, pbuffer->blocks_used_lock );

	pbuffer->blocks_free_lock = 1;
	/* start this off so lock() can be called on it for each block
	 * till all the blocks are used up */
	/* Initializing the semaphore to "blocks - 1" causes a hang when using option
	 * "-p 100" because it always keeps one block free, so we'll never reach 100% fill
	 * level. However, there doesn't seem to be a good reason to keep one block free,
	 * so we initialize the semaphore to "blocks" instead.
	 * <mbuck@debian.org> 2004-01-11
	 */	
#if 0
	sem_set( pbuffer->semid, pbuffer->blocks_free_lock, blocks - 1 );
#else
	sem_set( pbuffer->semid, pbuffer->blocks_free_lock, blocks );
#endif
	
	/* Do not detach the shared memory, but leave it mapped. It will be inherited
	 * over fork just fine and this ensures that it's mapped at the same address
	 * both in the reader and writer. The original code did a shmdt() here followed
	 * by shmat() both in the reader and writer and relied on it getting mapped at
	 * the same address in both processes, which of course isn't guaranteed and
	 * actually did fail under some unknown circumstances on amd64.
	 * <mbuck@debian.org> 2008-05-09
	 */
#if 0
	/* Detattach the shared memory so the fork doesnt do anything odd */
	shmdt( (char *)pbuffer );
	pbuffer = NO_BUFFER;
#endif
}

void
buffer_remove()
{
	static char removing = FALSE;

	/* Avoid accidental recursion */
	if( removing )
		return;
	removing = TRUE;

	/* Buffer not yet created */
	if( buffer_id == NONE )
		return;

	/* There should be a buffer so this must be after its detached it
	 * but before the fork picks it up */
	if( pbuffer == NO_BUFFER )
		get_buffer();

	if( debug )
		fprintf( stderr, "%s: removing semaphores and buffer\n", proc_string );
	remove_sems( pbuffer->semid );
	
	if( shmctl( buffer_id, IPC_RMID, (struct shmid_ds *)0 ) == -1 ){
		report_proc();
		perror( "failed to remove shared memory buffer" );
	}
}

void
get_buffer()
{
	int b;

	/* Grab the buffer space */
	pbuffer = (struct buffer *)shmat( buffer_id, (char *)0, 0 );
	if( pbuffer == NO_BUFFER ){
		report_proc();
		perror( "failed to attach shared memory" );
		byee( -1 );
	}

	/* Setup the data space pointers */
	for( b = 0; b < blocks; b++ )
		pbuffer->block[ b ].data =
			&pbuffer->data_space[ b * blocksize ];

}

void
start_reader_and_writer()
{
	fflush( stdout );
	fflush( stderr );

	if( (writer_pid = fork()) == -1 ){
		report_proc();
		perror( "unable to fork" );
		byee( -1 );
	}
	else if( writer_pid == 0 ){
		free_shm = 0;
		proc_string = "buffer (writer)";
		reader_pid = getppid();

		/* Never trust fork() to propogate signals - reset them */
		set_handlers();

		writer();
	}
	else {
		proc_string = "buffer (reader)";
		reader();

		wait_for_writer_end();
	}
}

/* Read from stdin into the buffer */
void
reader()
{
	if( debug )
		fprintf( stderr, "R: Entering reader\n" );

	while( 1 ){
		get_next_free_block();
		if( ! fill_block() )
			break;
	}

	if( debug )
		fprintf( stderr, "R: Exiting reader\n" );
}

void
get_next_free_block()
{
	test_writer();

	/* Maybe wait till there is room in the buffer */
	lock( pbuffer->semid, pbuffer->blocks_free_lock );

	curr_block = &pbuffer->block[ pbuffer->next_block_in ];

	pbuffer->next_block_in = INC( pbuffer->next_block_in );
}

int
fill_block()
{
	int bytes = 0;
	char *start;
	int toread;
	static char eof_reached = 0;
	
	if( eof_reached ){
		curr_block->bytes = 0;
		unlock( pbuffer->semid, pbuffer->blocks_used_lock );
		return 0;
	}

	start = curr_block->data;
	toread = blocksize;

	/* Fill the block with input.  This reblocks the input. */
	while( toread != 0 ){
		bytes = read( fdin, start, toread );
		if( bytes <= 0 ){
			/* catch interrupted system calls for death
			 * of children in pipeline */
			if( bytes < 0 && errno == EINTR )
				continue;
			break;
		}
		start += bytes;
		toread -= bytes;
	}

	if( bytes == 0 )
		eof_reached = 1;

	if( bytes < 0 ){
		report_proc();
		perror( "failed to read input" );
		byee( -1 );
	}

	/* number of bytes available. Zero will be taken as eof */
	if( !padblock || toread == blocksize )
		curr_block->bytes = blocksize - toread;
	else {
		if( toread ) bzero( start, toread );
		curr_block->bytes = blocksize;
	}

	if( debug > 1 )
		fprintf( stderr, "R: got %d bytes\n", curr_block->bytes );

	unlock( pbuffer->semid, pbuffer->blocks_used_lock );

	return curr_block->bytes;
}

/* Write the buffer to stdout */
void
writer()
{
	int filled = 0;
	int maxfilled = (blocks * percent) / 100;
	int first_block = 0;

	if( debug )
		fprintf( stderr, "\tW: Entering writer\n blocks = %d\n maxfilled = %d\n",
			blocks,
			maxfilled );

	while( 1 ){
		if( !filled )
			first_block = pbuffer->next_block_out;
		get_next_filled_block();
		if( !data_to_write() )
			break;

		filled++;
		if( debug > 1 )
			fprintf( stderr, "W: filled = %d\n", filled );
		if( filled >= maxfilled ){
			if( debug > 1 )
				fprintf( stderr, "W: writing\n" );
			write_blocks_to_stdout( filled, first_block );
			filled = 0;
		}
	}

	write_blocks_to_stdout( filled, first_block );

	if( showevery ){
		pr_out();
		fprintf( stderr, "\n" );
	}

	if( print_total ){
		fprintf( stderr, "Kilobytes Out %" NUM_K_FMT "\n", outk );
	}

	if( debug )
		fprintf( stderr, "\tW: Exiting writer\n" );
}

void
get_next_filled_block()
{
	/* Hang till some data is available */
	lock( pbuffer->semid, pbuffer->blocks_used_lock );

	curr_block = &pbuffer->block[ pbuffer->next_block_out ];

	pbuffer->next_block_out = INC( pbuffer->next_block_out );
}

int
data_to_write()
{
	return curr_block->bytes;
}

void
write_blocks_to_stdout( filled, first_block )
	int filled;
	int first_block;
{
	pbuffer->next_block_out = first_block;

	while( filled-- ){
		curr_block = &pbuffer->block[ pbuffer->next_block_out ];
		pbuffer->next_block_out = INC( pbuffer->next_block_out );
		write_block_to_stdout();
	}
}

void
write_block_to_stdout()
{
	unsigned long out = 0;
	static unsigned long last_gb = 0;
	static NUM_K_TYPE next_k = 0;
	int written;

	if( next_k == 0 && showevery ){
		if( debug > 3 )
			fprintf( stderr, "W: next_k = %" NUM_K_FMT " showevery = %d\n", next_k, showevery );
		showevery = showevery / 1024;
		next_k = showevery;
	}

	if( (written = write( fdout, curr_block->data, curr_block->bytes )) != curr_block->bytes ){
		report_proc();
		perror( "write of data failed" );
		fprintf( stderr, "bytes to write=%d, bytes written=%d, total written %10" NUM_K_FMT "K\n", curr_block->bytes, written, outk );
		byee( -1 );
	}

	if( write_pause ){
		usleep( write_pause );
	}

	out = curr_block->bytes / 1024;
	outk += out;
	last_gb += out;

	/*
	 * on character special devices (tapes), do an lseek() every 1 Gb,
	 * to overcome the 2Gb limit. This resets the file offset to
	 * zero, but -- at least on exabyte SCSI drives -- does not perform
	 * any actual action on the tape.
	 */
	if( Zflag && last_gb >= 1 K K ){
		last_gb = 0;
		if( in_ISCHR )
			(void) lseek( fdin, 0, SEEK_SET);
		if( out_ISCHR )
			(void) lseek( fdout, 0, SEEK_SET);
	}
	if( showevery ){
		if( debug > 3 )
			fprintf( stderr, "W: outk = %" NUM_K_FMT ", next_k = %" NUM_K_FMT "\n",
				outk, next_k );
		if( outk >= next_k ){
			pr_out();
			next_k += showevery;
		}
	}

	unlock( pbuffer->semid, pbuffer->blocks_free_lock );
}


void
byee( exit_val )
	int exit_val;
{
	if( writer_pid != 0 ){
		if( exit_val != 0 ){
			/* I am shutting down due to an error.
			 * Shut the writer down or else it will try to access
			 * the freed up locks */
			end_writer();
		}
		wait_for_writer_end();
	}

	if( free_shm ){
		buffer_remove();
	}

#ifdef SIGCHLD
	signal( SIGCHLD, SIG_IGN );
#else
#ifdef SIGCLD
	signal( SIGCLD, SIG_IGN );
#endif
#endif

	/* If the child died or was killed show this in the exit value */
	if( writer_status ){
		if( WEXITSTATUS( writer_status ) || WIFSIGNALED( writer_status ) ){
			if( debug )
				fprintf( stderr, "writer died badly: 0x%04x\n", writer_status );
			exit( -2 );
		}
	}

	exit( exit_val );
}

/* Kill off the writer */
void
end_writer()
{
	if( writer_pid )
		kill( writer_pid, SIGHUP );
}

void
wait_for_writer_end()
{
	int deadpid;

	/* Now wait for the writer to finish */
	while( writer_pid && ((deadpid = wait( &writer_status )) != writer_pid) &&
		deadpid != -1 )
		;
}

void
test_writer()
{
	/* Has the writer gone unexpectedly? */
	if( writer_pid == 0 ){
		fprintf( stderr, "writer has died unexpectedly\n" );
		byee( -1 );
	}
}

/* Given a string of <num>[<suff>] returns a num
 * suff =
 *   m/M for 1meg
 *   k/K for 1k
 *   b/B for 512
 */
int
do_size( arg )
	char *arg;
{
	int ret = 0;

	char unit = '\0';
	sscanf( arg, "%d%c", &ret, &unit );

	switch( unit ){
	case 'm':
	case 'M':
		ret = ret K K;
		break;
	case 'k':
	case 'K':
		ret = ret K;
		break;
	case 'b':
	case 'B':
		ret *= 512;
		break;
	}
	
	return ret;
}

void
pr_out()
{
	struct timeval now;
	unsigned long ms_delta, k_per_s;
	
	gettimeofday(&now, NULL);
	ms_delta = (now.tv_sec - starttime.tv_sec) * 1000
		   + (now.tv_usec - starttime.tv_usec) / 1000;
	if (ms_delta) {
		/* Use increased accuracy for small amounts of data,
		 * decreased accuracy for *huge* throughputs > 4.1GB/s
		 * to avoid division by 0. This will overflow if your
		 * machine's throughput exceeds 4TB/s - you deserve to
		 * lose if you're still using 32 bit longs on such a
		 * beast ;-)
		 * <mbuck@debian.org>
		 */
		if (outk < ULONG_MAX / 1000) {
			k_per_s = (outk * 1000) / ms_delta;
		} else if (ms_delta >= 1000) {
			k_per_s = outk / (ms_delta / 1000);
		} else {
			k_per_s = (outk / ms_delta) * 1000;
		}
		fprintf( stderr, " %10" NUM_K_FMT "K, %10luK/s\r", outk, k_per_s );
	} else {
		if (outk) {
			fprintf( stderr, " %10" NUM_K_FMT "K,          ?K/s\r", outk );
		} else {
			fprintf( stderr, "          0K,          0K/s\r");
		}
	}
}

#ifdef SYS5
#include <sys/time.h>

#ifndef __alpha
bzero( b, l )
	char *b;
	unsigned l;
{
	memset( b, '\0', l );
}
#endif /* __alpha */

usleep_back()
{
}

void
usleep( u )
	unsigned u;
{
	struct itimerval old, t;
	signal( SIGALRM, usleep_back );
	t.it_interval.tv_sec = 0;
	t.it_interval.tv_usec = 0;
	t.it_value.tv_sec = u / 1000000;
	t.it_value.tv_usec = u % 1000000;
	setitimer( ITIMER_REAL, &t, &old );
	pause();
	setitimer( ITIMER_REAL, &old, NULL );
}
#endif

/* Called before error reports */
void
report_proc()
{
	fprintf( stderr, "%s: ", proc_string );
}
