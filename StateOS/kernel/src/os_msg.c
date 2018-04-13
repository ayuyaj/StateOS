/******************************************************************************

    @file    StateOS: os_msg.c
    @author  Rajmund Szymanski
    @date    13.04.2018
    @brief   This file provides set of functions for StateOS.

 ******************************************************************************

   Copyright (c) 2018 Rajmund Szymanski. All rights reserved.

   Permission is hereby granted, free of charge, to any person obtaining a copy
   of this software and associated documentation files (the "Software"), to
   deal in the Software without restriction, including without limitation the
   rights to use, copy, modify, merge, publish, distribute, sublicense, and/or
   sell copies of the Software, and to permit persons to whom the Software is
   furnished to do so, subject to the following conditions:

   The above copyright notice and this permission notice shall be included
   in all copies or substantial portions of the Software.

   THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS
   OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
   FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
   THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
   LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
   FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
   IN THE SOFTWARE.

 ******************************************************************************/

#include "inc/os_msg.h"
#include "inc/os_tsk.h"

/* -------------------------------------------------------------------------- */
void msg_init( msg_t *msg, unsigned limit, unsigned *data )
/* -------------------------------------------------------------------------- */
{
	assert(!port_isr_inside());
	assert(msg);
	assert(limit);
	assert(data);

	port_sys_lock();

	memset(msg, 0, sizeof(msg_t));

	msg->limit = limit;
	msg->data  = data;

	port_sys_unlock();
}

/* -------------------------------------------------------------------------- */
msg_t *msg_create( unsigned limit )
/* -------------------------------------------------------------------------- */
{
	msg_t *msg;

	assert(!port_isr_inside());
	assert(limit);

	port_sys_lock();

	msg = core_sys_alloc(ABOVE(sizeof(msg_t)) + limit * sizeof(unsigned));
	msg_init(msg, limit, (void *)((size_t)msg + ABOVE(sizeof(msg_t))));
	msg->res = msg;

	port_sys_unlock();

	return msg;
}

/* -------------------------------------------------------------------------- */
void msg_kill( msg_t *msg )
/* -------------------------------------------------------------------------- */
{
	assert(!port_isr_inside());
	assert(msg);

	port_sys_lock();

	msg->count = 0;
	msg->first = 0;
	msg->next  = 0;

	core_all_wakeup(msg, E_STOPPED);

	port_sys_unlock();
}

/* -------------------------------------------------------------------------- */
void msg_delete( msg_t *msg )
/* -------------------------------------------------------------------------- */
{
	port_sys_lock();

	msg_kill(msg);
	core_sys_free(msg->res);

	port_sys_unlock();
}

/* -------------------------------------------------------------------------- */
static
void priv_msg_get( msg_t *msg, unsigned *data )
/* -------------------------------------------------------------------------- */
{
	*data = msg->data[msg->first];

	msg->first = (msg->first + 1) % msg->limit;
	msg->count--;
}

/* -------------------------------------------------------------------------- */
static
void priv_msg_put( msg_t *msg, unsigned data )
/* -------------------------------------------------------------------------- */
{
	msg->data[msg->next] = data;

	msg->next = (msg->next + 1) % msg->limit;
	msg->count++;
}

/* -------------------------------------------------------------------------- */
unsigned msg_take( msg_t *msg, unsigned *data )
/* -------------------------------------------------------------------------- */
{
	tsk_t  * tsk;
	unsigned event = E_TIMEOUT;

	assert(msg);
	assert(data);

	port_sys_lock();

	if (msg->count > 0)
	{
		priv_msg_get(msg, data);
		tsk = core_one_wakeup(msg, E_SUCCESS);
		if (tsk) priv_msg_put(msg, tsk->tmp.msg);
		event = E_SUCCESS;
	}

	port_sys_unlock();

	return event;
}

/* -------------------------------------------------------------------------- */
static
unsigned priv_msg_wait( msg_t *msg, unsigned *data, cnt_t time, unsigned(*wait)(void*,cnt_t) )
/* -------------------------------------------------------------------------- */
{
	tsk_t  * tsk;
	unsigned event = E_SUCCESS;

	assert(!port_isr_inside());
	assert(msg);
	assert(data);

	port_sys_lock();

	if (msg->count > 0)
	{
		priv_msg_get(msg, data);
		tsk = core_one_wakeup(msg, E_SUCCESS);
		if (tsk) priv_msg_put(msg, tsk->tmp.msg);
	}
	else
	{
		System.cur->tmp.data = data;
		event = wait(msg, time);
	}

	port_sys_unlock();

	return event;
}

/* -------------------------------------------------------------------------- */
unsigned msg_waitUntil( msg_t *msg, unsigned *data, cnt_t time )
/* -------------------------------------------------------------------------- */
{
	return priv_msg_wait(msg, data, time, core_tsk_waitUntil);
}

/* -------------------------------------------------------------------------- */
unsigned msg_waitFor( msg_t *msg, unsigned *data, cnt_t delay )
/* -------------------------------------------------------------------------- */
{
	return priv_msg_wait(msg, data, delay, core_tsk_waitFor);
}

/* -------------------------------------------------------------------------- */
unsigned msg_give( msg_t *msg, unsigned data )
/* -------------------------------------------------------------------------- */
{
	tsk_t  * tsk;
	unsigned event = E_TIMEOUT;

	assert(msg);

	port_sys_lock();

	if (msg->count < msg->limit)
	{
		priv_msg_put(msg, data);
		tsk = core_one_wakeup(msg, E_SUCCESS);
		if (tsk) priv_msg_get(msg, tsk->tmp.data);
		event = E_SUCCESS;
	}

	port_sys_unlock();

	return event;
}

/* -------------------------------------------------------------------------- */
static
unsigned priv_msg_send( msg_t *msg, unsigned data, cnt_t time, unsigned(*wait)(void*,cnt_t) )
/* -------------------------------------------------------------------------- */
{
	tsk_t  * tsk;
	unsigned event = E_SUCCESS;

	assert(!port_isr_inside());
	assert(msg);

	port_sys_lock();

	if (msg->count < msg->limit)
	{
		priv_msg_put(msg, data);
		tsk = core_one_wakeup(msg, E_SUCCESS);
		if (tsk) priv_msg_get(msg, tsk->tmp.data);
	}
	else
	{
		System.cur->tmp.msg = data;
		event = wait(msg, time);
	}

	port_sys_unlock();

	return event;
}

/* -------------------------------------------------------------------------- */
unsigned msg_sendUntil( msg_t *msg, unsigned data, cnt_t time )
/* -------------------------------------------------------------------------- */
{
	return priv_msg_send(msg, data, time, core_tsk_waitUntil);
}

/* -------------------------------------------------------------------------- */
unsigned msg_sendFor( msg_t *msg, unsigned data, cnt_t delay )
/* -------------------------------------------------------------------------- */
{
	return priv_msg_send(msg, data, delay, core_tsk_waitFor);
}

/* -------------------------------------------------------------------------- */
void msg_push( msg_t *msg, unsigned data )
/* -------------------------------------------------------------------------- */
{
	tsk_t *tsk;

	assert(msg);

	port_sys_lock();

	priv_msg_put(msg, data);

	if (msg->count > msg->limit)
	{
		msg->count = msg->limit;
		msg->first = msg->next;
	}
	else
	{
		tsk = core_one_wakeup(msg, E_SUCCESS);
		if (tsk) priv_msg_get(msg, tsk->tmp.data);
	}

	port_sys_unlock();
}

/* -------------------------------------------------------------------------- */
