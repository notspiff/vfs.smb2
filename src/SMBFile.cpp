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

#include "p8-platform/threads/mutex.h"
#include <fcntl.h>
#include <sstream>
#include <iostream>

extern "C"
{
#include <smb2/smb2.h>
#include <smb2/libsmb2.h>
#include <smb2/libsmb2-raw.h>
}

#include "SMBFile.h"

void* CSMBFile::Open(const VFSURL& url)
{
  int ret = 0;

  smb2_url *smburl = nullptr;

  P8PLATFORM::CLockObject lock(CSMBConnection::Get());

  if(!CSMBConnection::Get().Connect(url))
  {
    return nullptr;
  }

  smburl = smb2_parse_url(CSMBConnection::Get().GetSmbContext(), url.url);
  if (!smburl)
  {
    kodi::Log(ADDON_LOG_ERROR, "Failed to parse url: %s", smb2_get_error(CSMBConnection::Get().GetSmbContext()));
    return nullptr;
  }

  SMBContext* result = new SMBContext;

  result->pSmbContext = CSMBConnection::Get().GetSmbContext();

  result->pFileHandle = smb2_open(result->pSmbContext, smburl->path, O_RDONLY);

  if (!result->pFileHandle)
  {
    kodi::Log(ADDON_LOG_INFO, "CSMBFile::Open: Unable to open file : '%s'  error : '%s'", smburl->path, smb2_get_error(result->pSmbContext));
    delete result;
    return nullptr;
  }

  kodi::Log(ADDON_LOG_DEBUG,"CSMBFile::Open - opened %s", smburl->path);
  result->filename = smburl->path;

  struct __stat64 tmpBuffer;

  if( Stat(url, &tmpBuffer) )
  {
    Close(result);
    return nullptr;
  }

  result->size = tmpBuffer.st_size;
  // We've successfully opened the file!
  return result;
}

ssize_t CSMBFile::Read(void* context, void* lpBuf, size_t uiBufSize)
{
  SMBContext* ctx = (SMBContext*)context;
  if (!ctx || !ctx->pFileHandle|| !ctx->pSmbContext)
    return -1;

  P8PLATFORM::CLockObject lock(CSMBConnection::Get());
  ssize_t numberOfBytesRead = smb2_read(ctx->pSmbContext, ctx->pFileHandle, (uint8_t *)lpBuf, uiBufSize);

  //something went wrong ...
  if (numberOfBytesRead < 0)
    kodi::Log(ADDON_LOG_ERROR, "%s - Error( %" PRId64", %s )", __FUNCTION__, (int64_t)numberOfBytesRead, smb2_get_error(ctx->pSmbContext));

  return numberOfBytesRead;
}

int64_t CSMBFile::Seek(void* context, int64_t iFilePosition, int iWhence)
{
  SMBContext* ctx = (SMBContext*)context;
  if (!ctx || !ctx->pFileHandle|| !ctx->pSmbContext)
    return 0;

  int ret = 0;

  P8PLATFORM::CLockObject lock(CSMBConnection::Get());

  ret = (int)smb2_lseek(ctx->pSmbContext, ctx->pFileHandle, iFilePosition, iWhence);
  if (ret < 0)
  {
    kodi::Log(ADDON_LOG_ERROR, "%s - Error( seekpos: %" PRId64 ", whence: %i, fsize: %" PRId64 ", %s)",
              __FUNCTION__, iFilePosition, iWhence, ctx->size, smb2_get_error(ctx->pSmbContext));
    return -1;
  }

  // not sure about this, might have to find a way to get current offset
  return (int64_t)ret;
}

int64_t CSMBFile::GetLength(void* context)
{
  if (!context)
    return 0;

  SMBContext* ctx = (SMBContext*)context;
  return ctx->size;
}

int64_t CSMBFile::GetPosition(void* context)
{
  if (!context)
    return 0;

  SMBContext* ctx = (SMBContext*)context;
  int ret = 0;
  uint64_t offset = 0;

  if (CSMBConnection::Get().GetSmbContext() == nullptr || ctx->pFileHandle == nullptr)
    return 0;

  P8PLATFORM::CLockObject lock(CSMBConnection::Get());

  ret = (int)smb2_lseek(CSMBConnection::Get().GetSmbContext(), ctx->pFileHandle, 0, SEEK_CUR);

  if (ret < 0)
  {
    kodi::Log(ADDON_LOG_ERROR, "SMB: Failed to lseek(%s)", smb2_get_error(CSMBConnection::Get().GetSmbContext()));
  }

  // todo, smb2_lseek doesn't provide current position information
  return ret;
}

int CSMBFile::IoControl(void* context, XFILE::EIoControl request, void* param)
{
  if(request == XFILE::IOCTRL_SEEK_POSSIBLE)
    return 1;

  return -1;
}

