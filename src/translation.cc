/* $Id$
 *
 * Copyright (c) 2004  Daniel Elstner  <daniel.elstner@gmx.net>
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

#include <config.h>
#if ENABLE_NLS
# include <libintl.h>
#endif

#include "translation.h"
#include "miscutils.h"

#include <glibmm.h>
#include <vector>
#include <cstring>


namespace
{

Glib::ustring compose_impl(const Glib::ustring& format, const std::vector<Glib::ustring>& args)
{
  using namespace Glib;

  ustring result;
  const ustring::const_iterator pend = format.end();

  for (ustring::const_iterator p = format.begin(); p != pend; ++p)
  {
    gunichar uc = *p;

    if (uc == '%' && Util::next(p) != pend)
    {
      uc = *++p;

      if (Unicode::isdigit(uc))
      {
        const int index = Unicode::digit_value(uc) - 1;

        if (index >= 0 && unsigned(index) < args.size())
        {
          result += args[index];
          continue;
        }
      }

      if (uc != '%')
        result += '%';
    }

    result += uc;
  }

  return result;
}

} // anonymous namespace


#if ENABLE_NLS
void Util::initialize_gettext(const char* domain, const char* localedir)
{
  bindtextdomain(domain, localedir);
  bind_textdomain_codeset(domain, "UTF-8");
  textdomain(domain);
}
#else
void Util::initialize_gettext(const char*, const char*)
{}
#endif /* ENABLE_NLS */

/*
 * This is just like the sgettext() in the GNU gettext manual.
 */
const char* Util::sgettext(const char* msgid)
{
#if ENABLE_NLS
  const char* result = gettext(msgid);

  if (result == msgid)
#else
  const char* result = msgid;
#endif
  {
    if (const char *const delimiter = std::strrchr(result, '|'))
      result = delimiter + 1;
  }

  return result;
}

Glib::ustring Util::compose(const Glib::ustring& format, const Glib::ustring& arg1)
{
  std::vector<Glib::ustring> args;
  args.push_back(arg1);
  return compose_impl(format, args);
}

Glib::ustring Util::compose(const Glib::ustring& format, const Glib::ustring& arg1,
                                                         const Glib::ustring& arg2)
{
  std::vector<Glib::ustring> args;
  args.push_back(arg1);
  args.push_back(arg2);
  return compose_impl(format, args);
}

Glib::ustring Util::compose(const Glib::ustring& format, const Glib::ustring& arg1,
                                                         const Glib::ustring& arg2,
                                                         const Glib::ustring& arg3)
{
  std::vector<Glib::ustring> args;
  args.push_back(arg1);
  args.push_back(arg2);
  args.push_back(arg3);
  return compose_impl(format, args);
}

