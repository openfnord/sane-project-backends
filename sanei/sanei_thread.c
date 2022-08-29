/* sane - Scanner Access Now Easy.
   Copyright (C) 1998-2001 Yuri Dario
   Copyright (C) 2003-2004 Gerhard Jaeger (pthread/process support)
   This file is part of the SANE package.

   This program is free software; you can redistribute it and/or
   modify it under the terms of the GNU General Public License as
   published by the Free Software Foundation; either version 2 of the
   License, or (at your option) any later version.

   This program is distributed in the hope that it will be useful, but
   WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
   General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program.  If not, see <https://www.gnu.org/licenses/>.

   As a special exception, the authors of SANE give permission for
   additional uses of the libraries contained in this release of SANE.

   The exception is that, if you link a SANE library with other files
   to produce an executable, this does not by itself cause the
   resulting executable to be covered by the GNU General Public
   License.  Your use of that executable is in no way restricted on
   account of linking the SANE library code into it.

   This exception does not, however, invalidate any other reasons why
   the executable file might be covered by the GNU General Public
   License.

   If you submit changes to SANE to the maintainers to be included in
   a subsequent release, you agree by submitting the changes that
   those changes may be distributed with this exception intact.

   If you write modifications of your own for SANE, it is your choice
   whether to permit this exception to apply to your modifications.
   If you do not wish that, delete this exception notice.

   OS/2
   Helper functions for the OS/2 port (using threads instead of forked
   processes). Don't use them in the backends, they are used automatically by
   macros.

   Other OS:
   use this lib, if you intend to let run your reader function within its own
   task (thread or process). Depending on the OS and/or the configure settings
   pthread or fork is used to achieve this goal.
*/

#include "../include/sane/config.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <signal.h>
#ifdef HAVE_UNISTD_H
# include <unistd.h>
#endif
#ifdef HAVE_OS2_H
# define INCL_DOSPROCESS
# include <os2.h>
#endif
#ifdef __BEOS__
# undef USE_PTHREAD /* force */
# include <kernel/OS.h>
#endif
#if !defined USE_PTHREAD && !defined HAVE_OS2_H && !defined __BEOS__
# include <sys/wait.h>
#endif

#define BACKEND_NAME sanei_thread      /**< name of this module for debugging */

#include "../include/sane/sane.h"
#include "../include/sane/sanei_debug.h"
#include "../include/sane/sanei_thread.h"

#ifndef _VAR_NOT_USED
# define _VAR_NOT_USED(x)	((x)=(x))
#endif

typedef struct {

	int         (*func)( void* );
	SANE_Status  status;
	void        *func_data;

} ThreadDataDef, *pThreadDataDef;

static ThreadDataDef td;

/** for init issues - here only for the debug output
 */
void
sanei_thread_init( void )
{
	DBG_INIT();

	memset( &td, 0, sizeof(ThreadDataDef));
	td.status = SANE_STATUS_GOOD;
}

SANE_Bool
sanei_thread_is_forked( void )
{
#if defined USE_PTHREAD || defined HAVE_OS2_H || defined __BEOS__
	return SANE_FALSE;
#else
	return SANE_TRUE;
#endif
}

/* Use this to mark a SANE_Pid as invalid instead of marking with -1.
 */
#ifdef USE_PTHREAD
static void
sanei_thread_set_invalid( SANE_Pid *pid )
{
  pid->is_valid = SANE_FALSE;
}
#endif

/* Return if PID is a valid PID or not. */
SANE_Bool
sanei_thread_is_valid( SANE_Pid pid )
{
    return pid.is_valid;
}

/* pthread_t is not an integer on all platform.  Do our best to return
 * a PID-like value from structure.  On platforms were it is an integer,
 * return that.
 *
 * Note: perhaps on platforms where pthread_t is a structure, compute a hash
 * of the structure. It's not ideal but it is a thought.
 *
 */
long
sanei_thread_pid_to_long( SANE_Pid pid )
{
	int rc;

	if (!pid.is_valid)
	{
		return 0l;
	}
#ifdef WIN32
#ifdef WINPTHREAD_API
	rc = (long) pid.pid;
#else
	rc = pid.pid.p;
#endif
#else
	rc = (long) pid.pid;
#endif

	return rc;
}

