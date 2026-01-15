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

#include <iostream>
#include <sstream>
#include <sys/ioctl.h>
#include <sys/types.h> 
#include <sys/stat.h> 
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <signal.h>
#include <fcntl.h> 
#include <sys/io.h>
#include <pthread.h>
#include <errno.h>
#include <sched.h>
#include <cstring>
#include <sys/time.h>
#include <sys/resource.h>

// Localisation https://developer.gnome.org/glib/stable/glib-I18N.html 
#include <glib.h>
#include <glib/gi18n.h>

#include "LogicDrawing.hpp"
#include "DeviceCapture.hpp"
#include "UserCapture.hpp"
#include "LogicWindow.hpp"

LogicDrawing::LogicDrawing(Gtk::Window *parent, Gtk::ToggleButton &single)
: m_Dispatcher()
, m_Notification()
//, m_capture(new DeviceCapture())
, m_capture(new UserCapture())
, m_timescale()
, m_parent(parent)
, m_single(single)
, m_thread(NULL)
{
    // done after attach as we need size
    //create_pixmap();
    m_Dispatcher.connect(sigc::mem_fun(*this, &LogicDrawing::on_notification_from_worker_thread));
    m_Notification.connect(sigc::mem_fun(*this, &LogicDrawing::on_intermediate_from_worker_thread));
    
}


LogicDrawing::~LogicDrawing()
{
	threadTerminate();
	if (m_capture) {
		delete m_capture;
		m_capture = NULL;
	}

}

bool 
LogicDrawing::getInit() 
{
	return m_capture->getInit();
}


void
LogicDrawing::on_action_save()
{
    Gtk::FileChooserDialog dialog(_("Save file"), Gtk::FILE_CHOOSER_ACTION_SAVE);
    dialog.set_transient_for(*m_parent);

    //Add response buttons the the dialog:
    dialog.add_button(_("_Cancel"), Gtk::RESPONSE_CANCEL);
    dialog.add_button(_("_Save"), Gtk::RESPONSE_OK);

    //Add filters, so that only certain file types can be selected:

    auto filter_image_png = Gtk::FileFilter::create();
    filter_image_png->set_name(_("Png files"));
    filter_image_png->add_mime_type("image/png");
    dialog.add_filter(filter_image_png);
    int result = dialog.run();
    switch(result)
    {
        case(Gtk::RESPONSE_OK):
            save_png(dialog.get_filename());
            break;
    }
}

void
LogicDrawing::save_png(std::string filename)
{
    std::cout << "on save " << filename << std::endl;
    Cairo::RefPtr<Cairo::ImageSurface> outpixmap = Cairo::ImageSurface::create(Cairo::FORMAT_ARGB32,
                                FRONT + Capture::BUF_LEN,
                                (outp_height + outp_gap) * BITS + outp_height);

    {
        Cairo::RefPtr<Cairo::Context> cr = Cairo::Context::create(outpixmap);
		cr->set_source_rgb(0.0, 0.0, 0.0);
		cr->rectangle(0, 0, outpixmap->get_width(), outpixmap->get_height());
		cr->paint();
		
      	frame(cr);
		m_timescale.marks(cr, 0.5, outp_height-0.5, true);	// does also prepare		
		for (uint32_t bit = 0; bit < BITS; ++bit) {
			double y = bit2pos(bit);
			draw(cr, FRONT, y, bit);
		}        
    }
    outpixmap->write_to_png(filename);
}

Timescale *
LogicDrawing::getTimescale()
{
	return &m_timescale;
}

void 
LogicDrawing::setMessage(const char *msg)
{
	LogicWindow *logicWnd = (LogicWindow *)m_parent;
	Glib::ustring umsg(msg);
	logicWnd->setMessage(umsg);
}

void 
LogicDrawing::setEdgeEnabled(bool edgeEnabled)
{
	m_capture->setEdgeEnabled(edgeEnabled);
}

