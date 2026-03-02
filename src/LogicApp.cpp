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

#include <gtkmm.h>
#include <iostream>
#include <exception>
#include <thread>
#include <future>
// Localisation https://developer.gnome.org/glib/stable/glib-I18N.html
#include <glib.h>
#include <glib/gi18n.h>

// Gettext
//#include <libintl.h>

#include "logic_config.h"		// to get PACKAGE
#include "LogicApp.hpp"

LogicApp::LogicApp(int argc, char **argv)
: Gtk::Application(argc, argv, "de.pfeifer_syscon.logic")
{
}


LogicApp::~LogicApp()
{
}

void
LogicApp::on_activate()
{
    add_window(m_logicAppWindow);
    m_logicAppWindow.present();
}


void
LogicApp::on_action_quit()
{
     m_logicAppWindow.hide();

  // Not really necessary, when Gtk::Widget::hide() is called, unless
  // Gio::Application::hold() has been called without a corresponding call
  // to Gio::Application::release().
  quit();
}

void
LogicApp::on_startup()
{
  // Call the base class's implementation.
  Gtk::Application::on_startup();

  // Add actions and keyboard accelerators for the application menu.
  add_action("start", sigc::mem_fun(m_logicAppWindow, &LogicWindow::on_action_single));
  add_action("save", sigc::mem_fun(m_logicAppWindow, &LogicWindow::on_action_save));
  add_action("about", sigc::mem_fun(m_logicAppWindow, &LogicWindow::on_action_about));
  add_action("help", sigc::mem_fun(m_logicAppWindow, &LogicWindow::on_action_help));
  add_action("quit", sigc::mem_fun(*this, &LogicApp::on_action_quit));
  set_accel_for_action("app.quit", "<Ctrl>Q");

    auto refBuilder = Gtk::Builder::create();
    try {
        refBuilder->add_from_resource("/de/pfeifer_syscon/logic/app-menu.ui");

        auto object = refBuilder->get_object("appmenu");
        auto app_menu = Glib::RefPtr<Gio::MenuModel>::cast_dynamic(object);
        if (app_menu) {
			set_menubar(app_menu);	// set_app_menu will result in single application menu entry
		}
        else
          std::cerr << "LogicApp::on_startup(): No \"appmenu\" object in app_menu.ui"
                    << std::endl;
    }
    catch (const Glib::Error& ex) {
      std::cerr << "LogicApp::on_startup(): " << ex.what() << std::endl;
      return;
    }
}

int main(int argc, char** argv) {

    auto app = LogicApp(argc, argv);

    return app.run();
}