int
sanei_thread_kill( SANE_Pid pid )
{
	if (!pid.is_valid)
	{
		return -1;
	}
	DBG(2, "sanei_thread_kill() will kill %ld\n",
	    sanei_thread_pid_to_long(pid));
#ifdef USE_PTHREAD
#if defined (__APPLE__) && defined (__MACH__)
	return pthread_kill((pthread_t)pid.pid, SIGUSR2);
#else
	return pthread_cancel((pthread_t)pid.pid);
#endif
#elif defined HAVE_OS2_H
	return DosKillThread(pid.pid);
#else
	return kill( pid.pid, SIGTERM );
#endif
}

#ifdef HAVE_OS2_H

static void
local_thread( void *arg )
{
	pThreadDataDef ltd = (pThreadDataDef)arg;

	DBG( 2, "thread started, calling func() now...\n" );
	ltd->status = ltd->func( ltd->func_data );

	DBG( 2, "func() done - status = %d\n", ltd->status );
	_endthread();
}

/*
 * starts a new thread or process
 * parameters:
 *
 * func  address of reader function
 * args  pointer to scanner data structure
 *
 */
SANE_Pid
sanei_thread_begin( int (*func)(void *args), void* args )
{
	SANE_Pid pid = {SANE_FALSE, -1};

	td.func      = func;
	td.func_data = args;

	pid.pid = _beginthread( local_thread, NULL, 1024*1024, (void*)&td );
	if ( pid.pid == -1 )
	{
		DBG( 1, "_beginthread() failed\n" );
		return pid;
	}

	DBG( 2, "_beginthread() created thread %d\n", pid );
	pid.is_valid = SANE_TRUE;

	return pid;
}

SANE_Pid
sanei_thread_waitpid( SANE_Pid pid, int *status )
{
	if (!pid.is_valid)
	{
		return pid;
	}

	if (status)
		*status = 0;
	return pid; /* DosWaitThread( (TID*) &pid, DCWW_WAIT);*/
}

int
sanei_thread_sendsig( SANE_Pid pid, int sig )
{
	if (!pid.is_valid)
	{
		return -1;
	}
	return 0;
}

#elif defined __BEOS__

static int32
local_thread( void *arg )
{
	pThreadDataDef ltd = (pThreadDataDef)arg;

	DBG( 2, "thread started, calling func() now...\n" );
	ltd->status = ltd->func( ltd->func_data );

	DBG( 2, "func() done - status = %d\n", ltd->status );
	return ltd->status;
}

/*
 * starts a new thread or process
 * parameters:
 * star  address of reader function
 * args  pointer to scanner data structure
 *
 */
SANE_Pid
sanei_thread_begin( int (*func)(void *args), void* args )
{
	SANE_Pid pid = {SANE_FALSE, B_OK};

	td.func      = func;
	td.func_data = args;

	pid.pid = spawn_thread( local_thread, "sane thread (yes they can be)", B_NORMAL_PRIORITY, (void*)&td );
	if ( pid.pid < B_OK ) {
		DBG( 1, "spawn_thread() failed\n" );
		return pid;
	}
	if ( resume_thread(pid.pid) < B_OK ) {
		DBG( 1, "resume_thread() failed\n" );
		return pid;
	}

	DBG( 2, "spawn_thread() created thread %d\n", pid );
	pid.is_valid = SANE_TRUE;

	return pid;
}

SANE_Pid
sanei_thread_waitpid( SANE_Pid pid, int *status )
{
	int32 st;

	if (!pid.is_valid)
	{
		return pid;
	}

	if ( wait_for_thread(pid.pid, &st) < B_OK )
	{
		pid.is_valid = SANE_FALSE;
		return pid;
	}
	if ( status )
		*status = (int)st;

	return pid;
}

int
sanei_thread_sendsig( SANE_Pid pid, int sig )
{
	if (!pid.is_valid)
	{
		return -1;
	}

	if (sig == SIGKILL)
		sig = SIGKILLTHR;

	return kill(pid.pid, sig);
}

#else /* HAVE_OS2_H, __BEOS__ */

#ifdef USE_PTHREAD

