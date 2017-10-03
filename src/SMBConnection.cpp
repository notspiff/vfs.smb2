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

#include "SMBConnection.h"

#include <p8-platform/util/timeutils.h>

#include <kodi/Filesystem.h>
#include <kodi/General.h>
#include <kodi/Network.h>

CSMBConnection& CSMBConnection::Get()
{
  static CSMBConnection instance;

  return instance;
}

CSMBConnection::CSMBConnection()
: m_pSmbContext(nullptr)
, m_hostName("")
, m_resolvedHostName("")
, m_readChunkSize(0)
, m_writeChunkSize(0)
{
}

CSMBConnection::~CSMBConnection()
{
  smb2_destroy_context(m_pSmbContext);

  m_hostName.clear();
  m_writeChunkSize = 0;
  m_readChunkSize = 0;
  m_pSmbContext = nullptr;
}

void CSMBConnection::resolveHost(const std::string& hostname)
{
  kodi::network::DNSLookup(hostname, m_resolvedHostName);
}

bool CSMBConnection::Connect(const VFSURL& url)
{
  P8PLATFORM::CLockObject lock(*this);
  smb2_url *smburl = nullptr;

  resolveHost(url.hostname);

  if(m_hostName != url.hostname)
  {
    m_pSmbContext = smb2_init_context();
    if(!m_pSmbContext)
    {
      kodi::Log(ADDON_LOG_ERROR, "Failed to init context");
      return false;
    }

    smburl = smb2_parse_url(m_pSmbContext, url.url);
    if (!smburl)
    {
      kodi::Log(ADDON_LOG_ERROR, "Failed to parse url: %s", smb2_get_error(m_pSmbContext));
      return false;
    }

    smb2_set_security_mode(m_pSmbContext, SMB2_NEGOTIATE_SIGNING_ENABLED);

    auto ret = smb2_connect_share(m_pSmbContext, smburl->server, smburl->share);
    if (ret < 0)
    {
      kodi::Log(ADDON_LOG_ERROR, "smb2_connect_share failed. %s", smb2_get_error(m_pSmbContext));
      smb2_destroy_url(smburl);
      smb2_destroy_context(m_pSmbContext);
      return false;
    }

    kodi::Log(ADDON_LOG_DEBUG,"SMB: Connected to server %s and share %s", url.hostname, smburl->share);

    m_hostName = url.hostname;

    m_readChunkSize = smb2_get_max_read_size(m_pSmbContext);
    m_writeChunkSize = smb2_get_max_write_size(m_pSmbContext);

    kodi::Log(ADDON_LOG_DEBUG,"SMB: chunks: r/w %i/%i\n", (int)m_readChunkSize,(int)m_writeChunkSize);
  }

  return true;
}
