
/*
 *      Copyright (C) 2005-2013 Team XBMC
 *      http://xbmc.org
 *
 *  This Program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2, or (at your option)
 *  any later version.
 *
 *  This Program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with XBMC; see the file COPYING.  If not, see
 *  <http://www.gnu.org/licenses/>.
 *
 */

#include <list>
#include <map>
#include <stdint.h>
#include <string>

extern "C"
{
#include <smb2/smb2.h>
#include <smb2/libsmb2.h>
#include <smb2/libsmb2-raw.h>
}

#include <kodi/addon-instance/VFS.h>
#include <p8-platform/threads/mutex.h>

class CSMBConnection : public P8PLATFORM::CMutex
{
public:
  static CSMBConnection& Get();
  virtual ~CSMBConnection();
  bool Connect(const VFSURL& url);
  struct smb2_context *GetSmbContext(){return m_pSmbContext;}
  uint64_t GetMaxReadChunkSize(){return m_readChunkSize;}
  uint64_t GetMaxWriteChunkSize(){return m_writeChunkSize;}

private:
  CSMBConnection();
  struct smb2_context *m_pSmbContext;
  std::string m_hostName;
  std::string m_resolvedHostName;
  uint64_t m_readChunkSize;
  uint64_t m_writeChunkSize;

  void resolveHost(const std::string& hostname);
};