/* seems to be undefined in MacOS X */
#ifndef PTHREAD_CANCELED
# define PTHREAD_CANCELED ((void *) -1)
#endif

/**
 */
#if defined (__APPLE__) && defined (__MACH__)
static void
thread_exit_handler( int signo )
{
	DBG( 2, "signal(%i) caught, calling pthread_exit now...\n", signo );
	pthread_exit( PTHREAD_CANCELED );
}
#endif


static void*
local_thread( void *arg )
{
	static int     status;
	pThreadDataDef ltd = (pThreadDataDef)arg;

#if defined (__APPLE__) && defined (__MACH__)
	struct sigaction act;

	sigemptyset(&(act.sa_mask));
	act.sa_flags   = 0;
	act.sa_handler = thread_exit_handler;
	sigaction( SIGUSR2, &act, 0 );
#else
	int old;

	pthread_setcancelstate( PTHREAD_CANCEL_ENABLE, &old );
	pthread_setcanceltype ( PTHREAD_CANCEL_ASYNCHRONOUS, &old );
#endif

	DBG( 2, "thread started, calling func() now...\n" );

	status = ltd->func( ltd->func_data );

	/* so sanei_thread_get_status() will work correctly... */
	ltd->status = status;

	DBG( 2, "func() done - status = %d\n", status );

	/* return the status, so pthread_join is able to get it*/
	pthread_exit((void*)&status );
}

/**
 */
static void
restore_sigpipe( void )
{
#ifdef SIGPIPE
	struct sigaction act;

	if( sigaction( SIGPIPE, NULL, &act ) == 0 ) {

		if( act.sa_handler == SIG_IGN ) {
			sigemptyset( &act.sa_mask );
			act.sa_flags   = 0;
			act.sa_handler = SIG_DFL;

			DBG( 2, "restoring SIGPIPE to SIG_DFL\n" );
			sigaction( SIGPIPE, &act, NULL );
		}
	}
#endif
}

#else /* the process stuff */

static int
eval_wp_result( SANE_Pid pid, int wpres, int pf )
{
	int retval = SANE_STATUS_IO_ERROR;

	if( wpres == pid.pid ) {

		if( WIFEXITED(pf)) {
			retval = WEXITSTATUS(pf);
		} else {

			if( !WIFSIGNALED(pf)) {
				retval = SANE_STATUS_GOOD;
			} else {
				DBG( 1, "Child terminated by signal %d\n", WTERMSIG(pf));
				if( WTERMSIG(pf) == SIGTERM )
					retval = SANE_STATUS_GOOD;
			}
		}
	}
	return retval;
}
#endif

SANE_Pid
sanei_thread_begin( int (func)(void *args), void* args )
{
#ifdef USE_PTHREAD
	int result;
	SANE_Pid thread;

#ifdef SIGPIPE
	struct sigaction act;

	/* if signal handler for SIGPIPE is SIG_DFL, replace by SIG_IGN */
	if( sigaction( SIGPIPE, NULL, &act ) == 0 ) {

		if( act.sa_handler == SIG_DFL ) {
			sigemptyset( &act.sa_mask );
			act.sa_flags   = 0;
			act.sa_handler = SIG_IGN;

			DBG( 2, "setting SIGPIPE to SIG_IGN\n" );
			sigaction( SIGPIPE, &act, NULL );
		}
	}
#endif

	td.func      = func;
	td.func_data = args;

	result = pthread_create( &thread.pid, NULL, local_thread, &td );
	usleep( 1 );

	if ( result != 0 )
	{
		DBG( 1, "pthread_create() failed with %d\n", result );
		sanei_thread_set_invalid(&thread);
	}
	else
	{
		DBG( 2, "pthread_create() created thread %ld\n",
		     sanei_thread_pid_to_long(thread) );
		thread.is_valid = SANE_TRUE;
	}

	return thread;
#else
	SANE_Pid pid = {SANE_FALSE, 0};
	pid.pid = fork();
	if( pid.pid < 0 )
	{
		DBG( 1, "fork() failed\n" );
		return pid;
	}

	pid.is_valid = SANE_TRUE;

	/*
	 * If I am the child....
	 *
	 */
	if( pid.pid == 0 ) {

    	/* run in child context... */
		int status = func( args );

		/* don't use exit() since that would run the atexit() handlers */
		_exit( status );
	}

	/* parents return */
	return pid;
#endif
}

