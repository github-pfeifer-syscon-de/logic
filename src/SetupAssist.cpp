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
#include <fstream> 
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>

#include "SetupAssist.hpp"

SetupAssist::SetupAssist() 
{
}


SetupAssist::~SetupAssist() 
{
}

void
SetupAssist::start(Gtk::Window& parent)
{
    auto refBuilder = Gtk::Builder::create();
    try {
        refBuilder->add_from_resource("/de/pfeifer_syscon/logic/intro-dlg.ui");
        auto object = refBuilder->get_object("intro-dlg");
        m_assistant = Glib::RefPtr<Gtk::Assistant>::cast_dynamic(object);
        if (m_assistant) {
            m_assistant->set_transient_for(parent);

			auto o_configCheck = refBuilder->get_object("configCheck");
			m_configCheck = Glib::RefPtr<Gtk::CheckButton>::cast_dynamic(o_configCheck);
            
			auto o_udevCheck = refBuilder->get_object("udevCheck");
			m_udevCheck = Glib::RefPtr<Gtk::CheckButton>::cast_dynamic(o_udevCheck);

			auto o_startCheck = refBuilder->get_object("startCheck");
			m_startCheck = Glib::RefPtr<Gtk::CheckButton>::cast_dynamic(o_startCheck);

			auto o_userCombo = refBuilder->get_object("userCombo");
			m_userCombo = Glib::RefPtr<Gtk::ComboBoxText>::cast_dynamic(o_userCombo);

			definePages(refBuilder);

            //m_assistant->signal_destory().connect(sigc::mem_fun(*this, &SetupAssist::done));
            m_assistant->signal_prepare().connect(sigc::mem_fun(*this, &SetupAssist::assistant_prepare));
			//m_assistant->signal_changed().connect(sigc::mem_fun(*this, &LogicWindow::assistant_changed));
            m_assistant->signal_apply().connect(sigc::mem_fun(*this, &SetupAssist::assistant_apply));
            m_assistant->signal_cancel().connect(sigc::mem_fun(*this, &SetupAssist::assistant_cancel));
            m_assistant->signal_close().connect(sigc::mem_fun(*this, &SetupAssist::assistant_close));            
                                    
            m_assistant->show();
            //m_assistant->request_focus();
        } else
            std::cerr << "SetupAssist::start: No \"intro-dlg\" object in intro-dlg.ui"
                << std::endl;
    } catch (const Glib::Error& ex) {
        std::cerr << "SetupAssist::start: " << ex.what() << std::endl;
    }
}

