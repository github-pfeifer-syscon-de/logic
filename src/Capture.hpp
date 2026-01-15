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

#pragma once

#include <gtkmm.h>
#include <stdint.h>

#ifndef max
#define max(a,b) \
   ({ __typeof__ (a) _a = (a); \
       __typeof__ (b) _b = (b); \
     _a > _b ? _a : _b; })
#endif

#ifndef min
#define min(a,b) \
   ({ __typeof__ (a) _a = (a); \
       __typeof__ (b) _b = (b); \
     _a <= _b ? _a : _b; })
#endif


class Capture 
{
public:
    Capture();
    virtual ~Capture();
    
    void setTrigger(int32_t triggerRiseEdge, int32_t triggerFallEdge);
    void setTime(int32_t time);
    /*
	 * as edge sense is a function that is build to allow interrupts
	 * disable it if we cound not find the right setup.
	 */
    void setEdgeEnabled(bool edgeEnabeld);
    uint64_t getTime();
    void setPreTrigger(uint32_t preTrigger);
    uint32_t getPreTrigger();
    
    int64_t getTime(uint32_t idx);
    uint32_t getValue(uint32_t idx);
    uint32_t getEdge(uint32_t idx);
    int32_t getStart();
	virtual bool start(Glib::Dispatcher &notification) = 0;   
	virtual long long getTimerRate() = 0;
	virtual bool getInit() = 0;
	void setInactive();
	
    static const unsigned int BUF_LEN = 1024;
protected:
	uint32_t m_mask;
	uint32_t m_triggerRiseEdge;
	uint32_t m_triggerFallEdge;
	int32_t m_time;
	uint32_t m_preTrigger;
	int32_t m_ext_start;		// external start
	bool m_active;

	int64_t m_times[BUF_LEN];
    uint32_t m_value[BUF_LEN];
    uint32_t m_edge[BUF_LEN];
    bool m_edge_enabled;
	void delay(unsigned long long delay);

private:
		
};
