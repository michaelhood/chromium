// Copyright 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CONTENT_BROWSER_RENDERER_HOST_PEPPER_PEPPER_FILE_SYSTEM_BROWSER_HOST_H_
#define CONTENT_BROWSER_RENDERER_HOST_PEPPER_PEPPER_FILE_SYSTEM_BROWSER_HOST_H_

#include <string>

#include "base/basictypes.h"
#include "base/callback.h"
#include "base/memory/weak_ptr.h"
#include "content/common/content_export.h"
#include "ppapi/c/pp_file_info.h"
#include "ppapi/c/private/ppb_isolated_file_system_private.h"
#include "ppapi/host/host_message_context.h"
#include "ppapi/host/resource_host.h"
#include "url/gurl.h"
#include "webkit/browser/fileapi/file_system_context.h"
#include "webkit/common/fileapi/file_system_types.h"

namespace content {

class BrowserPpapiHost;

class CONTENT_EXPORT PepperFileSystemBrowserHost
    : public ppapi::host::ResourceHost,
      public base::SupportsWeakPtr<PepperFileSystemBrowserHost> {
 public:
  // Creates a new PepperFileSystemBrowserHost for a file system of a given
  // |type|. The host must be opened before use.
  PepperFileSystemBrowserHost(BrowserPpapiHost* host,
                              PP_Instance instance,
                              PP_Resource resource,
                              PP_FileSystemType type);
  virtual ~PepperFileSystemBrowserHost();

  // Opens the PepperFileSystemBrowserHost to use an existing file system at the
  // given |root_url|. The file system at |root_url| must already be opened and
  // have the type given by GetType().
  // Calls |callback| when complete.
  void OpenExisting(const GURL& root_url, const base::Closure& callback);

  // ppapi::host::ResourceHost override.
  virtual int32_t OnResourceMessageReceived(
      const IPC::Message& msg,
      ppapi::host::HostMessageContext* context) OVERRIDE;
  virtual bool IsFileSystemHost() OVERRIDE;

  // Supports FileRefs direct access on the host side.
  PP_FileSystemType GetType() const { return type_; }
  bool IsOpened() const { return opened_; }
  GURL GetRootUrl() const { return root_url_; }
  scoped_refptr<fileapi::FileSystemContext> GetFileSystemContext() const {
    return file_system_context_;
  }

  // Supports FileIOs direct access on the host side.
  // Non-NULL only for PP_FILESYSTEMTYPE_LOCAL{PERSISTENT,TEMPORARY}.
  fileapi::FileSystemOperationRunner* GetFileSystemOperationRunner() const {
    return file_system_operation_runner_.get();
  }

 private:
  friend class PepperFileSystemBrowserHostTest;

  void OpenExistingWithContext(
      const base::Closure& callback,
      scoped_refptr<fileapi::FileSystemContext> file_system_context);
  void GotFileSystemContext(
      ppapi::host::ReplyMessageContext reply_context,
      fileapi::FileSystemType file_system_type,
      scoped_refptr<fileapi::FileSystemContext> file_system_context);
  void OpenFileSystemComplete(
      ppapi::host::ReplyMessageContext reply_context,
      const GURL& root,
      const std::string& name,
      base::PlatformFileError error);
  void GotIsolatedFileSystemContext(
      ppapi::host::ReplyMessageContext reply_context,
      const std::string& fsid,
      PP_IsolatedFileSystemType_Private type,
      scoped_refptr<fileapi::FileSystemContext> file_system_context);
  void OpenPluginPrivateFileSystem(
      ppapi::host::ReplyMessageContext reply_context,
      const std::string& fsid,
      scoped_refptr<fileapi::FileSystemContext> file_system_context);
  void OpenPluginPrivateFileSystemComplete(
      ppapi::host::ReplyMessageContext reply_context,
      const std::string& fsid,
      base::PlatformFileError error);

  int32_t OnHostMsgOpen(ppapi::host::HostMessageContext* context,
                        int64_t expected_size);
  int32_t OnHostMsgInitIsolatedFileSystem(
      ppapi::host::HostMessageContext* context,
      const std::string& fsid,
      PP_IsolatedFileSystemType_Private type);

  void SendReplyForIsolatedFileSystem(
      ppapi::host::ReplyMessageContext reply_context,
      const std::string& fsid,
      int32_t error);

  void SetFileSystemContext(
      scoped_refptr<fileapi::FileSystemContext> file_system_context);

  std::string GetPluginMimeType() const;

  // Returns plugin ID generated from plugin's MIME type.
  std::string GeneratePluginId(const std::string& mime_type) const;

  BrowserPpapiHost* browser_ppapi_host_;

  PP_FileSystemType type_;
  bool called_open_;  // whether open has been called.
  bool opened_;  // whether open has succeeded.
  GURL root_url_;
  scoped_refptr<fileapi::FileSystemContext> file_system_context_;

  scoped_ptr<fileapi::FileSystemOperationRunner> file_system_operation_runner_;

  std::string fsid_;  // used only for isolated filesystems.

  base::WeakPtrFactory<PepperFileSystemBrowserHost> weak_factory_;

  DISALLOW_COPY_AND_ASSIGN(PepperFileSystemBrowserHost);
};

}  // namespace content

#endif  // CONTENT_BROWSER_RENDERER_HOST_PEPPER_PEPPER_FILE_SYSTEM_BROWSER_HOST_H_
