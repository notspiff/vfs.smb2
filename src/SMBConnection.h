
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
  struct keepAliveStruct
  {
    std::string sharename;
    uint64_t refreshCounter;
  };
  typedef std::map<struct smb2fh *, struct keepAliveStruct> tFileKeepAliveMap;

  struct contextTimeout
  {
    struct smb2_context *pContext;
    uint64_t lastAccessedTime;
  };

  typedef std::map<std::string, struct contextTimeout> tOpenContextMap;

  static CSMBConnection& Get();
  virtual ~CSMBConnection();
  bool Connect(const VFSURL& url);
  struct smb2_context *GetSmbContext(){return m_pSmbContext;}
  uint64_t GetMaxReadChunkSize(){return m_readChunkSize;}
  uint64_t GetMaxWriteChunkSize(){return m_writeChunkSize;}
  void AddActiveConnection();
  void AddIdleConnection();
  void CheckIfIdle();
  void Deinit();
  //adds the filehandle to the keep alive list or resets
  //the timeout for this filehandle if already in list
  void resetKeepAlive(std::string _sharename, struct smb2fh  *_pFileHandle);
  //removes file handle from keep alive list
  void removeFromKeepAliveList(struct smb2fh  *_pFileHandle);

  const std::string& GetConnectedIp() const {return m_resolvedHostName;}
  const std::string& GetConnectedExport() const {return m_shareName;}
  const std::string  GetContextMapId() const {return m_shareName;}

private:
  CSMBConnection();
  struct smb2_context *m_pSmbContext;
  std::string m_shareName;
  std::string m_hostName;
  std::string m_resolvedHostName;
  uint64_t m_readChunkSize;
  uint64_t m_writeChunkSize;
  int m_OpenConnections;
  unsigned int m_IdleTimeout;
  tFileKeepAliveMap m_KeepAliveTimeouts;
  tOpenContextMap m_openContextMap;
  uint64_t m_lastAccessedTime;
  P8PLATFORM::CMutex m_keepAliveLock;
  P8PLATFORM::CMutex m_openContextLock;

  void clearMembers();
  struct smb2_context *getContextFromMap(std::string sharename, bool forceCacheHit = false);
  int getContextForShare(std::string sharename);
  void destroyOpenContexts();
  void destroyContext(std::string sharename);
  void resolveHost(const std::string& hostname);
  void keepAlive(std::string _sharename, struct smb2fh  *_pFileHandle);
};
