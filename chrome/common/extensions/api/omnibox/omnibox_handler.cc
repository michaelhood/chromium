// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/common/extensions/api/omnibox/omnibox_handler.h"

#include "base/memory/scoped_ptr.h"
#include "base/strings/string_util.h"
#include "base/strings/utf_string_conversions.h"
#include "base/values.h"
#include "extensions/common/extension.h"
#include "extensions/common/manifest.h"
#include "extensions/common/manifest_constants.h"

namespace extensions {

namespace {

// Manifest keys.
const char kKeyword[] = "keyword";

}  // namespace

// static
const std::string& OmniboxInfo::GetKeyword(const Extension* extension) {
  OmniboxInfo* info = static_cast<OmniboxInfo*>(
      extension->GetManifestData(manifest_keys::kOmnibox));
  return info ? info->keyword : EmptyString();
}

OmniboxHandler::OmniboxHandler() {
}

OmniboxHandler::~OmniboxHandler() {
}

bool OmniboxHandler::Parse(Extension* extension, string16* error) {
  scoped_ptr<OmniboxInfo> info(new OmniboxInfo);
  const base::DictionaryValue* dict = NULL;
  if (!extension->manifest()->GetDictionary(manifest_keys::kOmnibox,
                                            &dict) ||
      !dict->GetString(kKeyword, &info->keyword) ||
      info->keyword.empty()) {
    *error = ASCIIToUTF16(manifest_errors::kInvalidOmniboxKeyword);
    return false;
  }
  extension->SetManifestData(manifest_keys::kOmnibox, info.release());
  return true;
}

const std::vector<std::string> OmniboxHandler::Keys() const {
  return SingleKey(manifest_keys::kOmnibox);
}

}  // namespace extensions
