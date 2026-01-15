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

enum Result {
	OK,
	SETUP_REQUIRED,
	ERROR
};

class SetupAssist {
public:
    SetupAssist();
    virtual ~SetupAssist();

	void start(Gtk::Window& parent);
	Result checkBootConfig();
	Result checkUdev();

protected:    
	void assistant_apply();
	void assistant_changed();
	void assistant_prepare(Gtk::Widget* widget);
	void assistant_cancel();
	void assistant_close();
	void definePages(Glib::RefPtr<Gtk::Builder> refBuilder);
	void doBootConfig();
	void doUdevConfig();
	void parsePwd(std::string& s);

private:
	void showBootConfig();
	void showUdev();
	const char *bootconfig = "/boot/config.txt";
	const char *udevrule = "/etc/udev/rules.d/99-gpio.rules";

    Glib::RefPtr<Gtk::Assistant> m_assistant;
    Glib::RefPtr<Gtk::CheckButton> m_startCheck;
    Glib::RefPtr<Gtk::CheckButton> m_configCheck;
    Glib::RefPtr<Gtk::CheckButton> m_udevCheck;
    Glib::RefPtr<Gtk::ComboBoxText> m_userCombo;

};

