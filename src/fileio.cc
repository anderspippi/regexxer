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

#include "fileio.h"
#include "filebuffer.h"
#include "miscutils.h"
#include "stringutils.h"

#include <glib.h>
#include <glibmm.h>
#if 0
#include <libgnomevfsmm.h>
#endif
#include <cstring>


namespace
{

using Regexxer::FileBuffer;

enum { BUFSIZE = 4096 };

#if 0
Glib::RefPtr<FileBuffer> load_try_encoding(const std::string& filename, const std::string& encoding)
{
  //open the input file for read access:
  Gnome::Vfs::Handle read_handle;

  try
  {
    read_handle.open(filename, Gnome::Vfs::OPEN_READ);
  }
  catch(const Gnome::Vfs::exception& ex)
  {
    //TODO: Give the user some meaningful information?
    return Glib::RefPtr<FileBuffer>();
  }

  // The whole thing:
  //Use std::string because it might not be UTF8,
  //and because Gnome::Vfs::Handle::read() reads bytes, not necessarily whole characters.
  std::string text;
    
  try
  {
    // read data from the input URI:

    // Buffer for each chunk:
    const Util::ScopedArray<char> inbuf (new char[BUFSIZE]);
    
    bool bContinue = true;
    while(bContinue)
    {
      Gnome::Vfs::FileSize bytes_read = read_handle.read(inbuf.get(), BUFSIZE);
    
      if(bytes_read == 0)
        bContinue = false; //stop because we reached the end.
      else
      {
        //Check that it's not a binary file:
        if(std::memchr(inbuf.get(), '\0', bytes_read))
          return Glib::RefPtr<FileBuffer>();

        //Add the text to the string:
        text += std::string(inbuf.get(), bytes_read);
      }
    }
  }
  catch(const Gnome::Vfs::exception& ex)
  {
    std::cerr << "Gnome::Vfs::Handle::read() failed: " << ex.what() << std::endl;
    //TODO: Give the user some meaningful information?
    return Glib::RefPtr<FileBuffer>();
  }

  
  try
  {
    Glib::RefPtr<FileBuffer> text_buffer = FileBuffer::create();
   
    //Try to convert it to UTF8, guessing the encoding:
    //(most of the time, it will already be UTF8
    //TODO_performance: This copying of huge strings is probably very slow:
    //encoding is a parameter to this method.
    //if it fails then this method will be called with a different encoding,
    //until the calling method finally gives up.
    std::string text_utf8 = Glib::convert_with_fallback(text, "UTF-8", encoding);

    //Add the string to a TextBuffer:
    text_buffer->insert(text_buffer->end(), text_utf8);

    return text_buffer;
  }
  catch(const Glib::ConvertError& ex)
  {
    //The conversion failed - we probably guessed the encoding wrongly.
    return Glib::RefPtr<FileBuffer>();
  }
}
#else

Glib::RefPtr<FileBuffer> load_iochannel(const Glib::RefPtr<Glib::IOChannel>& input)
{
  const Glib::RefPtr<FileBuffer> text_buffer = FileBuffer::create();
  FileBuffer::iterator text_end = text_buffer->end();

  const Util::ScopedArray<char> inbuf (new char[BUFSIZE]);
  gsize bytes_read = 0;

  while (input->read(inbuf.get(), BUFSIZE, bytes_read) == Glib::IO_STATUS_NORMAL)
  {
    if (std::memchr(inbuf.get(), '\0', bytes_read)) // binary file?
      return Glib::RefPtr<FileBuffer>();

    text_end = text_buffer->insert(text_end, inbuf.get(), inbuf.get() + bytes_read);
  }

  g_assert(bytes_read == 0);

  return text_buffer;
}

void save_iochannel(const Glib::RefPtr<Glib::IOChannel>& output, const Glib::RefPtr<FileBuffer>& buffer)
{
  FileBuffer::iterator start = buffer->begin();
  FileBuffer::iterator stop  = start;

  for (; start; start = stop)
  {
    stop.forward_chars(BUFSIZE); // inaccurate, but doesn't matter
    const Glib::ustring chunk = buffer->get_text(start, stop);

    gsize bytes_written = 0;
    const Glib::IOStatus status = output->write(chunk.data(), chunk.bytes(), bytes_written);

    g_assert(status == Glib::IO_STATUS_NORMAL);
    g_assert(bytes_written == chunk.bytes());
  }
}

Glib::RefPtr<FileBuffer> load_try_encoding(const std::string& filename, const std::string& encoding)
{
  const Glib::RefPtr<Glib::IOChannel> channel = Glib::IOChannel::create_from_file(filename, "r");

  channel->set_buffer_size(BUFSIZE);

  try
  {
    channel->set_encoding(encoding);
    return load_iochannel(channel);
  }
  catch (const Glib::ConvertError&)
  {}

  return Glib::RefPtr<FileBuffer>();
}

#endif

} // anonymous namespace


