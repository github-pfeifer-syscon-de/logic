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

#include "LogicDrawing.hpp"
#include "ModelColums.hpp"
#include "DisplayTree.hpp"

class LogicWindow : public Gtk::ApplicationWindow {
public:
    LogicWindow();
    virtual ~LogicWindow();

    void set_input();
    void on_action_continious();
    void on_action_single();
    void on_action_save();
    void on_action_about();
    void on_action_help();
	void check_intro();
	
	const char *CONFIG_GRP_MAIN = "main";
	const char *CONFIG_TIME = "time";
	const char *CONFIG_TRIGGERS_RISEEDGE = "triggersRiseEdge";
	const char *CONFIG_TRIGGERS_FALLEDGE = "triggersFallEdge";
	bool on_delete_event(GdkEventAny *any_event) override;
	Glib::ustring get_config_string(const char *grp, const char *key);
	uint32_t get_config_uint(const char *grp, const char *key);
	bool has_config(const char *grp, const char *key);
	void setMessage(Glib::ustring &msg);
	void setPixbuf(uint32_t bit, Glib::RefPtr<Gdk::Pixbuf> pixbuf);
	void setTimescale(Gtk::Image &image );

protected:
	std::string get_config_name();
	void read_config();
	void save_config();
	Glib::KeyFile *m_config;
	Gtk::Label *m_info;

private:
    ModelColumns m_Columns;
    Glib::RefPtr<Gtk::ListStore> m_refTreeModel;
    DisplayTree *m_viewTable;

    LogicDrawing *m_planeView;
    Gtk::ToggleButton *continious;
    Gtk::ToggleButton *single;
    Gtk::ComboBoxText *time;
    Gtk::ComboBoxText *trigger;
    //Gtk::ComboBoxText *level;
};

