/* -*- Mode: C; indent-tabs-mode: t; c-basic-offset: 4; tab-width: 4 -*-  */
/*
 * displayTree.cpp
 *
 * Logic is free software: you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 * 
 * Logic is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details.
 * 
 * You should have received a copy of the GNU General Public License along
 * with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include <iostream>
#include <sstream>
// Localisation https://developer.gnome.org/glib/stable/glib-I18N.html 
#include <glib.h>
#include <glib/gi18n.h>

#include "DisplayTree.hpp"



DisplayTree::DisplayTree(Glib::RefPtr<Gtk::ListStore> listStore, Timescale *timescale, ModelColumns &columns)
: Gtk::TreeView(listStore)
, m_timescale(timescale)
, mouseX(-1)
, markX(-1)
, mouseY(-1)
, markY(-1)
{

	// By default the motion event is enabled need to cater for that in event handler
	//add_events(Gdk::EventMask::BUTTON_PRESS_MASK
    //        | Gdk::EventMask::BUTTON_RELEASE_MASK
    //        | Gdk::EventMask::BUTTON_MOTION_MASK);
	
	append_column(_("Bit"), columns.m_col_bit);
	createToggleColumn(columns.m_col_trigger_riseedge, "/de/pfeifer_syscon/logic/riseedge.png");
	createToggleColumn(columns.m_col_trigger_falledge, "/de/pfeifer_syscon/logic/falledge.png");
		
	auto cellRenderer = Gtk::manage(new Gtk::CellRendererPixbuf());
	//m_cellRenderer->set_fixed_size(Capture::BUF_LEN, LogicDrawing::outp_height);
	//m_cellRenderer->property_stock_size() = Capture::BUF_LEN; 
	cellRenderer->set_alignment(0.0, 0.5); // as we want to align the row content
	data_column_num = append_column(_("Data"), *cellRenderer);
	--data_column_num;
   	auto pColumn = get_column(data_column_num);
	if (pColumn) {
		pColumn->add_attribute(cellRenderer->property_pixbuf(), columns.m_col_data);
	}  
	
}

DisplayTree::~DisplayTree()
{
}

void 
DisplayTree::createToggleColumn(Gtk::TreeModelColumn<bool> &col, const char *pixname)
{
	auto cellTrigger = Gtk::manage(new Gtk::CellRendererToggle());
	cellTrigger->set_radio(false);		// as we want checkboxes
	cellTrigger->set_activatable(true);	// may not be needed
	cellTrigger->signal_toggled().connect(
	                   sigc::bind<Gtk::TreeModelColumn<bool> &>(
	                         sigc::mem_fun(*this, &DisplayTree::on_trigger_toggled), col));           
	int cols_count = append_column("", *cellTrigger);  
	auto pColumnTrigger = get_column(cols_count - 1);
	if (pColumnTrigger) {
		pColumnTrigger->add_attribute(cellTrigger->property_active(), col);
		Glib::RefPtr<Gdk::Pixbuf> pix = Gdk::Pixbuf::create_from_resource(pixname);
		auto image = Gtk::manage(new Gtk::Image());
		image->set(pix);
		pColumnTrigger->set_widget(*image);
		image->show();		// required for the image to show up 
		pColumnTrigger->set_alignment(0.5);	// make table header move with content
	}
	
}

void 
DisplayTree::on_trigger_toggled(const Glib::ustring& path, Gtk::TreeModelColumn<bool> &col)
{
	auto iter = get_model()->get_iter(path);
	if (iter) {
		Gtk::TreeModel::Row row = *iter;
		row[col] = !row[col];		
	}
}


bool
DisplayTree::on_motion_notify_event(GdkEventMotion* event)
{
    // Just a test.
    //std::cout 
    //   << "m " << event->x 
    //   << " " << event->y 
    //   << std::endl;
	if (((event->state & (GDK_BUTTON1_MASK | GDK_BUTTON2_MASK | GDK_BUTTON3_MASK)) == 0)
	 || m_timescale->ft == 0.0) {
		return Gtk::TreeView::on_motion_notify_event(event);	// Keep selectable / clickable
	}
	double x = event->x;
	double y = event->y + getYOffset();
	
    Gdk::Rectangle *selectedRect = new Gdk::Rectangle();
    Gdk::Rectangle *textOldRect = new Gdk::Rectangle();
    Gdk::Rectangle *textRect = new Gdk::Rectangle();
    int xmin = min(markX, min(mouseX, x));
    int ymin = min(markY, min(mouseY, y));
    int xmax = max(markX, max(mouseX, x));
    int ymax = max(markY, max(mouseY, y));
	selectedRect->set_x(xmin - 1);
	selectedRect->set_y(ymin - 1);
	selectedRect->set_width(xmax - xmin + 2);
	selectedRect->set_height(ymax - ymin + 2);
	
	textRect->set_x(x - 1);
	textRect->set_y(y - 11);
	textRect->set_width(x + 102);
	textRect->set_height(y + 12);
	
	textOldRect->set_x(mouseX - 1);
	textOldRect->set_y(mouseY - 11);
	textOldRect->set_width(mouseX + 102);
	textOldRect->set_height(mouseY + 12);
	textRect->join(*textOldRect);
	
	selectedRect->join(*textRect);	// for to work with one redraw join area
	
	mouseX = x;
	mouseY = y;

    double dx = abs(markX - mouseX) / m_timescale->ft;                    
    std::string period = m_timescale->freeFormat(dx, "s"); 	
    std::string freq = m_timescale->freeFormat(1.0/(dx), "Hz");	// as a hint display frequency
	m_info = period + std::string("\n") + freq;

	//std::cout << "pres " << selectedRect->x << "," << selectedRect->y
	//          << " w " <<  selectedRect->width << "," << selectedRect->height << endl;
	Gtk::Allocation allocation = get_allocation();
	queue_draw_area(selectedRect->get_x() + allocation.get_x(), 
					selectedRect->get_y() + allocation.get_y(), 
					selectedRect->get_width(), selectedRect->get_height());	
    delete selectedRect;
    delete textOldRect;
    delete textRect;          
	return Gtk::TreeView::on_motion_notify_event(event);	// Keep selectable / clickable
}

bool
DisplayTree::on_button_release_event(GdkEventButton* event)
{
	//mouseX = markX = -1.0;
	//mouseY = markY = -1.0;
	return Gtk::TreeView::on_button_release_event(event);	// Keep selectable / clickable
}

bool
DisplayTree::on_button_press_event(GdkEventButton* event)
{
    //std::cout 
    //   << "p " << event->x 
    //   << " " << event->y 
    //   << " " << event->button
    //   << std::endl;
	
    if (event->button == 1) {
		double x = event->x;
		double y = event->y + getYOffset();
			
		mouseX = markX = x;
		mouseY = markY = y;
		
		Gtk::Allocation allocation = get_allocation();
		queue_draw_area(event->x - 1 + allocation.get_x(), 
						event->y - 1 + allocation.get_y(), 
						2, 2);	
	}
	return Gtk::TreeView::on_button_press_event(event);	// Keep selectable / clickable
}

int 
DisplayTree::getYOffset() 
{
	Gtk::CellRenderer* cellRenderer = get_column_cell_renderer(0);
	int width, height;
	cellRenderer->get_preferred_height(*this, width, height);
	return height;  
}

bool
DisplayTree::on_draw(const Cairo::RefPtr<Cairo::Context> &cr)
{
	bool ret = Gtk::TreeView::on_draw(cr);	
    //cr->set_source_rgb(0.0, 0.0, 0.0);
    //cr->set_source(pixmap, 0, 0);
    //cr->paint();

	cr->set_font_size(10.0);
	//cr->set_source_rgb(1.0, 0.0, 0.0);
	//cr->move_to(FRONT + 5, bit2pos(0) + 10);
	//cr->show_text(msg);    

   if (mouseX >= 0.0
     && markX >= 0.0
     && mouseY >= 0.0
     && markY >= 0.0) {
		cr->set_source_rgb(0.0, 1.0, 0.0);

		cr->move_to(markX, markY);
		cr->line_to(mouseX, mouseY);
		cr->stroke();       
		
		double l = 0.0;
		Cairo::FontExtents fe;
		cr->get_font_extents(fe);
		std::istringstream f(m_info);
		std::string s;    
		while (getline(f, s, '\n')) {
			cr->move_to(mouseX + 10.0, mouseY + l);
			cr->show_text(s);
			l += fe.height;
		}		
	}

    //std::cout << "End paint\n";
    return ret;
}