int
sanei_thread_sendsig( SANE_Pid pid, int sig )
{
	DBG(2, "sanei_thread_sendsig() %d to thread (id=%ld)\n", sig,
	    sanei_thread_pid_to_long(pid));

	if (!pid.is_valid)
	{
		return -1;
	}
#ifdef USE_PTHREAD
	return pthread_kill( (pthread_t)pid.pid, sig );
#else
	return kill( pid.pid, sig );
#endif
}

SANE_Pid
sanei_thread_waitpid( SANE_Pid pid, int *status )
{
#ifdef USE_PTHREAD
	int *ls;
#else
	int ls;
#endif
	SANE_Pid result = pid;
	int stat;

	/*
	 * Can't join if the PID is invalid.
	 *
	 * Normally, we would assume that the below call would fail if
	 * the provided pid was invalid. However, we are now using a separate
	 * boolean flag in SANE_Pid so we must check this.
	 *
	 * We must assume that the caller is making rational assumptions when
	 * using SANE_Pid. You cannot assume that SANE_Pid is a pthread_t in
	 * particular.
	 *
	 */
	if (!pid.is_valid)
	{
		DBG(1, "sanei_thread_waitpid() - provided pid is invalid!\n");
		return pid;
	}

	stat = 0;

	DBG(2, "sanei_thread_waitpid() - %ld\n", sanei_thread_pid_to_long(pid));
#ifdef USE_PTHREAD
	int rc;
	rc = pthread_join( pid.pid, (void*)&ls );

	if( 0 == rc )
	{
		if( PTHREAD_CANCELED == ls )
		{
			DBG(2, "* thread has been canceled!\n" );
			stat = SANE_STATUS_GOOD;
		}
		else
		{
			stat = *ls;
		}
		DBG(2, "* result = %d (%p)\n", stat, (void*)status );
		result = pid;
	}
	if ( EDEADLK == rc )
	{
		if (!pthread_equal(pid.pid, pthread_self()))
		{
			/* call detach in any case to make sure that the thread resources
			 * will be freed, when the thread has terminated
			 */
			DBG(2, "* detaching thread(%ld)\n",
			    sanei_thread_pid_to_long(pid) );
			pthread_detach(pid.pid);
		}
	}
	if (status)
		*status = stat;

	restore_sigpipe();
#else
	result = waitpid( pid.pid, &ls, 0 );
	if((result < 0) && (errno == ECHILD)) {
		stat   = SANE_STATUS_GOOD;
		result = pid;
	} else {
		stat = eval_wp_result( pid, result, ls );
		DBG(2, "* result = %d (%p)\n", stat, (void*)status );
	}
	if( status )
		*status = stat;
#endif
	return result;
}

#endif /* HAVE_OS2_H */

SANE_Status
sanei_thread_get_status( SANE_Pid pid )
{
#if defined USE_PTHREAD || defined HAVE_OS2_H || defined __BEOS__
	_VAR_NOT_USED( pid );

	return td.status;
#else
	int ls, stat, result;

	stat = SANE_STATUS_IO_ERROR;
	if (pid.is_valid && (pid.pid > 0) )
	{
		result = waitpid( pid.pid, &ls, WNOHANG );

		stat = eval_wp_result( pid, result, ls );
	}
	return stat;
#endif
}

/*
 * Note: for the case of where the underlying system pid is neither
 * a pointer nor an integer, we should use an appropriate platform function
 * for comparing pids. We will have to take each case as it comes.
 *
 */
SANE_Bool
sanei_thread_pid_compare (SANE_Pid pid1, SANE_Pid pid2)
{
	if (!pid1.is_valid || !pid2.is_valid)
	{
		return SANE_FALSE;
	}

#if defined USE_PTHREAD
	return pthread_equal(pid1.pid, pid2.pid)? SANE_TRUE: SANE_FALSE;
#else
	return pid1.pid == pid2.pid;
#endif
}


/* END sanei_thread.c .......................................................*/
