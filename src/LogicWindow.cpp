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
#include <fstream> 
#include <thread>
#include <future>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
// Localisation https://developer.gnome.org/glib/stable/glib-I18N.html 
#include <glib.h>
#include <glib/gi18n.h>

#include "LogicWindow.hpp"
#include "LogicApp.hpp"
#include "Capture.hpp"
#include "SetupAssist.hpp"

LogicWindow::LogicWindow()
: Gtk::ApplicationWindow()
, m_planeView(NULL)
, m_Columns()
{
	Glib::RefPtr<Gdk::Pixbuf> pix = Gdk::Pixbuf::create_from_resource("/de/pfeifer_syscon/logic/icon.png");
	set_icon(pix);
	read_config();
	
    auto vBox = Gtk::manage(new Gtk::VBox());
    vBox->set_homogeneous(false);
    add(*vBox);
    auto hBox = Gtk::manage(new Gtk::HButtonBox());
    hBox->set_homogeneous(false);
    vBox->pack_start(*hBox);
    //Gtk::Viewport* viewport = (Gtk::Viewport*)m_planeView->get_parent();
	m_info = Gtk::manage(new Gtk::Label());
	m_info->override_color(Gdk::RGBA("#ff0000"));
	hBox->pack_start(*m_info);
    
    auto timeLabel = Gtk::manage(new Gtk::Label(_("Time:")));
    hBox->pack_start(*timeLabel);    
    time = Gtk::manage(new Gtk::ComboBoxText());
    time->append("0", _("fastest (estm. time)"));	// work without recorded time
    time->append("1", _("fast"));		// work without delay ~2MSamples (Raspi 4)
    //time->append("1000", "1us");	// for this we need a diffrent concept than just nanosleep
    time->append("5000", "5us");	// this will also be somewhat inaccurate
    time->append("10000", "10us");
    time->append("20000", "20us");
    time->append("50000", "50us");
    time->append("100000,", "100us");
    time->append("200000,", "200us");
    time->append("500000,", "500us");
    time->append("1000000", "1ms");
    time->append("2000000", "2ms");
    time->append("5000000", "5ms");
    time->append("10000000", "10ms");
    if (has_config(CONFIG_GRP_MAIN, CONFIG_TIME)) {
		time->set_active_id(get_config_string(CONFIG_GRP_MAIN, CONFIG_TIME));
	}
	else {
		time->set_active(0);
	}
    hBox->pack_start(*time);
    //auto triggerLabel = Gtk::manage(new Gtk::Label(_("Trigger:")));
    //hBox->pack_start(*triggerLabel);    
    //trigger = Gtk::manage(new Gtk::ComboBoxText());
    //trigger->append("-1", _("none"));
    //
    //for (int bit = 0; bit < LogicDrawing::BITS; ++bit) {
	//	std::ostringstream oss;
	//	oss << bit;
	//	trigger->append(oss.str(), oss.str());
	//}
    //if (has_config(CONFIG_GRP_MAIN, CONFIG_TRIGGER)) {
	//	trigger->set_active_id(get_config_string(CONFIG_GRP_MAIN, CONFIG_TRIGGER));
	//}
	//else {
	//	trigger->set_active(0);
	//}
    //hBox->pack_start(*trigger);
    
    continious = Gtk::manage(new Gtk::ToggleButton());
    continious->set_label(_("Continious"));
    continious->signal_toggled().connect(sigc::mem_fun(*this, &LogicWindow::on_action_continious));
    hBox->pack_start(*continious);
    single = Gtk::manage(new Gtk::ToggleButton());
    single->set_label(_("Single"));
    single->signal_toggled().connect(sigc::mem_fun(*this, &LogicWindow::on_action_single));
    hBox->pack_start(*single);
    //level = Gtk::manage(new Gtk::ComboBoxText());
    //level->append("h", "high");
    //level->append("l", "low");
    //level->set_active(0);
    //hBox->pack_start(*level);
    //set_decorated(FALSE);
	
    m_refTreeModel = Gtk::ListStore::create(m_Columns);
    uint32_t bits_riseEdge = get_config_uint(CONFIG_GRP_MAIN, CONFIG_TRIGGERS_RISEEDGE);
    uint32_t bits_fallEdge = get_config_uint(CONFIG_GRP_MAIN, CONFIG_TRIGGERS_FALLEDGE);
    for (uint32_t i = 0; i < LogicDrawing::BITS; ++i) {
		Gtk::TreeModel::Row row = *(m_refTreeModel->append());
		row[m_Columns.m_col_bit] = i;
		row[m_Columns.m_col_trigger_riseedge] = (bits_riseEdge & (1u << i));
		row[m_Columns.m_col_trigger_falledge] = (bits_fallEdge & (1u << i));
		//row[m_Columns.m_col_data] = i;    
	}
    
    m_planeView = new LogicDrawing(this, *single);
	
    m_viewTable = Gtk::manage(new DisplayTree(m_refTreeModel, m_planeView->getTimescale(), m_Columns));    
	
    auto scroll = Gtk::manage(new Gtk::ScrolledWindow());
    auto display = Gdk::Display::get_default();
    auto screen = display->get_default_screen();
    scroll->set_min_content_height(min(600, screen->get_width() - 64));
    scroll->set_min_content_width(min(Capture::BUF_LEN, screen->get_height() - 64));
    vBox->pack_start(*scroll);    
    scroll->add(*m_viewTable);
    //
    //m_planeView->create_pixmap();
    show_all_children();
    
    check_intro();
}

