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

/* This is a simple module to provide an easier to understand interface to
 * semaphores */

#include <stdio.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/ipc.h>
#include <sys/sem.h>
#include <errno.h>
#include "sem.h"

/* If we've got a version of glibc that doesn't define union semun, we do
 * it ourseleves like in semctl(2). Otherwise, fall back to the original
 * buffer behaviour of defining it (differetly!) only on some systems.
 *
 * mbuck@debian.org, 1999/08/29
 */
#if defined(__GNU_LIBRARY__) && defined(_SEM_SEMUN_UNDEFINED)
union semun {
	int val;			/* value for SETVAL              */
	struct semid_ds *buf;		/* buffer for IPC_STAT & IPC_SET */
	unsigned short int *array;	/* array for GETALL & SETALL     */
	struct seminfo *__buf;		/* buffer for IPC_INFO           */
};
#else
#if defined(SYS5) || defined(ultrix) || defined(_AIX)
union semun {
	int val;
	struct semid_ds *buf;
	ushort *array;
};
#endif
#endif   

/* IMPORTS */

/* Used to print error messages */
extern void report_proc();

/* Used to end the program - on error */
extern void byee();



/* Set a semaphore to a particular value - meant to be used before
 * first lock/unlock */
void
sem_set( sem_id, semn, val )
	int sem_id;
	int semn;
	int val;
{
	union semun arg;
	extern int errno;

	arg.val = val;

	errno = 0;
	semctl( sem_id, semn, SETVAL, arg );
	if( errno != 0 ){
		report_proc();
		perror( "internal error, sem_set" );
		byee( -1 );
	}
}
	
int
new_sems( nsems )
	int nsems;
{
	int sem;
	int i;

	sem = semget( IPC_PRIVATE, nsems, IPC_CREAT|S_IREAD|S_IWRITE );
	if( sem < 0 ){
		report_proc();
		perror( "internal error, couldn't create semaphore" );
		byee( -1 );
	}
	
	for( i = 0; i < nsems; i++ ){
		sem_set( sem, i, 1 );
	}

	return sem;
}

static void
do_sem( sem_id, pbuf, err )
	int sem_id;
	struct sembuf *pbuf;
	char *err;
{
	/* This just keeps us going in case of EINTR */
	while( 1 ){
		if( semop( sem_id, pbuf, 1 ) == -1 ){
			if( errno == EINTR ){
				continue;
			}
			report_proc();
			fprintf( stderr, "internal error pid %d, lock id %d\n",
				getpid(), sem_id );
			perror( err );
			byee( -1 );
		}
		return;
	}
}

void
lock( sem_id, semn )
	int sem_id;
	int semn;
{
	struct sembuf sembuf;

	sembuf.sem_num = semn;
	sembuf.sem_op = -1;
	sembuf.sem_flg = 0;

	do_sem( sem_id, &sembuf, "lock error" );
}

void
unlock( sem_id, semn )
	int sem_id;
	int semn;
{
	struct sembuf sembuf;

	sembuf.sem_num = semn;
	sembuf.sem_op = 1;
	sembuf.sem_flg = 0;

	do_sem( sem_id, &sembuf, "unlock error" );
}

void
remove_sems( sem_id )
	int sem_id;
{
	union semun arg;

	if( sem_id == -1 )
		return;

	arg.val = 0;
	if( semctl( sem_id, 0, IPC_RMID, arg ) == -1 ){
		report_proc();
		perror( "internal error, failed to remove semaphore" );
	}
}