double
LogicDrawing::bit2pos(int bit)
{
    return bit * (outp_height + outp_gap) + 10 + outp_gap + 0.5;
}

void
LogicDrawing::frame(Cairo::RefPtr<Cairo::Context> cr)
{
     //guint stride = pixmap->get_stride() / sizeof(guint32);  // related to image format ARGB32 index byte to guint32 as we use a pointer of that type
    //guint32 *img = (guint32 *)pixmap->get_data();
    //if (img == NULL)
    //        break;
	cr->set_font_size(10);
	/* Erase pixmap */
	//cr->set_source_rgb(0.0, 0.0, 0.0);
	//cr->rectangle(0, 0, pixmap->get_width(), pixmap->get_height());
	//cr->paint();
	cr->set_source_rgb(0.3, 0.3, 0.3);
	cr->set_line_width(1);
	for (uint32_t bit = 0; bit < BITS; ++bit) {
		double y = bit2pos(bit);
		cr->set_source_rgb(0.3, 0.3, 0.3);
		cr->rectangle(0.5+FRONT, y, Capture::BUF_LEN+FRONT, outp_height);
		cr->stroke();

		std::ostringstream oss;
		oss << bit; 	//"0x" << std::hex << (1 << bit);
		cr->set_source_rgb(0.7, 0.7, 0.7);
		cr->move_to(3, y + outp_height);
		cr->show_text(oss.str());
	}
}


void 
LogicDrawing::on_intermediate_from_worker_thread() 
{
	setMessage(_("triggered"));
    //queue_draw();
}

void
LogicDrawing::on_notification_from_worker_thread()
{
	setMessage("");
	m_single.set_active(false);
	int w = Capture::BUF_LEN;
	int h = outp_height;
    //frame();
	//double y0 = bit2pos(0);
	//double ye = bit2pos(BITS) - outp_gap;
	LogicWindow *logicWnd = (LogicWindow *)m_parent;
	Cairo::RefPtr<Cairo::ImageSurface> timesurface = Cairo::ImageSurface::create(Cairo::FORMAT_ARGB32, w, h);
	{
		Cairo::RefPtr<Cairo::Context> crTime = Cairo::Context::create(timesurface);
		m_timescale.draw(crTime, m_capture, 0.5, h-1.0);	// does also prepare
	}
	//timesurface->flush();
	//timesurface->write_to_png("/home/rpf/scale.png");
	auto image = Gtk::manage(new Gtk::Image());
	image->set(timesurface);
	logicWnd->setTimescale(*image);
    for (uint32_t bit = 0; bit < BITS; ++bit) {
        Cairo::RefPtr<Cairo::ImageSurface> pixmap = Cairo::ImageSurface::create(Cairo::FORMAT_ARGB32, w, h);
		{
			Cairo::RefPtr<Cairo::Context> cr = Cairo::Context::create(pixmap);
			draw(cr, 0.0, 0.0, bit);
		}
		Glib::RefPtr<Gdk::Pixbuf> image = Gdk::Pixbuf::create(pixmap, 0,0, w,h);
		logicWnd->setPixbuf(bit, image);		
    }
    //m_parent->queue_draw();
}