namespace Regexxer
{

/**** Regexxer::FileInfoBase ***********************************************/

FileInfoBase::~FileInfoBase()
{}


/**** Regexxer::DirInfo ****************************************************/

DirInfo::DirInfo()
:
  file_count     (0),
  modified_count (0)
{}

DirInfo::~DirInfo()
{}


/**** Regexxer::FileInfo ***************************************************/

FileInfo::FileInfo(const std::string& fullname_)
:
  fullname    (fullname_),
  load_failed (false)
{}

FileInfo::~FileInfo()
{}


/**** Regexxer -- file I/O functions ***************************************/

void load_file(const FileInfoPtr& fileinfo, const std::string& fallback_encoding)
{
  fileinfo->load_failed = true;

  std::string encoding = "UTF-8";
  Glib::RefPtr<FileBuffer> buffer = load_try_encoding(fileinfo->fullname, encoding);

  if(!buffer && !Glib::get_charset(encoding)) // locale charset is _not_ UTF-8
  {
    buffer = load_try_encoding(fileinfo->fullname, encoding);
  }

  if(!buffer && !Util::encodings_equal(encoding, fallback_encoding))
  {
    encoding = fallback_encoding;
    buffer = load_try_encoding(fileinfo->fullname, encoding);
  }

  if(buffer)
  {
    buffer->set_modified(false);

    fileinfo->load_failed = false;
    fileinfo->encoding    = encoding;
    fileinfo->buffer      = buffer;
  }
}

#if 0
void save_file(const FileInfoPtr& fileinfo)
{
  //open the input file for write access:
  Gnome::Vfs::Handle write_handle;

  try
  {
    write_handle.open(fileinfo->fullname, Gnome::Vfs::OPEN_WRITE);
  }
  catch(const Gnome::Vfs::exception& ex)
  {
    std::cerr << "Gnome::Vfs::Handle::open() failed: " << ex.what() << std::endl;
    //TODO: Give the user some meaningful information?
    return;
  }

  try
  {
    // Write the data:
    //TODO_performance: It might be faster if we don't use the temporary copy:
    const Glib::ustring chunk = fileinfo->buffer->get_text();
    /* GnomeVFSFileSize bytes_written = */
    write_handle.write(chunk.c_str(), chunk.bytes());
  }
  catch(const Gnome::Vfs::exception& ex)
  {
    std::cerr << "Gnome::Vfs::Handle::write() failed: " << ex.what() << std::endl;
    //TODO: Give the user some meaningful information?
    return;
  }
  
  fileinfo->buffer->set_modified(false);
}
#else
void save_file(const FileInfoPtr& fileinfo)
{
  const Glib::RefPtr<Glib::IOChannel> channel =
      Glib::IOChannel::create_from_file(fileinfo->fullname, "w");

  channel->set_buffer_size(BUFSIZE);

  channel->set_encoding(fileinfo->encoding);
  save_iochannel(channel, fileinfo->buffer);

  channel->close(); // might throw

  fileinfo->buffer->set_modified(false);
}
#endif

} // namespace Regexxer

