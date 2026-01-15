/* -*- Mode: C; indent-tabs-mode: t; c-basic-offset: 4; tab-width: 4 -*-  */
/*
 * displayTree.hpp
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

#pragma once 

#include <gtkmm.h>

#include "Timescale.hpp"
#include "ModelColums.hpp"

class DisplayTree : public Gtk::TreeView
{
public:
	DisplayTree(Glib::RefPtr<Gtk::ListStore> listStore, Timescale *timescale, ModelColumns &columns);
	virtual ~DisplayTree();

	bool on_motion_notify_event(GdkEventMotion* event) override;
	bool on_button_release_event(GdkEventButton* event) override;
	bool on_button_press_event(GdkEventButton* event) override;

	bool on_draw(const Cairo::RefPtr<Cairo::Context> &cr) override;

	void on_trigger_toggled(const Glib::ustring& path, Gtk::TreeModelColumn<bool> &col);
	//void on_trigger_toggled(const Glib::ustring& path);
	uint32_t data_column_num;
protected:

	int getYOffset();
	void createToggleColumn(Gtk::TreeModelColumn<bool> &col, const char *name);
 
private:
	Timescale *m_timescale;
	float markX,markY;
	float mouseX,mouseY;
	Glib::ustring m_info;

};

