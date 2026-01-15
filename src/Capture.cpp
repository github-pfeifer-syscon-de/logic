/*
 * Copyright (C) 2019 rpf
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */
#include <time.h>
#include <iostream>
 
#include "Capture.hpp"
 
Capture::Capture() 
: m_preTrigger(BUF_LEN/4)
, m_triggerRiseEdge(0)
, m_triggerFallEdge(0)
, m_mask(0)
, m_time(0)
, m_edge_enabled(false)
{
}

Capture::~Capture() 
{ 
}

void
Capture::setTrigger(int32_t triggerRiseEdge, int32_t triggerFallEdge) 
{
	m_triggerRiseEdge = triggerRiseEdge;
	m_triggerFallEdge = triggerFallEdge;
	m_mask = triggerRiseEdge | triggerFallEdge;
}
    
void
Capture::setTime(int32_t time) 
{
	//std::cout << "Capture::setTime " << time << std::endl;
	m_time = time;
}

void 
Capture::setEdgeEnabled(bool edgeEnabeld)
{
	m_edge_enabled = edgeEnabeld;
}

uint64_t
Capture::getTime() 
{
	return m_time;
}

int32_t 
Capture::getStart() 
{
	return m_ext_start;
}

void
Capture::setPreTrigger(uint32_t preTrigger) 
{
	m_preTrigger = preTrigger;
}

uint32_t Capture::getPreTrigger() 
{
	return m_preTrigger;
}

int64_t 
Capture::getTime(uint32_t idx)
{
	return m_times[idx];
}

uint32_t 
Capture::getValue(uint32_t idx)
{
	return m_value[idx];
}

uint32_t 
Capture::getEdge(uint32_t idx) 
{
	return m_edge[idx];
}

void
Capture::delay(unsigned long long delay)
{
    struct timespec tm;
    tm.tv_nsec = delay;
    tm.tv_sec = 0;
    nanosleep(&tm, &tm);
}

void
Capture::setInactive()
{
	m_active = false;
}
