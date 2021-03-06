// Copyright 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CONTENT_BROWSER_FRAME_HOST_NAVIGATOR_H_
#define CONTENT_BROWSER_FRAME_HOST_NAVIGATOR_H_

#include "base/memory/ref_counted.h"
#include "content/common/content_export.h"

namespace content {

class NavigationControllerImpl;
class NavigatorDelegate;

// This class is responsible for performing navigations in a node of the
// FrameTree. Its lifetime is bound to all FrameTreeNode objects that are
// using it and will be released once all nodes that use it are freed.
// The Navigator is bound to a single frame tree and cannot be used by multiple
// instances of FrameTree.
// TODO(nasko): Move all navigation methods, such as didStartProvisionalLoad
// from WebContentsImpl to this class.
class CONTENT_EXPORT Navigator : public base::RefCounted<Navigator> {
 public:
  Navigator(NavigationControllerImpl* nav_controller,
            NavigatorDelegate* delegate);

  NavigationControllerImpl* controller() {
    return controller_;
  }

  NavigatorDelegate* delegate() {
    return delegate_;
  }

 private:
  friend class base::RefCounted<Navigator>;
  virtual ~Navigator() {}

  // The NavigationController that will keep track of session history for all
  // RenderFrameHost objects using this Navigator.
  // TODO(nasko): Move ownership of the NavigationController from
  // WebContentsImpl to this class.
  NavigationControllerImpl* controller_;

  // Used to notify the object embedding this Navigator about navigation
  // events. Can be NULL in tests.
  NavigatorDelegate* delegate_;
};

}  // namespace content

#endif  // CONTENT_BROWSER_FRAME_HOST_NAVIGATOR_H_
