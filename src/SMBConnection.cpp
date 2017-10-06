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

//KEEP_ALIVE_TIMEOUT is decremented every half a second
//360 * 0.5s == 180s == 3mins
//so when no read was done for 3mins and files are open
//do the nfs keep alive for the open files
#define KEEP_ALIVE_TIMEOUT 360

//6 mins (360s) cached context timeout
#define CONTEXT_TIMEOUT 360000

//return codes for getContextForExport
#define CONTEXT_INVALID  0    //getcontext failed
#define CONTEXT_NEW      1    //new context created
#define CONTEXT_CACHED   2    //context cached and therefore already mounted (no new mount needed)

CSMBConnection& CSMBConnection::Get()
{
  static CSMBConnection instance;

  return instance;
}

CSMBConnection::CSMBConnection()
: m_pSmbContext(nullptr)
, m_shareName("")
, m_hostName("")
, m_resolvedHostName("")
, m_readChunkSize(0)
, m_writeChunkSize(0)
, m_OpenConnections(0)
, m_IdleTimeout(0)
, m_lastAccessedTime(0)
{
}

CSMBConnection::~CSMBConnection()
{
  Deinit();
}

void CSMBConnection::clearMembers()
{
  // NOTE - DON'T CLEAR m_exportList HERE!
  // splitUrlIntoExportAndPath checks for m_exportList.empty()
  // and would query the server in an excessive unwanted fashion
  // also don't clear m_KeepAliveTimeouts here because we
  // would loose any "paused" file handles during export change
  m_shareName.clear();
  m_hostName.clear();
  m_writeChunkSize = 0;
  m_readChunkSize = 0;
  m_pSmbContext = nullptr;
}

void CSMBConnection::resolveHost(const std::string& hostname)
{
  kodi::network::DNSLookup(hostname, m_resolvedHostName);
}

void CSMBConnection::destroyOpenContexts()
{
  m_openContextLock.Lock();
  for (const auto& entry : m_openContextMap)
  {
    smb2_destroy_context(entry.second.pContext);
  }
  m_openContextMap.clear();
  m_openContextLock.Unlock();
}

void CSMBConnection::destroyContext(std::string sharename)
{
  m_openContextLock.Lock();
  auto it = m_openContextMap.find(sharename);
  if (it != m_openContextMap.end())
  {
    smb2_destroy_context(it->second.pContext);
    m_openContextMap.erase(it);
  }
  m_openContextLock.Unlock();
}

struct smb2_context *CSMBConnection::getContextFromMap(std::string sharename, bool forceCacheHit/* = false*/)
{
  struct smb2_context *pRet = nullptr;
  m_openContextLock.Lock();

  auto it = m_openContextMap.find(sharename);
  if(it != m_openContextMap.end())
  {
    //check if context has timed out already
    uint64_t now = P8PLATFORM::GetTimeMs();
    if((now - it->second.lastAccessedTime) < CONTEXT_TIMEOUT || forceCacheHit)
    {
      //its not timedout yet or caller wants the cached entry regardless of timeout
      //refresh access time of that
      //context and return it
      if (!forceCacheHit) // only log it if this isn't the resetkeepalive on each read ;)
        kodi::Log(ADDON_LOG_DEBUG, "SMB: Refreshing context for %s, old: %" PRId64 ", new: %" PRId64, sharename.c_str(), it->second.lastAccessedTime, now);
      it->second.lastAccessedTime = now;
      pRet = it->second.pContext;
    }
    else
    {
      //context is timed out
      //destroy it and return nullptr
      kodi::Log(ADDON_LOG_DEBUG, "SMB: Old context timed out - destroying it");
      smb2_destroy_context(it->second.pContext);
      m_openContextMap.erase(it);
    }
  }
  m_openContextLock.Unlock();
  return pRet;
}

int CSMBConnection::getContextForShare(std::string sharename)
{
  int ret = CONTEXT_INVALID;

  clearMembers();

  m_pSmbContext = getContextFromMap(sharename);

  if(!m_pSmbContext)
  {
    kodi::Log(ADDON_LOG_DEBUG,"SMB: Context for %s not open - get a new context.", sharename.c_str());
    m_pSmbContext = smb2_init_context();

    if(!m_pSmbContext)
    {
      kodi::Log(ADDON_LOG_ERROR, "Failed to init context");
      return false;
    }
    else
    {
      struct contextTimeout tmp;
      m_openContextLock.Lock();
      tmp.pContext = m_pSmbContext;
      tmp.lastAccessedTime = P8PLATFORM::GetTimeMs();
      m_openContextMap[sharename] = tmp; //add context to list of all contexts
      ret = CONTEXT_NEW;
      m_openContextLock.Unlock();
    }
  }
  else
  {
    ret = CONTEXT_CACHED;
    kodi::Log(ADDON_LOG_DEBUG,"SMB: Using cached context.");
  }
  m_lastAccessedTime = P8PLATFORM::GetTimeMs(); //refresh last access time of m_pNfsContext

  return ret;
}