void 
SetupAssist::definePages(Glib::RefPtr<Gtk::Builder> refBuilder) 
{
	auto o_startBox = refBuilder->get_object("startBox");
	auto startBox = Glib::RefPtr<Gtk::Box>::cast_dynamic(o_startBox);

	auto o_startLbl = refBuilder->get_object("startLbl");
	auto startLbl = Glib::RefPtr<Gtk::Label>::cast_dynamic(o_startLbl);

	auto o_udevBox = refBuilder->get_object("udevBox");
	auto udevBox = Glib::RefPtr<Gtk::Box>::cast_dynamic(o_udevBox);

	auto o_configBox = refBuilder->get_object("configBox");
	auto configBox = Glib::RefPtr<Gtk::Box>::cast_dynamic(o_configBox);

	auto o_resultBox = refBuilder->get_object("resultBox");
	auto resultBox = Glib::RefPtr<Gtk::Box>::cast_dynamic(o_resultBox);

	
	// allow free movement
	m_assistant->set_page_complete(*startBox.operator->(), true);
	m_assistant->set_page_complete(*configBox.operator->(), true);
	m_assistant->set_page_complete(*udevBox.operator->(), true);
	m_assistant->set_page_complete(*resultBox.operator->(), true);
	
	std::string intro;
	Result configResult = checkBootConfig();
	Result gpioResult = checkUdev();
	Gtk::AssistantPageType introPage = Gtk::ASSISTANT_PAGE_INTRO;
	m_assistant->set_page_type(*configBox.operator->(), configResult == OK 
														? Gtk::ASSISTANT_PAGE_CONTENT
														: Gtk::ASSISTANT_PAGE_CONFIRM);
	m_assistant->set_page_type(*udevBox.operator->(), gpioResult == OK 
														? Gtk::ASSISTANT_PAGE_CONTENT
														: Gtk::ASSISTANT_PAGE_CONFIRM);

	if (geteuid() == 0) {
		switch (configResult) {
			case OK:
				intro = "No additional setup is required.\n";
				if (gpioResult != OK) {
					intro += "But if you want some assistance to setup user premissions please continue.";
				}
				else {
					introPage = Gtk::ASSISTANT_PAGE_CONFIRM;
				}
				break;
			default:
				intro =	"This program will need some preparation for GPIO access.\n" ;
						"Read the following steps carefully as for a reliable usage this setup will be needed.\n"
						"Without this setup the edge detection / trigger will be disabled!\n";
				break;
		}
	}
	else {
		intro =	"This program will need some preparation for GPIO access.\n" 
				"Read the following steps carefully as for a reliable usage this setup will be needed.\n"
				"You can do this config assisted if you run this intro once as root.\n";
		switch (configResult) {
			case OK:
				if (gpioResult == OK) {
					intro = "No additional setup is required.\n";
					introPage = Gtk::ASSISTANT_PAGE_CONFIRM;
				}
				break;
			default:
				intro += "Without this setup the edge detection / trigger will be disabled!\n";
				break;
		}		
	}
	startLbl->set_label(intro);
	m_assistant->set_page_type(*startBox.operator->(), introPage);
	m_assistant->set_page_type(*resultBox.operator->(), Gtk::ASSISTANT_PAGE_CONFIRM);
	
}

void 
SetupAssist::assistant_changed()  
{
//  if(m_entry.get_text_length())
//    set_page_complete(m_box, true);
//  else
//    set_page_complete(m_box, false);
}

void 
SetupAssist::assistant_apply() 
{
	gint pageNum = m_assistant->get_current_page();
	if (geteuid() == 0) {
		switch (pageNum) {
			case 1:
				doBootConfig();
				break;
			case 2:
				doUdevConfig();
				break;
		}
	}
	if (geteuid() != 0 
	 || pageNum == 3) {	// hide on last page
		m_assistant->hide();		
	}
}


void 
SetupAssist::doBootConfig() 
{
	std::fstream f;
	f.open(bootconfig, std::fstream::out | std::fstream::app);
	f << "dtoverlay=gpio-no-irq" << std::endl;
	f.close();
		
	m_assistant->commit();	
}

Result
SetupAssist::checkBootConfig() 
{
	Result result;
	if (access(bootconfig, R_OK)) {
		result = ERROR;
	}
	else {
		std::fstream f;
		f.open(bootconfig, std::fstream::in);
		std::string s;    
		bool found = false;
		while (getline(f, s, '\n')) {	
			if (s.rfind("dtoverlay=gpio-no-irq", 0) == 0) {
				found = true;
				break;
			}
		}	
		if (found) {
			result = OK;
		}
		else {
			result = SETUP_REQUIRED;
		}
		f.close();
	}
	return result;
} 

void
SetupAssist::showBootConfig() 
{
	m_configCheck->set_sensitive(false);
	Result bootConfig = checkBootConfig();
	const char *result;
	switch(bootConfig) {
		case OK:
			result = "Matching line was found.";
			m_configCheck->set_active(true);
			break;
		case SETUP_REQUIRED:
			result = "Matching line coud not be found, please add it. Otherwise edge detect / trigger function will remain disabled!";
			break;
		case ERROR:
			result = "Error checking access please check manually.";
			break;
		
	}
	m_configCheck->set_label(result);
} 


