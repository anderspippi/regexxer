/* $Id$
 *
 * Copyright (c) 2002  Daniel Elstner  <daniel.elstner@gmx.net>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License VERSION 2 as
 * published by the Free Software Foundation.  You are not allowed to
 * use any other version of the license; unless you got the explicit
 * permission from the author to do so.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */

#include "controller.h"
#include "imagebutton.h"
#include "translation.h"

#include <gtkmm/box.h>
#include <gtkmm/menu.h>
#include <gtkmm/menubar.h>
#include <gtkmm/stock.h>
#include <gtkmm/toolbar.h>
#include <memory>

#include <config.h>


namespace
{

void add_menu_stock(Gtk::MenuBar::MenuList& items, const Gtk::StockID& stock_id,
                    Regexxer::ControlItem& control)
{
  items.push_back(Gtk::Menu_Helpers::StockMenuElem(stock_id, control.slot()));
  control.add_widget(items.back());
}

void add_menu_stock(Gtk::MenuBar::MenuList& items, const Gtk::StockID& stock_id,
                    const Gtk::Menu::AccelKey& accel_key, Regexxer::ControlItem& control)
{
  items.push_back(Gtk::Menu_Helpers::StockMenuElem(stock_id, accel_key, control.slot()));
  control.add_widget(items.back());
}

void add_menu_image(Gtk::MenuBar::MenuList& items, const Gtk::StockID& stock_id,
                    const Glib::ustring& label, Regexxer::ControlItem& control)
{
  items.push_back(Gtk::Menu_Helpers::ImageMenuElem(
      label, *Gtk::manage(new Gtk::Image(stock_id, Gtk::ICON_SIZE_MENU)), control.slot()));
  control.add_widget(items.back());
}

void add_menu_image(Gtk::MenuBar::MenuList& items, const Gtk::StockID& stock_id,
                    const Glib::ustring& label, const Gtk::Menu::AccelKey& accel_key,
                    Regexxer::ControlItem& control)
{
  items.push_back(Gtk::Menu_Helpers::ImageMenuElem(
      label, accel_key, *Gtk::manage(new Gtk::Image(stock_id, Gtk::ICON_SIZE_MENU)), control.slot()));
  control.add_widget(items.back());
}

void add_tool_stock(Gtk::Toolbar::ToolList& tools, const Gtk::StockID& stock_id,
                    Regexxer::ControlItem& control)
{
  tools.push_back(Gtk::Toolbar_Helpers::StockElem(stock_id, control.slot()));
  control.add_widget(*tools.back().get_widget());
}

void add_widget_button(Gtk::Button& button, Regexxer::ControlItem& control)
{
  button.signal_clicked().connect(control.slot());
  control.add_widget(button);
}

} // anonymous namespace


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

SigC::Slot0<void> ControlItem::slot()
{
  return SigC::slot(*this, &ControlItem::activate);
}

void ControlItem::connect(const SigC::Slot0<void>& slot_activated)
{
  signal_activate_.connect(slot_activated);
}

