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
#include <cstring>


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