LogicWindow::~LogicWindow()
{
	if (m_planeView) {
		delete m_planeView;
	}
	m_planeView = NULL;
}

bool 
LogicWindow::on_delete_event(GdkEventAny *any_event) 	
{
	save_config();
	return Gtk::Widget::on_delete_event(any_event);
}

void
LogicWindow::setPixbuf(uint32_t bit, Glib::RefPtr<Gdk::Pixbuf> pixbuf)
{
	Gtk::TreeModel::Children children = m_refTreeModel->children();
	for (Gtk::TreeModel::Children::iterator iter = children.begin(); iter != children.end(); ++iter) {
		Gtk::TreeModel::Row row = *iter;
		if (row[m_Columns.m_col_bit] == bit) {
			row[m_Columns.m_col_data] = pixbuf;    
			break;
		}
	}	
}

void 
LogicWindow::setTimescale(Gtk::Image &image)
{
	auto data_col = m_viewTable->get_column(m_viewTable->data_column_num);
	//image.set_halign(Gtk::ALIGN_CENTER);
	data_col->set_widget(image);
	data_col->set_alignment(0.0f);	// make table header left aligned
	//data_col->get_button()->set_alignment(0.5f, 1.0f);
	image.show();		// required for the image to show up 
}

void 
LogicWindow::setMessage(Glib::ustring &msg)
{
	//m_info->set_markup("<span foreground=\"red\">" + msg + "</span>" );
	m_info->set_text(msg);
}

void
LogicWindow::set_input()
{
    Glib::ustring stime = time->get_active_id();
    m_planeView->setTime(stime);
    m_config->set_string(CONFIG_GRP_MAIN, CONFIG_TIME, stime);
    uint32_t triggerRiseEdge = 0;
    uint32_t triggerFallEdge = 0;
    Gtk::TreeModel::Children children = m_refTreeModel->children();
    uint32_t mask = 1u;
	for (Gtk::TreeModel::Children::iterator iter = children.begin(); iter != children.end(); ++iter) {
		Gtk::TreeModel::Row row = *iter;
		if (row[m_Columns.m_col_trigger_riseedge]) {
			triggerRiseEdge |= mask;
		}
		if (row[m_Columns.m_col_trigger_falledge]) {
			triggerFallEdge |= mask;
		}
		mask <<= 1u;
	}    
    m_planeView->setTrigger(triggerRiseEdge, triggerFallEdge);    
    m_config->set_int64(CONFIG_GRP_MAIN, CONFIG_TRIGGERS_RISEEDGE, triggerRiseEdge);
    m_config->set_int64(CONFIG_GRP_MAIN, CONFIG_TRIGGERS_FALLEDGE, triggerFallEdge);

    //Glib::ustring lev = level->get_active_id();
    //m_planeView->setLevel(lev);
}

void
LogicWindow::on_action_continious()
{
	set_input();
	m_planeView->continious(continious->get_active());
}

void
LogicWindow::on_action_single()
{
    if (continious->get_active())
        continious->set_active(false);
    if (single->get_active()) {
		set_input();
		m_planeView->single();
	}
	else {
		m_planeView->threadTerminate();
	}
}

void 
LogicWindow::on_action_save()
{
    m_planeView->on_action_save();
}

void
LogicWindow::on_action_about()
{
    auto refBuilder = Gtk::Builder::create();
    try {
        refBuilder->add_from_resource("/de/pfeifer_syscon/logic/abt-dlg.ui");
        auto object = refBuilder->get_object("abt-dlg");
        auto abtdlg = Glib::RefPtr<Gtk::AboutDialog>::cast_dynamic(object);
        if (abtdlg) {
            Glib::RefPtr<Gdk::Pixbuf> pix = Gdk::Pixbuf::create_from_resource("/de/pfeifer_syscon/logic/icon.png");
            abtdlg->set_logo(pix);
            abtdlg->set_transient_for(*this);
            abtdlg->run();
            abtdlg->hide();
        } else
            std::cerr << "LogicWindow::on_action_about(): No \"abt-dlg\" object in abt-dlg.ui"
                << std::endl;
    } catch (const Glib::Error& ex) {
        std::cerr << "LogicWindow::on_action_about(): " << ex.what() << std::endl;
    }
}