void ControlItem::add_widget(Gtk::Widget& widget)
{
  signal_set_sensitive_.connect(SigC::slot(widget, &Gtk::Widget::set_sensitive));
  widget.set_sensitive(enabled_ && group_enabled_);
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
  signal_set_enabled_.connect(SigC::slot(control, &ControlItem::set_group_enabled));
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
  save_file     (false),
  save_all      (false),
  undo          (false),
  preferences   (true),
  quit          (true),
  info          (true),
  find_files    (false),
  find_matches  (false),
  next_file     (false),
  prev_file     (false),
  next_match    (false),
  prev_match    (false),
  replace       (false),
  replace_file  (false),
  replace_all   (false)
{
  match_actions.add(undo);
  match_actions.add(find_files);
  match_actions.add(find_matches);
  match_actions.add(next_file);
  match_actions.add(prev_file);
  match_actions.add(next_match);
  match_actions.add(prev_match);
  match_actions.add(replace);
  match_actions.add(replace_file);
  match_actions.add(replace_all);
}

Controller::~Controller()
{}

Gtk::MenuBar* Controller::create_menubar()
{
  using namespace Gtk;
  using namespace Gtk::Menu_Helpers;

  std::auto_ptr<MenuBar> menubar (new MenuBar());
  MenuList& menubar_items = menubar->items();

  {
    Menu *const menu = new Menu();
    menubar_items.push_back(MenuElem(_("Menu|_File"), *manage(menu)));
    MenuList& items = menu->items();

    items.push_back(TearoffMenuElem());

    add_menu_stock(items, Stock::SAVE, save_file);
    add_menu_stock(items, StockID("regexxer-save-all"), save_all);

    items.push_back(SeparatorElem());

    add_menu_stock(items, Stock::UNDO, undo);

    items.push_back(SeparatorElem());

    add_menu_stock(items, Stock::PREFERENCES, preferences);

    items.push_back(SeparatorElem());

    add_menu_stock(items, Stock::QUIT, quit);
  }

  {
    Menu *const menu = new Menu();
    menubar_items.push_back(MenuElem(_("Menu|_Match"), *manage(menu)));
    MenuList& items = menu->items();

    items.push_back(TearoffMenuElem());

    add_menu_image(items, Stock::GOTO_FIRST, _("Menu|Match|_Previous file"),
                                             AccelKey("<control>p"), prev_file);
    add_menu_stock(items, Stock::GO_BACK,    AccelKey("<control>b"), prev_match);
    add_menu_stock(items, Stock::GO_FORWARD, AccelKey("<control>n"), next_match);
    add_menu_image(items, Stock::GOTO_LAST,  _("Menu|Match|_Next file"),
                                             AccelKey("<control>e"), next_file);
    items.push_back(SeparatorElem());

    add_menu_image(items, Stock::CONVERT, _("Menu|Match|Replace _current"),
                                          AccelKey("<control>r"), replace);
    add_menu_image(items, Stock::CONVERT, _("Menu|Match|Replace in _this file"), replace_file);
    add_menu_image(items, Stock::CONVERT, _("Menu|Match|Replace in _all files"), replace_all);
  }

  {
    Menu *const menu = new Menu();
    menubar_items.push_back(MenuElem(_("Menu|_Help"), *manage(menu)));
    MenuList& items = menu->items();

    items.push_back(TearoffMenuElem());

    add_menu_stock(items, StockID("regexxer-info"), info);
  }

  return menubar.release();
}

Gtk::Toolbar* Controller::create_toolbar()
{
  using namespace Gtk;
  using namespace Gtk::Toolbar_Helpers;

  std::auto_ptr<Toolbar> toolbar (new Toolbar());
  ToolList& tools = toolbar->tools();

  add_tool_stock(tools, Stock::SAVE, save_file);
  add_tool_stock(tools, StockID("regexxer-save-all"), save_all);

  tools.push_back(Space());
  add_tool_stock(tools, Stock::UNDO, undo);

  tools.push_back(Space());
  add_tool_stock(tools, Stock::PREFERENCES, preferences);

  tools.push_back(Space());
  add_tool_stock(tools, Stock::QUIT, quit);

  return toolbar.release();
}

Gtk::Widget* Controller::create_action_area()
{
  using namespace Gtk;

  std::auto_ptr<Box> action_area (new HBox(false, 10));
  action_area->set_border_width(2);

  Box *const box_replace = new HBox(true, 6 /* HIG */);
  action_area->pack_end(*manage(box_replace), PACK_SHRINK);

  Box *const box_move = new HBox(true, 6 /* HIG */);
  action_area->pack_end(*manage(box_move), PACK_SHRINK);

  Button *const button_prev_file = new ImageButton(Stock::GOTO_FIRST, _("Action|File backward"));
  box_move->pack_start(*manage(button_prev_file));

  Button *const button_prev = new ImageButton(Stock::GO_BACK, _("Action|Backward"));
  box_move->pack_start(*manage(button_prev));

  Button *const button_next = new ImageButton(Stock::GO_FORWARD, _("Action|Forward"));
  box_move->pack_start(*manage(button_next));

  Button *const button_next_file = new ImageButton(Stock::GOTO_LAST, _("Action|File forward"));
  box_move->pack_start(*manage(button_next_file));

  Button *const button_replace = new ImageLabelButton(Stock::CONVERT, _("Action|_Replace"), true);
  box_replace->pack_start(*manage(button_replace));

  Button *const button_replace_file = new ImageLabelButton(Stock::CONVERT, _("Action|_This file"), true);
  box_replace->pack_start(*manage(button_replace_file));

  Button *const button_replace_all = new ImageLabelButton(Stock::CONVERT, _("Action|_All files"), true);
  box_replace->pack_start(*manage(button_replace_all));

  button_prev_file   ->get_accessible()->set_description(_("Go to the previous matching file"));
  button_prev        ->get_accessible()->set_description(_("Go to previous match"));
  button_next        ->get_accessible()->set_description(_("Go to next match"));
  button_next_file   ->get_accessible()->set_description(_("Go to the next matching file"));
  button_replace     ->get_accessible()->set_description(_("Replace current match"));
  button_replace_file->get_accessible()->set_description(_("Replace all matches in the current file"));
  button_replace_all ->get_accessible()->set_description(_("Replace all matches in all files"));

  add_widget_button(*button_next_file,    next_file);
  add_widget_button(*button_prev_file,    prev_file);
  add_widget_button(*button_next,         next_match);
  add_widget_button(*button_prev,         prev_match);
  add_widget_button(*button_replace,      replace);
  add_widget_button(*button_replace_file, replace_file);
  add_widget_button(*button_replace_all,  replace_all);

  return action_area.release();
}

} // namespace Regexxer

