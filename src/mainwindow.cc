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

#include "mainwindow.h"
#include "filetree.h"
#include "globalstrings.h"
#include "prefdialog.h"
#include "statusline.h"
#include "stringutils.h"
#include "translation.h"
#include "settings.h"

#include <glib.h>
#include <gtkmm.h>
#include <gtksourceviewmm.h>
#include <algorithm>
#include <functional>
#include <iostream>

#include <config.h>

namespace
{

enum { BUSY_GUI_UPDATE_INTERVAL = 16 };

typedef Glib::RefPtr<Regexxer::FileBuffer> FileBufferPtr;

static const char *const selection_clipboard = "CLIPBOARD";

/*
 * List of authors to be displayed in the about dialog.
 */
static const char *const program_authors[] =
{
  "Daniel Elstner <daniel.kitta@gmail.com>",
  "Fabien Parent <parent.f@gmail.com>",
  "Murray Cumming <murrayc@murrayc.com>",
  0
};

static const char *const program_license =
  "regexxer is free software; you can redistribute it and/or modify "
  "it under the terms of the GNU General Public License as published by "
  "the Free Software Foundation; either version 2 of the License, or "
  "(at your option) any later version.\n"
  "\n"
  "regexxer is distributed in the hope that it will be useful, "
  "but WITHOUT ANY WARRANTY; without even the implied warranty of "
  "MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the "
  "GNU General Public License for more details.\n"
  "\n"
  "You should have received a copy of the GNU General Public License "
  "along with regexxer; if not, write to the Free Software Foundation, "
  "Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA\n";

class FileErrorDialog : public Gtk::MessageDialog
{
public:
  FileErrorDialog(Gtk::Window& parent, const Glib::ustring& message,
                  Gtk::MessageType type, const Regexxer::FileTree::Error& error);
  virtual ~FileErrorDialog();
};

FileErrorDialog::FileErrorDialog(Gtk::Window& parent, const Glib::ustring& message,
                                 Gtk::MessageType type, const Regexxer::FileTree::Error& error)
:
  Gtk::MessageDialog(parent, message, false, type, Gtk::BUTTONS_OK, true)
{
  using namespace Gtk;

  const Glib::RefPtr<TextBuffer> buffer = TextBuffer::create();
  TextBuffer::iterator buffer_end = buffer->end();

  typedef std::list<Glib::ustring> ErrorList;
  const ErrorList& error_list = error.get_error_list();

  for (ErrorList::const_iterator perr = error_list.begin(); perr != error_list.end(); ++perr)
    buffer_end = buffer->insert(buffer_end, *perr + '\n');

  Box& box = *get_vbox();
  Frame *const frame = new Frame();
  box.pack_start(*manage(frame), PACK_EXPAND_WIDGET);
  frame->set_border_width(6); // HIG spacing
  frame->set_shadow_type(SHADOW_IN);

  ScrolledWindow *const scrollwin = new ScrolledWindow();
  frame->add(*manage(scrollwin));
  scrollwin->set_policy(POLICY_AUTOMATIC, POLICY_AUTOMATIC);

  TextView *const textview = new TextView(buffer);
  scrollwin->add(*manage(textview));
  textview->set_editable(false);
  textview->set_cursor_visible(false);
  textview->set_wrap_mode(WRAP_WORD);
  textview->set_pixels_below_lines(8);

  set_default_size(400, 250);
  frame->show_all();
}

FileErrorDialog::~FileErrorDialog()
{}

static
void print_location(int linenumber, const Glib::ustring& subject, Regexxer::FileInfoPtr fileinfo)
{
  std::cout << fileinfo->fullname << ':' << linenumber + 1 << ':';

  std::string charset;

  if (Glib::get_charset(charset))
    std::cout << subject.raw(); // charset is UTF-8
  else
    std::cout << Glib::convert_with_fallback(subject.raw(), charset, "UTF-8");

  std::cout << std::endl;
}

} // anonymous namespace

namespace Regexxer
{

/**** Regexxer::InitState **************************************************/

InitState::InitState()
:
  folder        (),
  pattern       (),
  regex         (),
  substitution  (),
  no_recursive  (false),
  hidden        (false),
  no_global     (false),
  ignorecase    (false),
  feedback      (false),
  no_autorun    (false)
{}

InitState::~InitState()
{}

/**** Regexxer::MainWindow::BusyAction *************************************/

class MainWindow::BusyAction
{
private:
  MainWindow& object_;

  BusyAction(const BusyAction&);
  BusyAction& operator=(const BusyAction&);

public:
  explicit BusyAction(MainWindow& object)
    : object_ (object) { object_.busy_action_enter(); }

