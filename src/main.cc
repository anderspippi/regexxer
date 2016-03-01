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

#include "globalstrings.h"
#include "mainwindow.h"
#include "miscutils.h"
#include "translation.h"

#include <glib.h>
#include <gtk/gtk.h> /* for gtk_window_set_default_icon_name() */
#include <glibmm.h>
#include <gtkmm/iconset.h>
#include <gtkmm/iconsource.h>
#include <gtkmm/main.h>
#include <gtkmm/stock.h>
#include <gtkmm/stockitem.h>
#include <gtkmm/applicationwindow.h>
#include <gtksourceviewmm/init.h>
#include <giomm/init.h>

#include <exception>
#include <list>
#include <memory>

#include <config.h>

namespace
{

class RegexxerOptions
{
private:
  Regexxer::InitState   init_state_;
  Glib::OptionGroup     group_;
  Glib::OptionContext   context_;

  static Glib::OptionEntry entry(const char* long_name, char short_name,
                                 const char* description, const char* arg_description = 0);
  RegexxerOptions();

public:
  static std::unique_ptr<RegexxerOptions> create();
  ~RegexxerOptions();

  Glib::OptionContext& context()    { return context_; }
  Regexxer::InitState& init_state() { return init_state_; }
};

// static
Glib::OptionEntry RegexxerOptions::entry(const char* long_name, char short_name,
                                         const char* description, const char* arg_description)
{
  Glib::OptionEntry option_entry;

  option_entry.set_long_name(long_name);
  option_entry.set_short_name(short_name);

  if (description)
    option_entry.set_description(description);
  if (arg_description)
    option_entry.set_arg_description(arg_description);

  return option_entry;
}

RegexxerOptions::RegexxerOptions()
:
  init_state_ (),
  group_      (PACKAGE_TARNAME, Glib::ustring()),
  context_    ()
{}

RegexxerOptions::~RegexxerOptions()
{}

// static
std::unique_ptr<RegexxerOptions> RegexxerOptions::create()
{
  std::unique_ptr<RegexxerOptions> options (new RegexxerOptions());

  Glib::OptionGroup&  group = options->group_;
  Regexxer::InitState& init = options->init_state_;

  group.add_entry(entry("pattern", 'p', N_("Find files matching PATTERN"), N_("PATTERN")),
                  init.pattern);
  group.add_entry(entry("no-recursion", 'R', N_("Do not recurse into subdirectories")),
                  init.no_recursive);
  group.add_entry(entry("hidden", 'h', N_("Also find hidden files")),
                  init.hidden);
  group.add_entry(entry("regex", 'e', N_("Find text matching REGEX"), N_("REGEX")),
                  init.regex);
  group.add_entry(entry("no-global", 'G', N_("Find only the first match in a line")),
                  init.no_global);
  group.add_entry(entry("ignore-case", 'i', N_("Do case insensitive matching")),
                  init.ignorecase);
  group.add_entry(entry("substitution", 's', N_("Replace matches with STRING"), N_("STRING")),
                  init.substitution);
  group.add_entry(entry("line-number", 'n', N_("Print match location to standard output")),
                  init.feedback);
  group.add_entry(entry("no-autorun", 'A', N_("Do not automatically start search")),
                  init.no_autorun);
  group.add_entry_filename(entry(G_OPTION_REMAINING, '\0', 0, N_("[FOLDER]")),
                           init.folder);

  group.set_translation_domain(PACKAGE_TARNAME);
  options->context_.set_main_group(group);

  return options;
}

} // anonymous namespace

int main(int argc, char** argv)
{
  try
  {
    Util::initialize_gettext(PACKAGE_TARNAME, REGEXXER_LOCALEDIR);

    std::unique_ptr<RegexxerOptions> options = RegexxerOptions::create();
    options->context().parse(argc, argv);
    Glib::RefPtr<Gtk::Application> app =
        Gtk::Application::create(argc, argv, Regexxer::conf_schema);

    Gsv::init();
    Gio::init();

    Glib::set_application_name(PACKAGE_NAME);
    gtk_window_set_default_icon_name(PACKAGE_TARNAME);

    Regexxer::MainWindow window;

    window.initialize(app, options->init_state());
    options.reset();

    //app->register_application();
    app->run(*window.get_window());

    return 0;
  }
  catch (const Glib::OptionError& error)
  {
    const Glib::ustring what = error.what();
    g_printerr("%s: %s\n", g_get_prgname(), what.c_str());
  }
  catch (const Glib::Error& error)
  {
    const Glib::ustring what = error.what();
    g_error("unhandled exception: %s", what.c_str());
  }
  catch (const std::exception& ex)
  {
    g_error("unhandled exception: %s", ex.what());
  }
  catch (...)
  {
    g_error("unhandled exception: (type unknown)");
  }

  return 1;
}
