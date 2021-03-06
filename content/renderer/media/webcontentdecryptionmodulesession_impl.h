// Copyright 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CONTENT_RENDERER_MEDIA_WEBCONTENTDECRYPTIONMODULESESSION_IMPL_H_
#define CONTENT_RENDERER_MEDIA_WEBCONTENTDECRYPTIONMODULESESSION_IMPL_H_

#include <string>
#include <vector>

#include "base/basictypes.h"
#include "base/callback.h"
#include "media/base/media_keys.h"
#include "third_party/WebKit/public/platform/WebContentDecryptionModuleSession.h"
#include "third_party/WebKit/public/platform/WebString.h"

namespace media {
class MediaKeys;
}

namespace content {

class WebContentDecryptionModuleSessionImpl
    : public blink::WebContentDecryptionModuleSession {
 public:
  typedef base::Callback<void(uint32 reference_id)> SessionClosedCB;

  WebContentDecryptionModuleSessionImpl(
      media::MediaKeys* media_keys,
      Client* client,
      const SessionClosedCB& session_closed_cb);
  virtual ~WebContentDecryptionModuleSessionImpl();

  // blink::WebContentDecryptionModuleSession implementation.
  virtual blink::WebString sessionId() const OVERRIDE;
  virtual void generateKeyRequest(const blink::WebString& mime_type,
                                  const uint8* init_data,
                                  size_t init_data_length) OVERRIDE;
  virtual void update(const uint8* response, size_t response_length) OVERRIDE;
  virtual void close() OVERRIDE;

  const std::string& session_id() const { return session_id_; }

  uint32 reference_id() const { return reference_id_; }

  // Callbacks.
  void OnSessionCreated(const std::string& session_id);
  void OnSessionMessage(const std::vector<uint8>& message,
                        const std::string& destination_url);
  void OnSessionReady();
  void OnSessionClosed();
  void OnSessionError(media::MediaKeys::KeyError error_code, int system_code);

 private:
  // Non-owned pointers.
  media::MediaKeys* media_keys_;
  Client* client_;

  SessionClosedCB session_closed_cb_;

  // Session ID is the user visible ID for this session generated by the CDM.
  // This value is not set until the CDM calls OnSessionCreated().
  std::string session_id_;

  // Reference ID is used to uniquely track this object so that CDM callbacks
  // can get routed to the correct object.
  const uint32 reference_id_;

  // Reference ID should be unique per renderer process for debugging purposes.
  static uint32 next_reference_id_;

  DISALLOW_COPY_AND_ASSIGN(WebContentDecryptionModuleSessionImpl);
};

}  // namespace content

#endif  // CONTENT_RENDERER_MEDIA_WEBCONTENTDECRYPTIONMODULESESSION_IMPL_H_