void
LogicWindow::on_action_help()
{
    auto refBuilder = Gtk::Builder::create();
    try {
        refBuilder->add_from_resource("/de/pfeifer_syscon/logic/help-dlg.ui");
        auto object = refBuilder->get_object("help-dlg");
        auto helpdlg = Glib::RefPtr<Gtk::Assistant>::cast_dynamic(object);
        if (helpdlg) {
            helpdlg->set_transient_for(*this);
            
			auto o_page0 = refBuilder->get_object("page0");
			auto page0 = Glib::RefPtr<Gtk::Label>::cast_dynamic(o_page0);
			auto o_page1 = refBuilder->get_object("page1");
			auto page1 = Glib::RefPtr<Gtk::Label>::cast_dynamic(o_page1);
			auto o_page2 = refBuilder->get_object("page2");
			auto page2 = Glib::RefPtr<Gtk::Label>::cast_dynamic(o_page2);
			
			helpdlg->set_page_complete(*page0.operator->(), true);
			helpdlg->set_page_complete(*page1.operator->(), true);
			helpdlg->set_page_complete(*page2.operator->(), true);
			helpdlg->set_page_type(*page2.operator->(), Gtk::ASSISTANT_PAGE_CONFIRM);
	           
            helpdlg->signal_cancel().connect(sigc::mem_fun(*helpdlg.operator->(), &Gtk::Assistant::hide));
            helpdlg->signal_close().connect(sigc::mem_fun(*helpdlg.operator->(), &Gtk::Assistant::hide));                        
            
            helpdlg->show();
        } else
            std::cerr << "LogicWindow::on_action_help(): No \"help-dlg\" object in help-dlg.ui"
                << std::endl;
    } catch (const Glib::Error& ex) {
        std::cerr << "LogicWindow::on_action_help(): " << ex.what() << std::endl;
    }
}

void
LogicWindow::check_intro()
{
	const char *configlock = "/tmp/logic_configlock.tmp";
	auto setupAssist = new SetupAssist();

	Result configResult = SETUP_REQUIRED;
	Result udevResult = SETUP_REQUIRED;
	if (access(configlock, R_OK) != 0) {	// make the fist result stick until a reboot
		configResult = setupAssist->checkBootConfig();
		udevResult = setupAssist->checkUdev();
		if (configResult == SETUP_REQUIRED) {
			std::fstream f;
			f.open(configlock, std::fstream::out);
			f << configResult;
			f.close();
		}
	}
	m_planeView->setEdgeEnabled(configResult == OK);
	bool init = m_planeView->getInit();	// if init didnt work we need some setup
	if (configResult != OK 
	 || udevResult != OK
	 || !init) {
		setupAssist->start(*this);
	}
}

std::string
LogicWindow::get_config_name()
{
    char tmp[128];
    const gchar *cfgdir = g_get_user_config_dir();
    snprintf(tmp, sizeof(tmp), "%s/%s", cfgdir, "logic.conf");
    return std::string(tmp);
}


void
LogicWindow::read_config()
{
    m_config = new Glib::KeyFile();

    std::string cfg = get_config_name();
    try {
        if (!m_config->load_from_file(cfg, Glib::KEY_FILE_NONE)) {
            std::cerr << "Error loading " << cfg << std::endl;
        }
    }
    catch (Glib::FileError exc) {
        // may happen if didn't create a file (yet) but we can go on
        std::cerr << "File Error loading " << cfg << " if its missing, it will be created?" << std::endl;
    }
}

bool
LogicWindow::has_config(const char *grp, const char *key)
{
	return (m_config 
	 && m_config->has_group(grp) 
	 && m_config->has_key(grp, key));
}

Glib::ustring
LogicWindow::get_config_string(const char *grp, const char *key)
{
	if (has_config(grp, key)) {
		return m_config->get_string(grp, key);
	}
	return Glib::ustring("");
}

uint32_t
LogicWindow::get_config_uint(const char *grp, const char *key)
{
	if (has_config(grp, key)) {
		return (uint32_t)m_config->get_int64(grp, key);
	}
	return 0u;
}

void
LogicWindow::save_config()
{
    if (m_config) {
        std::string cfg = get_config_name();
        if (!m_config->save_to_file(cfg)) {
             std::cerr << "Error saving " << cfg << std::endl;
        }
    }
}

