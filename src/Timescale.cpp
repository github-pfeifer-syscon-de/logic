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

#include <iostream>
#include <math.h>

#include "Timescale.hpp"
#include "LogicDrawing.hpp"

Timescale::Timescale() 
: ft(0.0)
, f(1.0)
, unit(1.0)
{
}


Timescale::~Timescale() 
{
}

double 
Timescale::timerValue2time(int64_t timerValue, Capture *capture) 
{
	return (double)timerValue / (double)capture->getTimerRate();
}

void
Timescale::prepare(Capture *capture)
{
    int64_t tickS = capture->getTime(0);
    int64_t tick0 = capture->getTime(capture->getStart());
    int64_t tickE = capture->getTime(Capture::BUF_LEN-1);
    //std::cout << "idxS " << 0 << " idx0 " << capture->getStart() << " idxE " << Capture::BUF_LEN-1 << std::endl;
    //std::cout << "tickS " << tickS << " tick0 " << tick0 << " tickE " << tickE << std::endl;
    int64_t tickDiff = (tickE - tickS);
    double timeD = timerValue2time(tickDiff, capture);
	f = (double)(Capture::BUF_LEN-1) / tickDiff;
	ft = (double)(Capture::BUF_LEN-1) / timeD;
	//std::cout << "total " << timeD << " f " << f << " ft " << ft << std::endl;
    unit = timeD < 0.000001 
					? 1.e9
					: timeD < 0.001 
						? 1.e6
						: timeD < 1.0 
							? 1.e3
							: 1.0;
	//std::cout << "unit " << unit << std::endl;
	distTimeZero(capture);							
}

void
Timescale::draw(Cairo::RefPtr<Cairo::Context> cr, Capture *capture, double y0, double ye)
{
	prepare(capture);
	//cr->set_source_rgb(0.0, 0.0, 0.0);
	//cr->fill();
	
	int64_t tickS = capture->getTime(0);
    int64_t tick0 = capture->getTime(capture->getStart());
    int64_t tickE = capture->getTime(Capture::BUF_LEN-1);
    //std::cout << "idxS " << 0 << " idx0 " << capture->getStart() << " idxE " << Capture::BUF_LEN-1 << std::endl;
    //std::cout << "tickS " << tickS << " tick0 " << tick0 << " tickE " << tickE << std::endl;
    int64_t tickDiff = (tickE - tickS);
    double timeD = timerValue2time(tickDiff, capture);

	double dim = log10(timeD);
	//std::cout << "dim " << dim << std::endl;
	dim = floor(dim - 0.5);
	//std::cout << "floor(dim) " << dim << std::endl;
	m_step = pow(10, dim);
	//std::cout << "step " << step << std::endl;
	m_timeDS = timerValue2time(tick0 - tickS, capture);
	m_timeDE = timerValue2time(tickE - tick0, capture);
	//std::cout << "timeDS " << timeDS << std::endl;
	marks(cr, y0, ye, true);
}

void 
Timescale::marks(Cairo::RefPtr<Cairo::Context> cr, double y0, double ye, bool lbl) 
{
    cr->set_line_width(1.0);    
    cr->set_font_size(10.0);
	if (m_step > 0.0) {
		for (double t = 0.0; t >= -m_timeDS; t -= m_step) {
			//std::cout << "loop -t " << t << std::endl;
			timeMark(t, t+m_timeDS, cr, y0, ye, lbl);
		}
		//std::cout << "timeDE " << timeDE << std::endl;
		for (double t = m_step; t <= m_timeDE; t += m_step) {
			//std::cout << "loop +t " << t << std::endl;
			timeMark(t, t+m_timeDS, cr, y0, ye, lbl);
		}
	}
}

std::string
Timescale::format(double t)
{
    std::ostringstream oss;
	oss.precision(1);
    double value = t * unit;
    oss << std::fixed << value;    
	if (unit >= 1.e9)
		oss << 'n';
		else if (unit >= 1.e6)
			oss << 'u';			// sorry no unicode µ
			else if (unit >= 1.e3)
				oss << 'm';
	oss << 's';			
	std::string ret = oss.str();
	//printf("t %8.6lf ret %s\n", t, ret.c_str());
    return ret;
}

std::string 
Timescale::freeFormat(double t, const char *suffix) 
{
    std::ostringstream oss;
	oss.precision(1);
    oss << std::fixed;    
    char unit = ' ';
	if (t >= 10.e9) {		
		unit = 'G';
		t /= 1.e9;
	}
	else if (t >= 10.e6) {
		unit = 'M';
		t /= 1.e6;		
	}
	else if (t >= 10.e3) {
		unit = 'k';
		t /= 1.e3;
	}
	else if (t == 0.0) {
		// just keep this
	}
	else if (t < 10.e-6) {
		unit = 'n';
		t *= 1.e9;
	}
	else if (t < 10.e-3) {
		unit = 'u';		// use unicode µ ?
		t *= 1.e6;
	}
	else if (t < 10.0) {
		unit = 'm';
		t *= 1.e3;
	}
	oss << t << unit;			
	if (suffix != NULL) {
		oss << suffix;
	}	
	std::string ret = oss.str();
    return ret;
	
}

// as we might use a timer that has not the resolution to advance for each capture
//     distribute from neigbours e.g.:
//     input  0   1   0   0   1
//     output 0.5 0.5 0.3 0.3 0.3
// also fills dTim so it can be used to find a drawing position for a idx
void
Timescale::distTimeZero(Capture *capture)	
{
	double diff[Capture::BUF_LEN];
	diff[0] = 0.0;
	for (int i = 1; i < Capture::BUF_LEN; ++i) {
		diff[i] = capture->getTime(i) - capture->getTime(i-1);		
	}	
    for (int i = 1; i < Capture::BUF_LEN; ++i) {		
		if (diff[i] == 0.0) {
			for (int j = i+1; j < Capture::BUF_LEN; ++j) {
				if (diff[j] > 0.0) {
					double part = diff[j] / (double)(j - i + 1);
					for (int k = i; k <= j; ++k) {
						diff[k] = part;
					}
					i = j+1;
					break;
				}
			}
		}
	}
	double dSum = 0.0;
    for (int i = 0; i < Capture::BUF_LEN; ++i) {
		dSum += diff[i];
		dTim[i] = dSum * f;
	}
}

void
Timescale::timeMark(double t, double d, Cairo::RefPtr<Cairo::Context> cr, double y0, double ye, bool lbl)
{
	//std::cout << "t " << t << std::endl;
	double dt = d * ft;
	//std::cout << "d " << d << " dt " << dt << std::endl;
	double x = dt + 0.5;	// Display is linear
	
	cr->set_source_rgb(0.3, 0.3, 0.3);
	cr->move_to(x, y0);
	cr->line_to(x, ye);
	cr->stroke();       

	if (lbl) {
		cr->set_source_rgb(0.7, 0.7, 0.7);
		cr->move_to(x, ye / 2.0);
		//double t = (double)(m_capture->getTime(l) - time0) / (double)m_capture->getTimerRate();        
		cr->show_text(format(t));
	}
}

double 
Timescale::idx2x(uint32_t idx) 
{
	return dTim[idx];
}

