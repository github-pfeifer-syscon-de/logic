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
#include <string>

#include "Capture.hpp"

class Timescale {
public:
    Timescale();
    virtual ~Timescale();
    
    void draw(Cairo::RefPtr<Cairo::Context> cr, Capture *capture, double y0, double ye);
    void prepare(Capture *capture);
	double idx2x(uint32_t idx);
    std::string format(double t);
    std::string freeFormat(double t, const char *suffix);
	double ft;							// factor to scale time to display
	void marks(Cairo::RefPtr<Cairo::Context> cr, double y0, double ye, bool lbl); 

protected:
    void timeMark(double t, double d, Cairo::RefPtr<Cairo::Context> cr, double y0, double ye, bool lbl);
	void distTimeZero(Capture *capture);
	double timerValue2time(int64_t timValue, Capture *capture);
	
private:
    double dTim[Capture::BUF_LEN];		// relative time scaled to display 
	double f;							// factor to scale timer-value to display
	double unit;						// dispay scale 1e9 -> n, 1e6 -> u, 1e3 -> m

	double m_step;
	double m_timeDS;
	double m_timeDE; 

};
