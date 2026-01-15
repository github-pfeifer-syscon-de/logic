/* -*- Mode: C; indent-tabs-mode: t; c-basic-offset: 4; tab-width: 4 -*-  */
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


class ModelColumns : public Gtk::TreeModel::ColumnRecord
{
public:
    ModelColumns();
    virtual ~ModelColumns(); 

    Gtk::TreeModelColumn<uint32_t> m_col_bit;
    Gtk::TreeModelColumn<bool> m_col_trigger_riseedge;
    Gtk::TreeModelColumn<bool> m_col_trigger_falledge;
    Gtk::TreeModelColumn<Glib::RefPtr<Gdk::Pixbuf>> m_col_data;

private:

};