  ~BusyAction() { object_.busy_action_leave(); }
};

/**** Regexxer::MainWindow *************************************************/

MainWindow::MainWindow()
:
  headerbar_              (0),
  button_gear_            (0),
  grid_file_              (0),
  button_folder_          (0),
  combo_entry_pattern_    (Gtk::manage(new Gtk::ComboBoxText(true))),
  button_recursive_       (0),
  button_hidden_          (0),
  entry_regex_            (0),
  entry_regex_completion_stack_(10, Settings::instance()->get_string_array(conf_key_regex_patterns)),
  entry_regex_completion_ (Gtk::EntryCompletion::create()),
  entry_substitution_     (0),
  entry_substitution_completion_stack_(10, Settings::instance()->get_string_array(conf_key_substitution_patterns)),
  entry_substitution_completion_ (Gtk::EntryCompletion::create()),
  button_multiple_        (0),
  button_caseless_        (0),
  filetree_               (Gtk::manage(new FileTree())),
  scrollwin_filetree_     (0),
  scrollwin_textview_     (0),
  textview_               (Gtk::manage(new Gsv::View())),
  entry_preview_          (0),
  statusline_             (Gtk::manage(new StatusLine())),
  busy_action_running_    (false),
  busy_action_cancel_     (false),
  busy_action_iteration_  (0),
  undo_stack_             (new UndoStack()),
  match_action_group_     (Gio::SimpleActionGroup::create()),
  edit_action_group_      (Gio::SimpleActionGroup::create()),
  save_action_group_      (Gio::SimpleActionGroup::create())
{
  load_xml();

  entry_regex_ = comboboxentry_regex_->get_entry();
  entry_substitution_ = comboboxentry_substitution_->get_entry();

  textview_->set_buffer(FileBuffer::create());
  window_->set_title(PACKAGE_NAME);
  headerbar_->set_title(PACKAGE_NAME);
  headerbar_->set_show_close_button(true);

  vbox_main_->pack_start(*statusline_, Gtk::PACK_SHRINK);
  scrollwin_filetree_->add(*filetree_);
  grid_file_->attach(*combo_entry_pattern_, 1, 1, 1, 1);

  scrollwin_textview_->add(*textview_);

  headerbar_->show_all();
  statusline_->show_all();
  filetree_->show_all();
  combo_entry_pattern_->show_all();
  scrollwin_textview_->show_all();

  connect_signals();
}

MainWindow::~MainWindow()
{}

void MainWindow::initialize(const Glib::RefPtr<Gtk::Application>& application,
                            const InitState& init)
{
  Glib::RefPtr<Gio::Settings> settings = Settings::instance();
  int width = settings->get_int(conf_key_window_width);
  int height = settings->get_int(conf_key_window_height);

  int x = settings->get_int(conf_key_window_position_x);
  int y = settings->get_int(conf_key_window_position_y);

  bool maximized = settings->get_boolean(conf_key_window_maximized);

  application_ = application;
  application->signal_startup().connect
      (sigc::mem_fun(*this, &MainWindow::on_startup));
  application->register_application();

  window_->set_application(application_);
  window_->resize(width, height);
  window_->move(x, y);
  if (maximized)
    window_->maximize();

  init_actions();
  set_sensitive(match_action_group_, false);
  set_sensitive(edit_action_group_, false);
  set_sensitive(save_action_group_, true);
  set_sensitive("save", false);
  set_sensitive("save-all", false);

  Glib::RefPtr<Gtk::Builder> builder =
      Gtk::Builder::create_from_file(ui_gearmenu_filename);
  Glib::RefPtr<Gio::MenuModel> gearmenu =
    Glib::RefPtr<Gio::MenuModel>::cast_static(builder->get_object("gearmenu"));
  button_gear_->set_menu_model(gearmenu);

  textview_->set_show_line_numbers(settings->get_boolean(conf_key_show_line_numbers));
  textview_->set_highlight_current_line(settings->get_boolean(conf_key_highlight_current_line));
  textview_->set_auto_indent(settings->get_boolean(conf_key_auto_indentation));
  textview_->set_draw_spaces(static_cast<Gsv::DrawSpacesFlags>
                              (settings->get_flags(conf_key_draw_spaces)));

  std::string folder;

  if (!init.folder.empty())
    folder = init.folder.front();
  if (!Glib::path_is_absolute(folder))
    folder = Glib::build_filename(Glib::get_current_dir(), folder);

  const bool folder_exists = button_folder_->set_current_folder(folder);

  combo_entry_pattern_->get_entry()->set_text((init.pattern.empty()) ? Glib::ustring(1, '*') : init.pattern);

  entry_regex_->set_text(init.regex);
  entry_regex_->set_completion(entry_regex_completion_);
  entry_substitution_->set_text(init.substitution);
  entry_substitution_->set_completion(entry_substitution_completion_);

  comboboxentry_regex_->set_model(entry_regex_completion_stack_.get_completion_model());
  comboboxentry_regex_->set_entry_text_column(entry_regex_completion_stack_.get_completion_column());
  comboboxentry_substitution_->set_model(entry_substitution_completion_stack_.get_completion_model());
  comboboxentry_substitution_->set_entry_text_column(entry_substitution_completion_stack_.get_completion_column());

  entry_regex_completion_->set_model(entry_regex_completion_stack_.get_completion_model());
  entry_regex_completion_->set_text_column(entry_regex_completion_stack_.get_completion_column());
  entry_regex_completion_->set_inline_completion(true);
  entry_regex_completion_->set_popup_completion(false);

  entry_substitution_completion_->set_model(entry_substitution_completion_stack_.get_completion_model());
  entry_substitution_completion_->set_text_column(entry_substitution_completion_stack_.get_completion_column());
  entry_substitution_completion_->set_inline_completion(true);
  entry_substitution_completion_->set_popup_completion(false);

  button_recursive_->set_active(!init.no_recursive);
  button_hidden_   ->set_active(init.hidden);
  button_multiple_ ->set_active(!init.no_global);
  button_caseless_ ->set_active(init.ignorecase);

  combo_entry_pattern_->set_entry_text_column(0);
  const std::list<Glib::ustring> patterns =
      settings->get_string_array(conf_key_files_patterns);
  for (std::list<Glib::ustring>::const_iterator pattern = patterns.begin();
       pattern != patterns.end();
       ++pattern)
  {
    combo_entry_pattern_->append(*pattern);
  }

  if (init.feedback)
    filetree_->signal_feedback.connect(&print_location);

  // Strangely, folder_exists seems to be always true, probably because the
  // file chooser works asynchronously but the GLib main loop isn't running
  // yet.  As a work-around, explicitely check whether the directory exists
  // on the file system as well.
  if (folder_exists && !init.no_autorun
      && !init.folder.empty() && !init.pattern.empty()
      && Glib::file_test(folder, Glib::FILE_TEST_IS_DIR))
  {
    Glib::signal_idle().connect(sigc::mem_fun(*this, &MainWindow::autorun_idle));
  }
}

/**** Regexxer::MainWindow -- private **************************************/

void MainWindow::set_sensitive(const Glib::RefPtr<Gio::SimpleActionGroup>& group,
                               bool sensitive)
{
  std::vector<Glib::ustring> actions = group->list_actions();
  for (std::vector<Glib::ustring>::iterator i = actions.begin();
       i != actions.end();
       ++i)
  {
    Glib::RefPtr<Gio::SimpleAction> action =
        Glib::RefPtr<Gio::SimpleAction>::cast_static(group->lookup_action(*i));
    if (!action_enabled_.count(*i))
        action_enabled_[*i] = true;
    action->set_enabled(action_enabled_[*i] && sensitive);
  }

  action_group_enabled_[group] = sensitive;
}

void MainWindow::set_sensitive(const Glib::ustring& action_name, bool sensitive)
{
  Glib::RefPtr<Gio::SimpleActionGroup> group = action_to_group_[action_name];
  Glib::RefPtr<Gio::SimpleAction> action =
      Glib::RefPtr<Gio::SimpleAction>::cast_static
          (group->lookup_action(action_name));

  action->set_enabled(action_group_enabled_[group] && sensitive);
  action_enabled_[action_name] = sensitive;
}

void MainWindow::on_startup()
{
  static struct
  {
    const char* const name;
    sigc::slot<void> slot;
  } appmenu_actions[] =
  {
    {"preferences", sigc::mem_fun(*this, &MainWindow::on_preferences)},
    {"about", sigc::mem_fun(*this, &MainWindow::on_about)},
    {"quit", sigc::mem_fun(*this, &MainWindow::on_quit)},
    {0},
  };

  Glib::RefPtr<Gtk::Builder> builder =
      Gtk::Builder::create_from_file(ui_appmenu_filename);

  for (int i = 0; appmenu_actions[i].name; i++)
  {
    Glib::RefPtr<Gio::SimpleAction> action =
        Gio::SimpleAction::create(appmenu_actions[i].name);
    application_->add_action(action);
    action->signal_activate().connect(sigc::hide(appmenu_actions[i].slot));
  }

  Glib::RefPtr<Gio::MenuModel> appmenu =
      Glib::RefPtr<Gio::MenuModel>::cast_static(builder->get_object("appmenu"));
  application_->set_app_menu(appmenu);
}

void MainWindow::init_actions()
{
  struct actions
  {
    const char *const name;
    const char* accelerator;
    sigc::slot<void> slot;
  };

  static struct actions match_actions[] =
  {
    {"undo", "<Ctrl>Z", sigc::mem_fun(*this, &MainWindow::on_undo)},
    {"previous-file", "<Ctrl>P",sigc::bind(sigc::mem_fun(*this, &MainWindow::on_go_next_file), false)},
    {"back", "<Ctrl>B", sigc::bind(sigc::mem_fun(*this, &MainWindow::on_go_next), false)},
    {"forward", "<Ctrl>N", sigc::bind(sigc::mem_fun(*this, &MainWindow::on_go_next), true)},
    {"next-file", "<Ctrl>E", sigc::bind(sigc::mem_fun(*this, &MainWindow::on_go_next_file), true)},
    {"replace-current", "<Ctrl>R", sigc::mem_fun(*this, &MainWindow::on_replace)},
    {"replace-in-file", NULL, sigc::mem_fun(*this, &MainWindow::on_replace_file)},
    {"replace-in-all-files", NULL, sigc::mem_fun(*this, &MainWindow::on_replace_all)},
    {0},
  };

  for (int i = 0; match_actions[i].name; i++)
  {
    Glib::RefPtr<Gio::SimpleAction> action =
        Gio::SimpleAction::create(match_actions[i].name);
    action->signal_activate().connect(sigc::hide(match_actions[i].slot));
    match_action_group_->insert(action);
    action_to_group_[match_actions[i].name] = match_action_group_;
    if (match_actions[i].accelerator)
        application_->add_accelerator(match_actions[i].accelerator,
                                      Glib::ustring("match.") + match_actions[i].name);
  }

  static struct actions edit_actions[] =
  {
    {"cut", "<Ctrl>X", sigc::mem_fun(*this, &MainWindow::on_cut)},
    {"copy", "<Ctrl>C", sigc::mem_fun(*this, &MainWindow::on_copy)},
    {"paste", "<Ctrl>V", sigc::mem_fun(*this, &MainWindow::on_paste)},
    {"delete", NULL, sigc::mem_fun(*this, &MainWindow::on_erase)},
    {0},
  };

  for (int i = 0; edit_actions[i].name; i++)
  {
    Glib::RefPtr<Gio::SimpleAction> action =
        Gio::SimpleAction::create(edit_actions[i].name);
    action->signal_activate().connect(sigc::hide(edit_actions[i].slot));
    edit_action_group_->insert(action);
    action_to_group_[edit_actions[i].name] = edit_action_group_;
    if (edit_actions[i].accelerator)
        application_->add_accelerator(edit_actions[i].accelerator,
                                      Glib::ustring("edit.") + edit_actions[i].name);
  }

  static struct actions save_actions[] =
  {
    {"save", "<Ctrl>S", sigc::mem_fun(*this, &MainWindow::on_save_file)},
    {"save-all", NULL, sigc::mem_fun(*this, &MainWindow::on_save_all)},
    {0},
  };

  for (int i = 0; save_actions[i].name; i++)
  {
    Glib::RefPtr<Gio::SimpleAction> action =
        Gio::SimpleAction::create(save_actions[i].name);
    action->signal_activate().connect(sigc::hide(save_actions[i].slot));
    save_action_group_->insert(action);
    action_to_group_[save_actions[i].name] = save_action_group_;
    if (save_actions[i].accelerator)
        application_->add_accelerator(save_actions[i].accelerator,
                                      Glib::ustring("save.") + save_actions[i].name);
  }

  window_->insert_action_group("match", match_action_group_);
  window_->insert_action_group("edit", edit_action_group_);
  window_->insert_action_group("save", save_action_group_);

//  controller_.find_files  .connect(mem_fun(*this, &MainWindow::on_find_files));
//  controller_.find_matches.connect(mem_fun(*this, &MainWindow::on_exec_search));
}

void MainWindow::load_xml()
{
  const Glib::RefPtr<Gtk::Builder> xml = Gtk::Builder::create_from_file(ui_mainwindow_filename);

  Gtk::ApplicationWindow* mainwindow = 0;
  xml->get_widget("mainwindow", mainwindow);
  window_.reset(mainwindow);

  xml->get_widget("headerbar",           headerbar_);
  xml->get_widget("button_gear",         button_gear_);
  xml->get_widget("button_folder",       button_folder_);
  xml->get_widget("button_recursive",    button_recursive_);
  xml->get_widget("button_hidden",       button_hidden_);
  xml->get_widget("comboboxentry_regex",         comboboxentry_regex_);
  xml->get_widget("comboboxentry_substitution",  comboboxentry_substitution_);
  xml->get_widget("button_multiple",     button_multiple_);
  xml->get_widget("button_caseless",     button_caseless_);
  xml->get_widget("scrollwin_textview",  scrollwin_textview_);
  xml->get_widget("entry_preview",       entry_preview_);
  xml->get_widget("vbox_main",           vbox_main_);
  xml->get_widget("scrollwin_filetree",  scrollwin_filetree_);
  xml->get_widget("grid_file",           grid_file_);

  controller_.load_xml(xml);
}

void MainWindow::connect_signals()
{
  using sigc::bind;
  using sigc::mem_fun;

  window_->signal_hide         ().connect(mem_fun(*this, &MainWindow::on_hide));
  window_->signal_style_updated().connect(mem_fun(*this, &MainWindow::on_style_updated));
  window_->signal_delete_event ().connect(mem_fun(*this, &MainWindow::on_delete_event));

  combo_entry_pattern_->get_entry()->signal_activate().connect(controller_.find_files.slot());
  combo_entry_pattern_->signal_changed ().connect(mem_fun(*this, &MainWindow::on_entry_pattern_changed));

  entry_regex_       ->signal_activate().connect(controller_.find_matches.slot());
  entry_substitution_->signal_activate().connect(controller_.find_matches.slot());
  entry_substitution_->signal_changed ().connect(mem_fun(*this, &MainWindow::update_preview));

  controller_.find_files  .connect(mem_fun(*this, &MainWindow::on_find_files));
  controller_.find_matches.connect(mem_fun(*this, &MainWindow::on_exec_search));

  Settings::instance()->signal_changed().connect(mem_fun(*this, &MainWindow::on_conf_value_changed));

  statusline_->signal_cancel_clicked.connect(
      mem_fun(*this, &MainWindow::on_busy_action_cancel));

  filetree_->signal_switch_buffer.connect(
      mem_fun(*this, &MainWindow::on_filetree_switch_buffer));

  filetree_->signal_bound_state_changed.connect(
      mem_fun(*this, &MainWindow::on_bound_state_changed));

  filetree_->signal_file_count_changed.connect(
      mem_fun(*this, &MainWindow::on_filetree_file_count_changed));

  filetree_->signal_match_count_changed.connect(
      mem_fun(*this, &MainWindow::on_filetree_match_count_changed));

  filetree_->signal_modified_count_changed.connect(
      mem_fun(*this, &MainWindow::on_filetree_modified_count_changed));

  filetree_->signal_pulse.connect(
      mem_fun(*this, &MainWindow::on_busy_action_pulse));

  filetree_->signal_undo_stack_push.connect(
      mem_fun(*this, &MainWindow::on_undo_stack_push));
}

bool MainWindow::autorun_idle()
{
  controller_.find_files.activate();

  if (!busy_action_cancel_ && entry_regex_->get_text_length() > 0)
    controller_.find_matches.activate();

  return false;
}

void MainWindow::on_hide()
{
  on_busy_action_cancel();

  // Kill the dialogs if they're mapped right now.  This isn't strictly
  // necessary since they'd be deleted in the destructor anyway.  But if we
  // have to do a lot of cleanup the dialogs would stay open for that time,
  // which doesn't look neat.
  about_dialog_.reset();
  pref_dialog_.reset();
}

void MainWindow::on_style_updated()
{
  FileBuffer::pango_context_changed(window_->get_pango_context());
}

bool MainWindow::on_delete_event(GdkEventAny*)
{
  bool quit = confirm_quit_request();
  save_window_state();
  return !quit;
}

void MainWindow::on_cut()
{
  if (textview_->is_focus())
  {
    if (const Glib::RefPtr<Gtk::TextBuffer> buffer = textview_->get_buffer())
      buffer->cut_clipboard(textview_->get_clipboard(selection_clipboard),
                            textview_->get_editable());
  }
  else
  {
    const int noEntries = 3;
    Gtk::Entry *entries[noEntries] = { combo_entry_pattern_->get_entry(), entry_regex_,
                                       entry_substitution_ };
    for (int i = 0; i < 3; i++)
    {
      if (entries[i]->is_focus())
      {
        ((Gtk::Editable *)entries[i])->cut_clipboard();
        return ;
      }
    }
  }
}

void MainWindow::on_copy()
{
  if (textview_->is_focus())
  {
    if (const Glib::RefPtr<Gtk::TextBuffer> buffer = textview_->get_buffer())
      buffer->copy_clipboard(textview_->get_clipboard(selection_clipboard));
  }
  else
  {
    const int noEntries = 3;
    Gtk::Entry *entries[noEntries] = { combo_entry_pattern_->get_entry(), entry_regex_,
                                       entry_substitution_ };
    for (int i = 0; i < 3; i++)
    {
      if (entries[i]->is_focus())
      {
        ((Gtk::Editable *)entries[i])->copy_clipboard();
        return ;
      }
    }
  }
}

void MainWindow::on_paste()
{
  if (textview_->is_focus())
  {
    if (const Glib::RefPtr<Gtk::TextBuffer> buffer = textview_->get_buffer())
      buffer->paste_clipboard(textview_->get_clipboard(selection_clipboard),
                              textview_->get_editable());
  }
  else
  {
    const int noEntries = 3;
    Gtk::Entry *entries[noEntries] = { combo_entry_pattern_->get_entry(), entry_regex_,
                                       entry_substitution_ };
    for (int i = 0; i < 3; i++)
    {
      if (entries[i]->is_focus())
      {
        ((Gtk::Editable *)entries[i])->paste_clipboard();
        return ;
      }
    }
  }
}

void MainWindow::on_erase()
{
  if (const Glib::RefPtr<Gtk::TextBuffer> buffer = textview_->get_buffer())
    buffer->erase_selection(true, textview_->get_editable());
}

void MainWindow::on_quit()
{
  if (confirm_quit_request())
  {
    save_window_state();
    window_->hide();
  }
}

void MainWindow::save_window_state()
{
  int x = 0;
  int y = 0;

  int width = 0;
  int height = 0;

  window_->get_position(x, y);
  window_->get_size(width, height);
  bool maximized = (window_->get_window()->get_state() & Gdk::WINDOW_STATE_MAXIMIZED);

  Glib::RefPtr<Gio::Settings> settings = Settings::instance();

  settings->set_int(conf_key_window_position_x, x);
  settings->set_int(conf_key_window_position_y, y);
  settings->set_boolean(conf_key_window_maximized, maximized);

  if (!maximized)
  {
    settings->set_int(conf_key_window_width, width);
    settings->set_int(conf_key_window_height, height);
  }
}

bool MainWindow::confirm_quit_request()
{
  if (filetree_->get_modified_count() == 0)
    return true;

  Gtk::MessageDialog dialog (*window_,
                             _("Some files haven\342\200\231t been saved yet.\nQuit anyway?"),
                             false, Gtk::MESSAGE_WARNING, Gtk::BUTTONS_NONE, true);

  dialog.add_button(Gtk::Stock::CANCEL, Gtk::RESPONSE_CANCEL);
  dialog.add_button(Gtk::Stock::QUIT,   Gtk::RESPONSE_OK);

  return (dialog.run() == Gtk::RESPONSE_OK);
}

void MainWindow::on_find_files()
{
  if (filetree_->get_modified_count() > 0)
  {
    Gtk::MessageDialog dialog (*window_,
                               _("Some files haven\342\200\231t been saved yet.\nContinue anyway?"),
                               false, Gtk::MESSAGE_WARNING, Gtk::BUTTONS_OK_CANCEL, true);

    if (dialog.run() != Gtk::RESPONSE_OK)
      return;
  }

  std::string folder = button_folder_->get_filename();

  if (folder.empty())
    folder = Glib::get_current_dir();

  g_return_if_fail(Glib::path_is_absolute(folder));

  undo_stack_clear();

  BusyAction busy (*this);

  try
  {
    Glib::RefPtr<Glib::Regex> pattern = Glib::Regex::create(
        Util::shell_pattern_to_regex(combo_entry_pattern_->get_entry()->get_text()),
        Glib::REGEX_DOTALL);

    filetree_->find_files(folder, pattern,
                          button_recursive_->get_active(),
                          button_hidden_->get_active());
  }
  catch (const Glib::RegexError&)
  {
    Gtk::MessageDialog dialog (*window_, _("The file search pattern is invalid."),
                               false, Gtk::MESSAGE_ERROR, Gtk::BUTTONS_OK, true);
    dialog.run();
  }
  catch (const FileTree::Error& error)
  {
    FileErrorDialog dialog (*window_, _("The following errors occurred during search:"),
                            Gtk::MESSAGE_WARNING, error);
    dialog.run();
  }

  statusline_->set_file_count(filetree_->get_file_count());
}

void MainWindow::on_exec_search()
{
  BusyAction busy (*this);

  const Glib::ustring regex = entry_regex_->get_text();
  const bool caseless = button_caseless_->get_active();
  const bool multiple = button_multiple_->get_active();

  entry_regex_completion_stack_.push(regex);

  Settings::instance()->set_string_array(conf_key_regex_patterns, entry_regex_completion_stack_.get_stack());

  try
  {
    Glib::RefPtr<Glib::Regex> pattern =
        Glib::Regex::create(regex, (caseless) ? Glib::REGEX_CASELESS
                                              : static_cast<Glib::RegexCompileFlags>(0));

    filetree_->find_matches(pattern, multiple);
  }
  catch (const Glib::RegexError& error)
  {
    Gtk::MessageDialog dialog (*window_, error.what(), false,
                               Gtk::MESSAGE_ERROR, Gtk::BUTTONS_OK, true);
    dialog.run();

    return;
  }

  if (const FileBufferPtr buffer = FileBufferPtr::cast_static(textview_->get_buffer()))
  {
    statusline_->set_match_count(buffer->get_original_match_count());
    statusline_->set_match_index(buffer->get_match_index());
  }

  if (filetree_->get_match_count() > 0)
  {
    // Scrolling has to be post-poned after the redraw, otherwise we might
    // not end up where we want to.  So do that by installing an idle handler.

    Glib::signal_idle().connect(
        sigc::mem_fun(*this, &MainWindow::after_exec_search),
        Glib::PRIORITY_HIGH_IDLE + 25); // slightly less than redraw (+20)
  }
}

bool MainWindow::after_exec_search()
{
  filetree_->select_first_file();
  on_go_next(true);

  return false;
}

void MainWindow::on_filetree_switch_buffer(FileInfoPtr fileinfo, int file_index)
{
  const FileBufferPtr old_buffer = FileBufferPtr::cast_static(textview_->get_buffer());

  if (fileinfo && fileinfo->buffer == old_buffer)
    return;

  if (old_buffer)
  {
    std::for_each(buffer_connections_.begin(), buffer_connections_.end(),
                  std::mem_fun_ref(&sigc::connection::disconnect));

    buffer_connections_.clear();
    old_buffer->forget_current_match();
  }

  if (fileinfo)
  {
    const FileBufferPtr buffer = fileinfo->buffer;
    g_return_if_fail(buffer);

    textview_->set_buffer(buffer);
    textview_->set_editable(!fileinfo->load_failed);
    textview_->set_cursor_visible(!fileinfo->load_failed);

    if (!fileinfo->load_failed)
    {
      buffer_connections_.push_back(buffer->signal_modified_changed().
          connect(sigc::mem_fun(*this, &MainWindow::on_buffer_modified_changed)));

      buffer_connections_.push_back(buffer->signal_bound_state_changed.
          connect(sigc::mem_fun(*this, &MainWindow::on_bound_state_changed)));

      buffer_connections_.push_back(buffer->signal_preview_line_changed.
          connect(sigc::mem_fun(*this, &MainWindow::update_preview)));
    }

    set_title_filename(fileinfo->fullname);

    set_sensitive("replace-in-file", buffer->get_match_count() > 0);
    set_sensitive("save", buffer->get_modified());
    set_sensitive(edit_action_group_, !fileinfo->load_failed);

    statusline_->set_match_count(buffer->get_original_match_count());
    statusline_->set_match_index(buffer->get_match_index());
    statusline_->set_file_encoding(fileinfo->encoding);
  }
  else
  {
    textview_->set_buffer(FileBuffer::create());
    textview_->set_editable(false);
    textview_->set_cursor_visible(false);

    window_->set_title(PACKAGE_NAME);
    headerbar_->set_title(PACKAGE_NAME);
    headerbar_->set_subtitle("");

    set_sensitive("replace-in-file", false);
    set_sensitive("save", false);
    set_sensitive(edit_action_group_, false);

    statusline_->set_match_count(0);
    statusline_->set_match_index(0);
    statusline_->set_file_encoding("");
  }

  statusline_->set_file_index(file_index);
  update_preview();
}

void MainWindow::on_bound_state_changed()
{
  BoundState bound = filetree_->get_bound_state();

  set_sensitive("previous-file", (bound & BOUND_FIRST) == 0);
  set_sensitive("next-file", (bound & BOUND_LAST) == 0);

  if (const FileBufferPtr buffer = FileBufferPtr::cast_static(textview_->get_buffer()))
    bound &= buffer->get_bound_state();

  set_sensitive("back", (bound & BOUND_FIRST) == 0);
  set_sensitive("forward", (bound & BOUND_LAST) == 0);
}

void MainWindow::on_filetree_file_count_changed()
{
  const int file_count = filetree_->get_file_count();

  statusline_->set_file_count(file_count);
  controller_.find_matches.set_enabled(file_count > 0);
}

void MainWindow::on_filetree_match_count_changed()
{
  set_sensitive("replace-in-all-files", filetree_->get_match_count() > 0);

  if (const FileBufferPtr buffer = FileBufferPtr::cast_static(textview_->get_buffer()))
  {
    set_sensitive("replace-in-file", buffer->get_match_count() > 0);
    set_sensitive("replace-current", buffer->get_match_count() > 0);
  }
}

void MainWindow::on_filetree_modified_count_changed()
{
  set_sensitive("save-all", filetree_->get_modified_count() > 0);
}

void MainWindow::on_buffer_modified_changed()
{
  set_sensitive("save", textview_->get_buffer()->get_modified());
}

void MainWindow::on_go_next_file(bool move_forward)
{
  filetree_->select_next_file(move_forward);
  on_go_next(move_forward);
}

bool MainWindow::do_scroll(const Glib::RefPtr<Gtk::TextMark> mark)
{
  if (!mark)
    return false;

  textview_->scroll_to(mark, 0.125);
  return false;
}

void MainWindow::on_go_next(bool move_forward)
{
  if (const FileBufferPtr buffer = FileBufferPtr::cast_static(textview_->get_buffer()))
  {
    if (const Glib::RefPtr<Gtk::TextMark> mark = buffer->get_next_match(move_forward))
    {
      Glib::signal_idle ().connect (sigc::bind<const Glib::RefPtr<Gtk::TextMark> >
            (sigc::mem_fun (*this, &MainWindow::do_scroll), mark));
      statusline_->set_match_index(buffer->get_match_index());
      return;
    }
  }

  if (filetree_->select_next_file(move_forward))
  {
    on_go_next(move_forward); // recursive call
  }
}

void MainWindow::on_replace()
{
  if (const FileBufferPtr buffer = FileBufferPtr::cast_static(textview_->get_buffer()))
  {
    const Glib::ustring substitution = entry_substitution_->get_text();
    entry_substitution_completion_stack_.push(substitution);
    Settings::instance()->set_string_array(conf_key_substitution_patterns, entry_substitution_completion_stack_.get_stack());
    buffer->replace_current_match(substitution);
    on_go_next(true);
  }
}

void MainWindow::on_replace_file()
{
  if (const FileBufferPtr buffer = FileBufferPtr::cast_static(textview_->get_buffer()))
  {
    const Glib::ustring substitution = entry_substitution_->get_text();
    entry_substitution_completion_stack_.push(substitution);
    Settings::instance()->set_string_array(conf_key_substitution_patterns, entry_substitution_completion_stack_.get_stack());
    buffer->replace_all_matches(substitution);
    statusline_->set_match_index(0);
  }
}

void MainWindow::on_replace_all()
{
  BusyAction busy (*this);

  const Glib::ustring substitution = entry_substitution_->get_text();
  entry_substitution_completion_stack_.push(substitution);
  Settings::instance()->set_string_array(conf_key_substitution_patterns, entry_substitution_completion_stack_.get_stack());
  filetree_->replace_all_matches(substitution);
  statusline_->set_match_index(0);
}

void MainWindow::on_save_file()
{
  try
  {
    filetree_->save_current_file();
  }
  catch (const FileTree::Error& error)
  {
    const std::list<Glib::ustring>& error_list = error.get_error_list();
    g_assert(error_list.size() == 1);

    Gtk::MessageDialog dialog (*window_, error_list.front(), false,
                               Gtk::MESSAGE_ERROR, Gtk::BUTTONS_OK, true);
    dialog.run();
  }
}

void MainWindow::on_save_all()
{
  try
  {
    filetree_->save_all_files();
  }
  catch (const FileTree::Error& error)
  {
    FileErrorDialog dialog (*window_, _("The following errors occurred during save:"),
                            Gtk::MESSAGE_ERROR, error);
    dialog.run();
  }
}

void MainWindow::on_undo_stack_push(UndoActionPtr action)
{
  undo_stack_->push(action);
  set_sensitive("undo", true);
}

void MainWindow::on_undo()
{
  if (textview_->is_focus())
  {
    BusyAction busy (*this);
    undo_stack_->undo_step(sigc::mem_fun(*this, &MainWindow::on_busy_action_pulse));
    set_sensitive("undo", !undo_stack_->empty());
  }
}

void MainWindow::undo_stack_clear()
{
  set_sensitive("undo", false);
  undo_stack_.reset(new UndoStack());
}

void MainWindow::on_entry_pattern_changed()
{
  controller_.find_files.set_enabled(combo_entry_pattern_->get_entry()->get_text_length() > 0);
}

void MainWindow::update_preview()
{
  if (const FileBufferPtr buffer = FileBufferPtr::cast_static(textview_->get_buffer()))
  {
    Glib::ustring preview;
    const int pos = buffer->get_line_preview(entry_substitution_->get_text(), preview);

    entry_preview_->set_text(preview);
    set_sensitive("replace-current", pos >= 0);

    // Beware, strange code ahead!
    //
    // The goal is to scroll the preview entry so that it shows the entire
    // replaced text if possible.  In order to do that we first move the cursor
    // to 0, forcing scrolling to the left boundary.  Then we set the cursor to
    // the end of the replaced text, thus forcing the entry widget to scroll
    // again.  The replacement should then be entirely visible provided that it
    // fits into the entry.
    //
    // The problem is that Gtk::Entry doesn't update its scroll position
    // immediately but in an idle handler, thus any calls to set_position()
    // but the last one have no effect at all.
    //
    // To workaround that, we install an idle handler that's executed just
    // after the entry updated its scroll position, but before redrawing is
    // done.

    entry_preview_->set_position(0);

    if (pos > 0)
    {
      using namespace sigc;

      Glib::signal_idle().connect(
          bind_return(bind(mem_fun(*entry_preview_, &Gtk::Editable::set_position), pos), false),
          Glib::PRIORITY_HIGH_IDLE + 17); // between scroll update (+ 15) and redraw (+ 20)
    }
  }
}

void MainWindow::set_title_filename(const std::string& filename)
{
  Glib::ustring title = Glib::filename_display_basename(filename);

  title += " (";
  title += Util::filename_short_display_name(Glib::path_get_dirname(filename));
  title += ") \342\200\223 " PACKAGE_NAME; // U+2013 EN DASH

  window_->set_title(title);
  headerbar_->set_title(Glib::filename_display_basename(filename));
  headerbar_->set_subtitle(Util::filename_short_display_name
      (Glib::path_get_dirname(filename)));
}

void MainWindow::busy_action_enter()
{
  g_return_if_fail(!busy_action_running_);

  controller_.match_actions.set_enabled(false);
  set_sensitive(match_action_group_, false);

  statusline_->pulse_start();

  busy_action_running_    = true;
  busy_action_cancel_     = false;
  busy_action_iteration_  = 0;
}

void MainWindow::busy_action_leave()
{
  g_return_if_fail(busy_action_running_);

  busy_action_running_ = false;

  statusline_->pulse_stop();

  controller_.match_actions.set_enabled(true);
  set_sensitive(match_action_group_, true);
}

bool MainWindow::on_busy_action_pulse()
{
  g_return_val_if_fail(busy_action_running_, true);

  if (!busy_action_cancel_ && (++busy_action_iteration_ % BUSY_GUI_UPDATE_INTERVAL) == 0)
  {
    statusline_->pulse();

    const Glib::RefPtr<Glib::MainContext> context = Glib::MainContext::get_default();

    do {}
    while (context->iteration(false) && !busy_action_cancel_);
  }

  return busy_action_cancel_;
}

void MainWindow::on_busy_action_cancel()
{
  if (busy_action_running_)
    busy_action_cancel_ = true;
}

void MainWindow::on_about()
{
  if (about_dialog_.get())
  {
    about_dialog_->present();
  }
  else
  {
    std::unique_ptr<Gtk::AboutDialog> dialog (new Gtk::AboutDialog());
    std::vector<Glib::ustring> authors;
    for (int i = 0; program_authors[i]; i++)
      authors.push_back(program_authors[i]);

    dialog->set_version(PACKAGE_VERSION);
    dialog->set_logo_icon_name(PACKAGE_TARNAME);
    dialog->set_comments(_("Search and replace using regular expressions"));
    dialog->set_copyright("Copyright \302\251 2002-2007 Daniel Elstner\n"
    					  "Copyright \302\251 2009-2011 Fabien Parent");
    dialog->set_website("http://regexxer.sourceforge.net/");

    dialog->set_authors(authors);
    dialog->set_translator_credits(_("translator-credits"));
    dialog->set_license(program_license);
    dialog->set_wrap_license(true);

    dialog->set_transient_for(*window_);
    dialog->show();
    dialog->signal_response().connect(sigc::mem_fun(*this, &MainWindow::on_about_dialog_response));

    about_dialog_ = std::move(dialog);
  }
}

void MainWindow::on_about_dialog_response(int)
{
  about_dialog_.reset();
}

void MainWindow::on_preferences()
{
  if (pref_dialog_.get())
  {
    pref_dialog_->get_dialog()->present();
  }
  else
  {
    std::unique_ptr<PrefDialog> dialog (new PrefDialog(*window_));

    dialog->get_dialog()->signal_hide()
        .connect(sigc::mem_fun(*this, &MainWindow::on_pref_dialog_hide));
    dialog->get_dialog()->show();

    pref_dialog_ = std::move(dialog);
  }
}

void MainWindow::on_pref_dialog_hide()
{
  pref_dialog_.reset();
}

void MainWindow::on_conf_value_changed(const Glib::ustring& key)
{
  if (key == conf_key_textview_font)
  {
    std::string style = "GtkTextView { font: ";
    style += Settings::instance()->get_string(key);
    style += "}";
    Glib::RefPtr<Gtk::CssProvider> css = Gtk::CssProvider::create();
    css->load_from_data(style);

    textview_     ->get_style_context()->add_provider(css, 1);
    entry_preview_->get_style_context()->add_provider(css, 1);
  }
}

} // namespace Regexxer
