// Copyright 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef UI_EVENTS_TEST_EVENTS_TEST_UTILS_X11_H_
#define UI_EVENTS_TEST_EVENTS_TEST_UTILS_X11_H_

#include "base/memory/scoped_ptr.h"
#include "ui/events/event_constants.h"
#include "ui/events/keycodes/keyboard_codes.h"
#include "ui/events/x/device_data_manager.h"
#include "ui/gfx/point.h"
#include "ui/gfx/x/x11_types.h"

typedef union _XEvent XEvent;

namespace ui {

struct Valuator {
  Valuator(DeviceDataManager::DataType type, double v)
      : data_type(type), value(v) {}

  DeviceDataManager::DataType data_type;
  double value;
};

class ScopedXI2Event {
 public:
  ScopedXI2Event();
  ~ScopedXI2Event();

  operator XEvent*() { return event_.get(); }

  // Initializes a XEvent with for the appropriate type with the specified data.
  // Note that ui::EF_ flags should be passed as |flags|, not the native ones in
  // <X11/X.h>.
  void InitKeyEvent(EventType type,
                    KeyboardCode key_code,
                    int flags);

  void InitButtonEvent(EventType type,
                       int flags);

  void InitMouseWheelEvent(int wheel_delta,
                           int flags);

  void InitScrollEvent(int deviceid,
                       int x_offset,
                       int y_offset,
                       int x_offset_ordinal,
                       int y_offset_ordinal,
                       int finger_count);

  void InitTouchEvent(int deviceid,
                      int evtype,
                      int tracking_id,
                      const gfx::Point& location,
                      const std::vector<Valuator>& valuators);

 private:
  void Cleanup();

  scoped_ptr<XEvent> event_;

  DISALLOW_COPY_AND_ASSIGN(ScopedXI2Event);
};

// Initializes a test touchpad device for scroll events.
void SetUpScrollDeviceForTest(unsigned int deviceid);

// Initializes a list of touchscreen devices for touch events.
void SetupTouchDevicesForTest(const std::vector<unsigned int>& devices);

}  // namespace ui

#endif  // UI_EVENTS_TEST_EVENTS_TEST_UTILS_X11_H_