bool CSMBConnection::Connect(const VFSURL& url)
{
  P8PLATFORM::CLockObject lock(*this);
  smb2_url *smburl = nullptr;
  std::string sharename = url.hostname + "/" + url.sharename;

  resolveHost(url.hostname);

  if( (sharename != m_shareName || m_hostName != url.hostname) ||
      (P8PLATFORM::GetTimeMs() - m_lastAccessedTime) > CONTEXT_TIMEOUT )
  {

    auto contextRet = getContextForShare(sharename);
    if(contextRet == CONTEXT_INVALID)//we need a new context because sharename or hostname has changed
    {
      return false;
    }

    if(contextRet == CONTEXT_NEW) //new context was created - we need to mount it
    {

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
    }

    m_shareName = sharename;
    m_hostName = url.hostname;

    m_readChunkSize = 4096; //smb2_get_max_read_size(m_pSmbContext);
    m_writeChunkSize = 4096; //smb2_get_max_write_size(m_pSmbContext);

    kodi::Log(ADDON_LOG_DEBUG,"SMB: chunks: r/w %i/%i\n", (int)m_readChunkSize,(int)m_writeChunkSize);
  }

  return true;
}

void CSMBConnection::Deinit()
{
  if(m_pSmbContext)
  {
    destroyOpenContexts();
    m_pSmbContext = nullptr;
  }
  clearMembers();
  // clear any keep alive timouts on deinit
  m_KeepAliveTimeouts.clear();
}

/* This is called from CApplication::ProcessSlow() and is used to tell if nfs have been idle for too long */
void CSMBConnection::CheckIfIdle()
{
  /* We check if there are open connections. This is done without a lock to not halt the mainthread. It should be thread safe as
   worst case scenario is that m_OpenConnections could read 0 and then changed to 1 if this happens it will enter the if wich will lead to another check, wich is locked.  */
  if (m_OpenConnections == 0 && m_pSmbContext != nullptr)
  { /* I've set the the maximum IDLE time to be 1 min and 30 sec. */
    P8PLATFORM::CLockObject lock(*this);
    if (m_OpenConnections == 0 /* check again - when locked */)
    {
      if (m_IdleTimeout > 0)
      {
        m_IdleTimeout--;
      }
      else
      {
        kodi::Log(ADDON_LOG_NOTICE, "SMB is idle. Closing the remaining connections.");
        Deinit();
      }
    }
  }

  if( m_pSmbContext != nullptr )
  {
    P8PLATFORM::CLockObject lock(m_keepAliveLock);
    //handle keep alive on opened files
    for (auto& entry : m_KeepAliveTimeouts)
    {
      if(entry.second.refreshCounter > 0)
      {
        entry.second.refreshCounter--;
      }
      else
      {
        keepAlive(entry.second.sharename, entry.first);
        //reset timeout
        resetKeepAlive(entry.second.sharename, entry.first);
      }
    }
  }
}

//remove file handle from keep alive list on file close
void CSMBConnection::removeFromKeepAliveList(struct smb2fh  *_pFileHandle)
{
  P8PLATFORM::CLockObject lock(m_keepAliveLock);
  m_KeepAliveTimeouts.erase(_pFileHandle);
}

//reset timeouts on read
void CSMBConnection::resetKeepAlive(std::string _sharename, struct smb2fh  *_pFileHandle)
{
  P8PLATFORM::CLockObject lock(m_keepAliveLock);
  //refresh last access time of the context aswell
  struct smb2_context *pContext = getContextFromMap(_sharename, true);

  // if we keep alive using m_pNfsContext we need to mark
  // its last access time too here
  if (m_pSmbContext == pContext)
  {
    m_lastAccessedTime = P8PLATFORM::GetTimeMs();
  }

  //adds new keys - refreshs existing ones
  m_KeepAliveTimeouts[_pFileHandle].sharename = _sharename;
  m_KeepAliveTimeouts[_pFileHandle].refreshCounter = KEEP_ALIVE_TIMEOUT;
}

//keep alive the filehandles nfs connection
//by blindly doing a read 32bytes - seek back to where
//we were before
void CSMBConnection::keepAlive(std::string _sharename, struct smb2fh  *_pFileHandle)
{
  uint64_t offset = 0;
  char buffer[32];
  // this also refreshs the last accessed time for the context
  // true forces a cachehit regardless the context is timedout
  // on this call we are sure its not timedout even if the last accessed
  // time suggests it.
  struct smb2_context *pContext = getContextFromMap(_sharename, true);

  if (!pContext)// this should normally never happen - paranoia
    pContext = m_pSmbContext;

  kodi::Log(ADDON_LOG_NOTICE, "SMB: sending keep alive after %i s.", KEEP_ALIVE_TIMEOUT/2);
  P8PLATFORM::CLockObject lock(*this);
  smb2_lseek(pContext, _pFileHandle, 0, SEEK_CUR, &offset);
  smb2_read(pContext, _pFileHandle, (uint8_t *)buffer, 32);
  smb2_lseek(pContext, _pFileHandle, 0, SEEK_SET, &offset);
}

/* The following two function is used to keep track on how many Opened files/directories there are.
needed for unloading the dylib*/
void CSMBConnection::AddActiveConnection()
{
  P8PLATFORM::CLockObject lock(*this);
  m_OpenConnections++;
}

void CSMBConnection::AddIdleConnection()
{
  P8PLATFORM::CLockObject lock(*this);
  m_OpenConnections--;
  /* If we close a file we reset the idle timer so that we don't have any wierd behaviours if a user
   leaves the movie paused for a long while and then press stop */
  m_IdleTimeout = 180;
}