void 
SetupAssist::doUdevConfig()
{
	if (access(udevrule, R_OK) == -1) {	// if the file happen to exists do nothing
		std::fstream f;
		f.open(udevrule, std::fstream::out | std::fstream::app);
		f << "SUBSYSTEM==\"gpio*\", PROGRAM=\"/bin/sh -c 'chown -R root:gpio /sys/class/gpio && chmod -R 770 /sys/class/gpio; chown -R root:gpio /sys/devices/virtual/gpio && chmod -R 770 /sys/devices/virtual/gpio; chown -R root:gpio /sys/devices/platform/soc/*.gpio/gpio && chmod -R 770 /sys/devices/platform/soc/*.gpio/gpio'\"" << std::endl;
		//f << "SUBSYSTEM==\"i2c-dev\", GROUP=\"i2c\", MODE=\"0660\"" << std::endl;
		//f << "SUBSYSTEM==\"spidev\", GROUP=\"spi\", MODE=\"0660\"" << std::endl;
		f << "SUBSYSTEM==\"bcm2835-gpiomem\", GROUP=\"gpio\", MODE=\"0660\"" << std::endl;
		f.close();
	}

	int retGrp = system("groupadd gpio");
	std::cout << "done groupadd ret " << retGrp << std::endl;
	//if (m_userCombo->get_active_id() != nullptr) {
		char buf[80];
		Glib::ustring user = m_userCombo->get_active_id();
		snprintf(buf, sizeof(buf), "usermod -a -G gpio %s", user.c_str());
		int retUser = system(buf);
		std::cout << "done " << buf << " ret " << retUser << std::endl;
	//}
	m_assistant->commit();	
}


Result 
SetupAssist::checkUdev()
{
	const char *gpio = "/dev/gpiomem";
	struct stat struct_stat;
	if (stat(gpio, &struct_stat) == -1) {
		return ERROR;
	}
	else {
		if (struct_stat.st_gid == 0) {	// if udev rule works grp shoud not be root
			return SETUP_REQUIRED;
		}
	}
	return OK;
}

void 
SetupAssist::parsePwd(std::string& s) 
{
	std::istringstream f(s);
	std::string fld;    
	std::string user;    
	int i = 0;
	while (getline(f, fld, ':')) {
		switch (i) {
		case 0:
			user = fld;
			break;
		case 2:
			int uid = atoi(fld.c_str());
			//std::cout << "  user " << user << " uid " << uid << std::endl;
			if (uid >= 1000 
			 && uid < 65000) {		// coud check login defs
				m_userCombo->append(user, user);
			}
			break;
		}
		++i;
	}
}

void 
SetupAssist::showUdev() 
{
	m_udevCheck->set_sensitive(false);
	if (geteuid() == 0) {
		std::fstream f;
		f.open("/etc/passwd", std::fstream::in);
		std::string s;    
		while (getline(f, s, '\n')) {	
			//std::cout << "passwd " << s << std::endl;
			parsePwd(s);
		}	
		f.close();		
		m_userCombo->set_active(0);
	}
	else {
		m_userCombo->set_sensitive(false);
	}
	
	
	const char *result;
	Result udevResult = checkUdev();
	switch (udevResult) {
	case OK:
		result = "Required permission is set.";
		m_udevCheck->set_active(true);
		break;
	case ERROR:
		result = "Cound not verify setting, please verify!";
		break;
	case SETUP_REQUIRED:
		result = "Please set the required permission.";
		break;
	}
	m_udevCheck->set_label(result);	
}

void 
SetupAssist::assistant_prepare(Gtk::Widget* widget) 
{
	m_assistant->set_title(Glib::ustring::compose("Gtk::Assistant example (Page %1 of %2)",
									m_assistant->get_current_page() + 1, m_assistant->get_n_pages()));
	gint pageNum = m_assistant->get_current_page();
	switch (pageNum) {
		case 0:
			break;
		case 1:
			showBootConfig();
			break;
		case 2:
			showUdev();
			break;
	}
	//Gtk::Widget *page = m_assistant->get_nth_page(num);
	//m_assistant->set_page_complete(*page, true);
}


void 
SetupAssist::assistant_cancel() 
{
	m_assistant->hide();
}

void 
SetupAssist::assistant_close()
{
	m_assistant->hide();
}

