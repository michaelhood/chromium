// Copyright 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "mojo/public/bindings/lib/message.h"

#include <stdlib.h>

#include <algorithm>

namespace mojo {

Message::Message()
    : data(NULL) {
}

Message::~Message() {
  free(data);
  // TODO(davemoore): We don't close the handles because they're typically
  // owned by the Connection. This could result in some Handle leaks. This will
  // be fixed by a later cl.
}

void Message::Swap(Message* other) {
  std::swap(data, other->data);
  std::swap(handles, other->handles);
}

}  // namespace mojo
