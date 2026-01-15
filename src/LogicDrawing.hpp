/*
 * Copyright (C) 2018 rpf
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
#include <thread>


#include "Capture.hpp"
#include "Timescale.hpp"


class LogicDrawing 
{
public:
    LogicDrawing(Gtk::Window *parent, Gtk::ToggleButton &single);
    virtual ~LogicDrawing();

    void continious(bool active);
    bool single();
    void on_notification_from_worker_thread();
    void on_intermediate_from_worker_thread();
    double bit2pos(int bit);
    void frame(Cairo::RefPtr<Cairo::Context> cr);
    void setTime(Glib::ustring &time);
    void setTrigger(uint32_t triggerRiseEdge, uint32_t triggerFallEdge);
    void setLevel(Glib::ustring &level);
	void setEdgeEnabled(bool edgeEnabled);
	void threadTerminate();
	void resetSelection();
	bool getInit();
	void on_action_save();
	Timescale *getTimescale();

 	static const unsigned int BITS = 32;		// bits to display / capture is always 32
    static const unsigned int outp_height = 20;
    static const unsigned int outp_gap = 6;
	static const unsigned int FRONT = 16;	// front space for bit numbers
	
protected:
	Capture *m_capture;
	Timescale m_timescale;
	
	void save_png(std::string filename);
	std::thread *m_thread;
	void setMessage(const char *msg);
	void draw(Cairo::RefPtr<Cairo::Context> cr, double x0, double y0, uint32_t bit);
	void renice();
	double bool2y(bool now); 
 
private:

    Gtk::Window *m_parent;
    Gtk::ToggleButton &m_single;
    sigc::connection m_timer;  			// Timer for animation
    Glib::Dispatcher    m_Dispatcher;   // used for redraw notification
    Glib::Dispatcher    m_Notification; // used for redraw notification
    bool m_level;
};

