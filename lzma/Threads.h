/*
 * This file is a part of Pcompress, a chunked parallel multi-
 * algorithm lossless compression and decompression program.
 *
 * Copyright (C) 2012-2013 Moinak Ghosh. All rights reserved.
 * Use is subject to license terms.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 3 of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this program.
 * If not, see <http://www.gnu.org/licenses/>.
 *
 * moinakg@belenix.org, http://moinakg.wordpress.com/
 *      
 */

/* Threads.h -- multithreading library
2008-11-22 : Igor Pavlov : Public domain */

#ifndef __7Z_THRESDS_H
#define __7Z_THRESDS_H

#include "Types.h"
#include "windows.h"

#ifdef ENV_BEOS
#include <kernel/OS.h>
#define MAX_THREAD 256
#else
#include <pthread.h>
#endif

/* #define DEBUG_SYNCHRO 1 */

typedef struct _CThread
{
#ifdef ENV_BEOS
	thread_id _tid;
#else
	pthread_t _tid;
#endif
	int _created;

} CThread;

#define Thread_Construct(thread) (thread)->_created = 0
#define Thread_WasCreated(thread) ((thread)->_created != 0)

typedef unsigned THREAD_FUNC_RET_TYPE;
#define THREAD_FUNC_CALL_TYPE MY_STD_CALL
#define THREAD_FUNC_DECL THREAD_FUNC_RET_TYPE THREAD_FUNC_CALL_TYPE

typedef THREAD_FUNC_RET_TYPE (THREAD_FUNC_CALL_TYPE * THREAD_FUNC_TYPE)(void *);

WRes Thread_Create(CThread *thread, THREAD_FUNC_TYPE startAddress, LPVOID parameter);
WRes Thread_Wait(CThread *thread);
WRes Thread_Close(CThread *thread);

typedef struct _CEvent
{
  int _created;
  int _manual_reset;
  int _state;
#ifdef ENV_BEOS
  thread_id _waiting[MAX_THREAD];
  int       _index_waiting;
  sem_id    _sem;
#else
  pthread_mutex_t _mutex;
  pthread_cond_t  _cond;
#endif
} CEvent;

typedef CEvent CAutoResetEvent;
typedef CEvent CManualResetEvent;

#define Event_Construct(event) (event)->_created = 0
#define Event_IsCreated(event) ((event)->_created)

WRes ManualResetEvent_Create(CManualResetEvent *event, int initialSignaled);
WRes ManualResetEvent_CreateNotSignaled(CManualResetEvent *event);
WRes AutoResetEvent_Create(CAutoResetEvent *event, int initialSignaled);
WRes AutoResetEvent_CreateNotSignaled(CAutoResetEvent *event);
WRes Event_Set(CEvent *event);
WRes Event_Reset(CEvent *event);
WRes Event_Wait(CEvent *event);
WRes Event_Close(CEvent *event);


typedef struct _CSemaphore
{
  int _created;
  UInt32 _count;
  UInt32 _maxCount;
#ifdef ENV_BEOS
  thread_id _waiting[MAX_THREAD];
  int       _index_waiting;
  sem_id    _sem;
#else
  pthread_mutex_t _mutex;
  pthread_cond_t  _cond;
#endif
} CSemaphore;

#define Semaphore_Construct(p) (p)->_created = 0

WRes Semaphore_Create(CSemaphore *p, UInt32 initiallyCount, UInt32 maxCount);
WRes Semaphore_ReleaseN(CSemaphore *p, UInt32 num);
#define Semaphore_Release1(p) Semaphore_ReleaseN(p, 1)
WRes Semaphore_Wait(CSemaphore *p);
WRes Semaphore_Close(CSemaphore *p);

typedef struct {
#ifdef ENV_BEOS
	sem_id _sem;
#else
        pthread_mutex_t _mutex;
#endif
} CCriticalSection;

WRes CriticalSection_Init(CCriticalSection *p);
#ifdef ENV_BEOS
#define CriticalSection_Delete(p) delete_sem((p)->_sem)
#define CriticalSection_Enter(p)  acquire_sem((p)->_sem)
#define CriticalSection_Leave(p)  release_sem((p)->_sem)
#else
#ifdef DEBUG_SYNCHRO
void CriticalSection_Delete(CCriticalSection *);
void CriticalSection_Enter(CCriticalSection *);
void CriticalSection_Leave(CCriticalSection *);
#else
#define CriticalSection_Delete(p) pthread_mutex_destroy(&((p)->_mutex))
#define CriticalSection_Enter(p)  pthread_mutex_lock(&((p)->_mutex))
#define CriticalSection_Leave(p)  pthread_mutex_unlock(&((p)->_mutex))
#endif
#endif

#endif

