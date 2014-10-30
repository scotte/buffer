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

/* Allocate new semaphores */
int new_sems();

/* Perform actions on semaphores */
void sem_set();
void lock();
void unlock();
void remove_sems();
