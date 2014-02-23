/*
 * Copyright (c) 2002-2007  Daniel Elstner  <daniel.kitta@gmail.com>
 *
 * This file is part of regexxer.
 *
 * regexxer is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * regexxer is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with regexxer; if not, write to the Free Software Foundation,
 * Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
 */

#include "controller.h"

#include <gtkmm/button.h>
#include <gtkmm/menu.h>
#include <gtkmm/toolbutton.h>
#include <gtkmm/builder.h>

#include <config.h>


namespace Regexxer
{

/**** Regexxer::ControlItem ************************************************/

ControlItem::ControlItem(bool enable)
:
  enabled_       (enable),
  group_enabled_ (true)
{}

ControlItem::~ControlItem()
{}

void ControlItem::activate()
{
  if (enabled_ && group_enabled_)
    signal_activate_(); // emit
}

sigc::slot<void> ControlItem::slot()
{
  return sigc::mem_fun(*this, &ControlItem::activate);
}

void ControlItem::connect(const sigc::slot<void>& slot_activated)
{
  signal_activate_.connect(slot_activated);
}

void ControlItem::add_widget(Gtk::Widget& widget)
{
  signal_set_sensitive_.connect(sigc::mem_fun(widget, &Gtk::Widget::set_sensitive));
  widget.set_sensitive(enabled_ && group_enabled_);
}

void ControlItem::add_widgets(const Glib::RefPtr<Gtk::Builder>& xml,
                              const char* menuitem_name, const char* button_name)
{
  const sigc::slot<void> slot_activate = slot();

  Gtk::MenuItem* menuitem = 0;
  Gtk::Widget*   widget   = 0;

  if (menuitem_name)
    xml->get_widget(menuitem_name, menuitem);

  if (button_name)
    xml->get_widget(button_name, widget);
  
  if (menuitem_name)
  {
    menuitem->signal_activate().connect(slot_activate);
    add_widget(*menuitem);
  }

  if (button_name)
  {
    if (Gtk::ToolButton *const button = dynamic_cast<Gtk::ToolButton*>(widget))
      button->signal_clicked().connect(slot_activate);
    else if (Gtk::Button *const button = dynamic_cast<Gtk::Button*>(widget))
      button->signal_clicked().connect(slot_activate);
    else
      g_return_if_reached();

    add_widget(*widget);
  }
}

void ControlItem::set_enabled(bool enable)
{
  if (enable != enabled_)
  {
    enabled_ = enable;

    if (group_enabled_)
      signal_set_sensitive_(enabled_); // emit
  }
}

void ControlItem::set_group_enabled(bool enable)
{
  if (enable != group_enabled_)
  {
    group_enabled_ = enable;

    if (enabled_)
      signal_set_sensitive_(group_enabled_); // emit
  }
}

bool ControlItem::is_enabled() const
{
  return (enabled_ && group_enabled_);
}


/**** Regexxer::ControlGroup ***********************************************/

ControlGroup::ControlGroup(bool enable)
:
  enabled_ (enable)
{}

ControlGroup::~ControlGroup()
{}

void ControlGroup::add(ControlItem& control)
{
  signal_set_enabled_.connect(sigc::mem_fun(control, &ControlItem::set_group_enabled));
  control.set_group_enabled(enabled_);
}

void ControlGroup::set_enabled(bool enable)
{
  if (enable != enabled_)
  {
    enabled_ = enable;
    signal_set_enabled_(enabled_); // emit
  }
}


/**** Regexxer::Controller *************************************************/

Controller::Controller()
:
  match_actions (true),
  find_files    (false),
  find_matches  (false)
{
  match_actions.add(find_files);
  match_actions.add(find_matches);
}

Controller::~Controller()
{}

void Controller::load_xml(const Glib::RefPtr<Gtk::Builder>& xml)
{
  find_files  .add_widgets(xml, 0,                       "button_find_files");
  find_matches.add_widgets(xml, 0,                       "button_find_matches");
}

} // namespace Regexxer
