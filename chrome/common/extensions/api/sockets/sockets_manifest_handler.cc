// Copyright 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/common/extensions/api/sockets/sockets_manifest_handler.h"

#include "chrome/common/extensions/api/sockets/sockets_manifest_data.h"
#include "chrome/common/extensions/api/sockets/sockets_manifest_permission.h"
#include "extensions/common/extension.h"
#include "extensions/common/manifest_constants.h"

namespace extensions {

SocketsManifestHandler::SocketsManifestHandler() {}

SocketsManifestHandler::~SocketsManifestHandler() {}

bool SocketsManifestHandler::Parse(Extension* extension, string16* error) {
  const base::Value* sockets = NULL;
  CHECK(extension->manifest()->Get(manifest_keys::kSockets, &sockets));
  scoped_ptr<SocketsManifestData> data =
      SocketsManifestData::FromValue(*sockets, error);
  if (!data)
    return false;

  extension->SetManifestData(manifest_keys::kSockets, data.release());
  return true;
}

ManifestPermission* SocketsManifestHandler::CreatePermission() {
  return new SocketsManifestPermission();
}

ManifestPermission* SocketsManifestHandler::CreateInitialRequiredPermission(
    const Extension* extension) {
  SocketsManifestData* data = SocketsManifestData::Get(extension);
  if (data)
    return data->permission()->Clone();
  return NULL;
}

const std::vector<std::string> SocketsManifestHandler::Keys() const {
  return SingleKey(manifest_keys::kSockets);
}

}  // namespace extensions