void
LogicDrawing::draw(Cairo::RefPtr<Cairo::Context> cr, double x0, double y0, uint32_t bit)
{
	// this does not reveal colors ...
 	//Glib::RefPtr<Gtk::Settings> settings = Gtk::Settings::get_default();
	//Glib::PropertyProxy<Glib::ustring> sttings->property_gtk_color_scheme();
	//const Gtk::Allocation allocation = get_allocation();
 
	auto refStyleContext = m_single.get_style_context();	// missuse button as source for colors
  	// background is keept transparent
  	//refStyleContext->render_background(cr, allocation.get_x(), allocation.get_y(), allocation.get_width(), allocation.get_height());
  	const auto state = refStyleContext->get_state();
  	Gdk::Cairo::set_source_rgba(cr, refStyleContext->get_color(state));
	
	cr->set_line_width(1.0);
	m_timescale.marks(cr, y0+0.5, y0+outp_height-1.0, false);
	bool last = false;
	//cr->set_source_rgb(1.0, 1.0, 1.0);	// to adjust to l&f use style context see above
	
    double y = y0 + 0.5;
    uint32_t mask = 1 << bit;
    cr->move_to(x0 + 0.5, y);
    for (int idx = 0; idx < Capture::BUF_LEN; ++idx) {
        bool edgs = (m_capture->getEdge(idx) & mask);
		double tx = x0 + m_timescale.idx2x(idx) + 0.5;
        if (edgs) {
			double ty = y + bool2y(!last);	// if we captured a edge show changed value
	        cr->line_to(tx, ty);
		}
		bool now = (m_capture->getValue(idx) & mask);
        double ty = y + bool2y(now);
        cr->line_to(tx, ty);		// and always represent actual value
        last = now;
    }
    cr->stroke();
}

double 
LogicDrawing::bool2y(bool now) 
{
	return (now ? 0.0 : outp_height - 1.0);
}

void
LogicDrawing::continious(bool active)
{
    if (active) {
       sigc::slot<bool> slot = sigc::mem_fun(*this, &LogicDrawing::single);
       m_timer = Glib::signal_timeout().connect(slot, 1000);
    }
    else {
		if (m_timer.connected()) {
			m_timer.disconnect();
		}
		//threadTerminate();
		//setMessage("");
    }
}

void
LogicDrawing::setTime(Glib::ustring &time)
{
    int32_t itime = atoi(time.c_str());
    //std::cout << "LogicDrawing::setTime " << itime << std::endl;
    m_capture->setTime(itime);
}

void
LogicDrawing::setTrigger(uint32_t triggerRiseEdge, uint32_t triggerFallEdge)
{
    m_capture->setTrigger(triggerRiseEdge, triggerFallEdge);
}

void
LogicDrawing::setLevel(Glib::ustring &level)
{
    m_level = level=="h" ? true : false;
}

bool
LogicDrawing::single()
{
    threadTerminate();	// terminate a "old" thread (if exists)
    setMessage(_("wait"));
    //queue_draw();
	m_thread = new std::thread([this] {
        try {
			//renice();		// see comment below 
            if (m_capture->start(m_Notification)) {
				m_Dispatcher.emit();
			}
			
			m_thread = NULL;
        }
        catch (std::exception &e) {
            std::cout << "Exception on worker "
                      << " " << e.what()
                      << std::endl;
        }
    });
    if (geteuid() == 0) {
		sched_param sch_params;
		memset(&sch_params, 0, sizeof(sch_params));	// in case the structure will be extended 
		int policy = SCHED_OTHER;	// SCHED_RR might even be more greedy
		sch_params.sched_priority = sched_get_priority_max(policy);	// need for speed ?
		//std::cout << "Prio " <<   sch_params.sched_priority << std::endl;
		if(pthread_setschedparam(m_thread->native_handle(), policy,  &sch_params)) {
			std::cerr << "Failed to set Thread scheduling : " << std::strerror(errno) << std::endl;
		}
	}
    return true;
}

void 
LogicDrawing::renice() 
{	// wound like to increace priority, by nice 
	//   (and here we woud need the actual minimum, but only have max), 
	//   but usually it is not allowed for user processes to be "unnice"  
	struct rlimit rlim;
	getrlimit(RLIMIT_NICE, &rlim);
	std::cout << "nice soft " << 20 - rlim.rlim_cur
	          << " hard " << 20 - rlim.rlim_max
			  << std::endl;
	int nic = nice(0);
	std::cout << "nice " << nic << std::endl;
}

void 
LogicDrawing::threadTerminate() 
{
	std::thread *thread = m_thread;
	if (thread != NULL) {
		m_capture->setInactive();
		if (thread->joinable()) {
			thread->join();
		}
	}
}