int CSMBFile::Stat(const VFSURL& url, struct __stat64* buffer)
{
  smb2_url *smburl = nullptr;
  int ret = 0;
  P8PLATFORM::CLockObject lock(CSMBConnection::Get());
  std::string filename;

  if(!CSMBConnection::Get().Connect(url))
  {
    kodi::Log(ADDON_LOG_ERROR, "failed to connect");
    return -1;
  }

  smburl = smb2_parse_url(CSMBConnection::Get().GetSmbContext(), url.url);
  if (!smburl)
  {
    kodi::Log(ADDON_LOG_ERROR, "Failed to parse url: %s", smb2_get_error(CSMBConnection::Get().GetSmbContext()));
    return false;
  }

  struct smb2_stat_64 st;

  ret = smb2_stat(CSMBConnection::Get().GetSmbContext(), smburl->path, &st);

  //if buffer == nullptr we where called from Exists - in that case don't spam the log with errors
  if (ret != 0 && buffer != nullptr)
  {
    kodi::Log(ADDON_LOG_ERROR, "SMB: Failed to stat(%s) %s", url.filename, smb2_get_error(CSMBConnection::Get().GetSmbContext()));
    ret = -1;
  }
  else
  {
    if(buffer)
    {
      memset(buffer, 0, sizeof(struct __stat64));
      buffer->st_ino = st.smb2_ino;
      buffer->st_nlink = st.smb2_nlink;
      buffer->st_size = st.smb2_size;
      buffer->st_atime = st.smb2_atime;
      buffer->st_mtime = st.smb2_mtime;
      buffer->st_ctime = st.smb2_ctime;
    }
  }
  return ret;
}

bool CSMBFile::Close(void* context)
{
  SMBContext* ctx = (SMBContext*)context;
  if (!ctx)
    return false;

  P8PLATFORM::CLockObject lock(CSMBConnection::Get());

  if (ctx->pFileHandle != nullptr && ctx->pSmbContext != nullptr)
  {
    int ret = 0;
    kodi::Log(ADDON_LOG_DEBUG,"CSMBFile::Close closing file %s", ctx->filename.c_str());

    ret = smb2_close(ctx->pSmbContext, ctx->pFileHandle);

    if (ret < 0)
    {
      kodi::Log(ADDON_LOG_ERROR, "Failed to close(%s) - %s", ctx->filename.c_str(), smb2_get_error(ctx->pSmbContext));
    }
  }

  delete ctx;

  return true;
}

bool CSMBFile::Exists(const VFSURL& url)
{
  return Stat(url, nullptr) == 0;
}

bool CSMBFile::GetDirectory(const VFSURL& url, std::vector<kodi::vfs::CDirEntry>& items, CVFSCallbacks callbacks)
{
  P8PLATFORM::CLockObject lock(CSMBConnection::Get());

  smb2_url *smburl = nullptr;
  smb2dir *smbdir = nullptr;
  smb2dirent *smbdirent = nullptr;

  if(!CSMBConnection::Get().Connect(url))
  {
    kodi::Log(ADDON_LOG_ERROR, "failed to connect");
    return false;
  }

  smburl = smb2_parse_url(CSMBConnection::Get().GetSmbContext(), url.url);
  if (!smburl)
  {
    kodi::Log(ADDON_LOG_ERROR, "Failed to parse url: %s", smb2_get_error(CSMBConnection::Get().GetSmbContext()));
    return false;
  }

  smbdir = smb2_opendir(CSMBConnection::Get().GetSmbContext(), smburl->path);
  if(!smbdir)
  {
    kodi::Log(ADDON_LOG_ERROR, "smb2_opendir failed. %s", smb2_get_error(CSMBConnection::Get().GetSmbContext()));
    return false;
  }

  lock.Unlock();

  while((smbdirent = smb2_readdir(CSMBConnection::Get().GetSmbContext(), smbdir)) != nullptr)
  {
    int64_t iSize = 0;
    bool bIsDir = false;
    int64_t lTimeDate = 0;
    std::string path(std::string(url.url) + std::string(smbdirent->name));

    iSize = smbdirent->st.smb2_size;
    bIsDir = smbdirent->st.smb2_type == SMB2_TYPE_DIRECTORY;
    lTimeDate = smbdirent->st.smb2_mtime;

    if(lTimeDate == 0)
    {
      lTimeDate = smbdirent->st.smb2_ctime;
    }

    kodi::vfs::CDirEntry pItem;
    pItem.SetLabel(smbdirent->name);
    pItem.SetSize(iSize);

    if (bIsDir)
    {
      if (path[path.size()-1] != '/')
        path += '/';
      pItem.SetFolder(true);
    }
    else
    {
      pItem.SetFolder(false);
    }

    if (smbdirent->name[0] == '.')
    {
      pItem.AddProperty("file:hidden", "true");
    }
    else
    {
      pItem.ClearProperties();
    }
    pItem.SetPath(path);
    items.push_back(pItem);
  }

  lock.Lock();
  smb2_closedir(CSMBConnection::Get().GetSmbContext(), smbdir);
  return true;
}
