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

#include "Capture.hpp"

class UserCapture : public Capture
{
public:
    UserCapture();
    virtual ~UserCapture();
    
    bool start(Glib::Dispatcher &notification) override;
    long long getTimerRate() override;
	bool getInit() override;
    
   	static const long long TIMER_RATE_GHz = 1000000000ll;	// 1GHz 
   	static const long long TIMER_RATE_MHz = 1000000ll;		// 1MHz 
   	
protected:	
	bool run(Glib::Dispatcher &notification);

	void setupPmw();
	bool setupSpi();
	bool sendSpi(uint8_t send_data);

private:
	long long m_timer_rate;
	int m_init;
	void convertToOutput(int32_t start, int32_t *times, uint32_t *value, uint32_t *edge);

};

