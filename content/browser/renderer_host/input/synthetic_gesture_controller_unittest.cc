// Copyright 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "base/memory/scoped_ptr.h"
#include "base/time/time.h"
#include "content/browser/renderer_host/input/synthetic_gesture.h"
#include "content/browser/renderer_host/input/synthetic_gesture_controller.h"
#include "content/browser/renderer_host/input/synthetic_gesture_target.h"
#include "content/browser/renderer_host/input/synthetic_pinch_gesture.h"
#include "content/browser/renderer_host/input/synthetic_smooth_scroll_gesture.h"
#include "content/browser/renderer_host/render_widget_host_delegate.h"
#include "content/browser/renderer_host/test_render_view_host.h"
#include "content/common/input/input_event.h"
#include "content/common/input/synthetic_smooth_scroll_gesture_params.h"
#include "content/public/test/mock_render_process_host.h"
#include "content/public/test/test_browser_context.h"
#include "testing/gtest/include/gtest/gtest.h"
#include "third_party/WebKit/public/web/WebInputEvent.h"
#include "ui/gfx/point_f.h"

namespace content {

namespace {

const int kFlushInputRateInMs = 16;
const int kPointerAssumedStoppedTimeMs = 43;

class MockSyntheticGesture : public SyntheticGesture {
 public:
  MockSyntheticGesture(bool* finished, int num_steps)
      : finished_(finished),
        num_steps_(num_steps),
        step_count_(0) {
    *finished_ = false;
  }
  virtual ~MockSyntheticGesture() {}

  virtual Result ForwardInputEvents(const base::TimeDelta& interval,
                                    SyntheticGestureTarget* target) OVERRIDE {
    step_count_++;
    if (step_count_ == num_steps_) {
      *finished_ = true;
      return SyntheticGesture::GESTURE_FINISHED;
    } else if (step_count_ > num_steps_) {
      *finished_ = true;
      // Return arbitrary failure.
      return SyntheticGesture::GESTURE_SOURCE_TYPE_NOT_IMPLEMENTED;
    }

    return SyntheticGesture::GESTURE_RUNNING;
  }

 protected:
  bool* finished_;
  int num_steps_;
  int step_count_;
};

class MockSyntheticGestureTarget : public SyntheticGestureTarget {
 public:
  MockSyntheticGestureTarget()
      : num_success_(0),
        num_failure_(0),
        flush_requested_(false),
        pointer_assumed_stopped_time_ms_(kPointerAssumedStoppedTimeMs) {}
  virtual ~MockSyntheticGestureTarget() {}

  // SyntheticGestureTarget:
  virtual void DispatchInputEventToPlatform(const InputEvent& event) OVERRIDE {}

  virtual void OnSyntheticGestureCompleted(
      SyntheticGesture::Result result) OVERRIDE {
    DCHECK_NE(result, SyntheticGesture::GESTURE_RUNNING);
    if (result == SyntheticGesture::GESTURE_FINISHED)
      num_success_++;
    else
      num_failure_++;
  }

  virtual void SetNeedsFlush() OVERRIDE {
    flush_requested_ = true;
  }

  virtual SyntheticGestureParams::GestureSourceType
  GetDefaultSyntheticGestureSourceType() const OVERRIDE {
    return SyntheticGestureParams::TOUCH_INPUT;
  }
  virtual bool SupportsSyntheticGestureSourceType(
      SyntheticGestureParams::GestureSourceType gesture_source_type)
      const OVERRIDE {
    return true;
  }

  virtual base::TimeDelta PointerAssumedStoppedTime() const OVERRIDE {
    return base::TimeDelta::FromMilliseconds(pointer_assumed_stopped_time_ms_);
  }

  void set_pointer_assumed_stopped_time_ms(int time_ms) {
    pointer_assumed_stopped_time_ms_ = time_ms;
  }

  int num_success() const { return num_success_; }
  int num_failure() const { return num_failure_; }

  bool flush_requested() const { return flush_requested_; }
  void ClearFlushRequest() { flush_requested_ = false; }

 private:
  int num_success_;
  int num_failure_;

  bool flush_requested_;

  int pointer_assumed_stopped_time_ms_;
};

class MockSyntheticSmoothScrollMouseTarget : public MockSyntheticGestureTarget {
 public:
  MockSyntheticSmoothScrollMouseTarget() : scroll_distance_(0) {}
  virtual ~MockSyntheticSmoothScrollMouseTarget() {}

  virtual void DispatchInputEventToPlatform(const InputEvent& event) OVERRIDE {
    const blink::WebInputEvent* web_event = event.web_event.get();
    ASSERT_EQ(web_event->type, blink::WebInputEvent::MouseWheel);
    const blink::WebMouseWheelEvent* mouse_wheel_event =
        static_cast<const blink::WebMouseWheelEvent*>(web_event);
    ASSERT_EQ(mouse_wheel_event->deltaX, 0);
    scroll_distance_ -= mouse_wheel_event->deltaY;
  }

  float scroll_distance() const { return scroll_distance_; }

 private:
  float scroll_distance_;
};

class MockSyntheticSmoothScrollTouchTarget : public MockSyntheticGestureTarget {
 public:
  MockSyntheticSmoothScrollTouchTarget()
      : scroll_distance_(0), anchor_y_(0), started_(false) {}
  virtual ~MockSyntheticSmoothScrollTouchTarget() {}

  virtual void DispatchInputEventToPlatform(const InputEvent& event) OVERRIDE {
    const blink::WebInputEvent* web_event = event.web_event.get();
    ASSERT_TRUE(blink::WebInputEvent::isTouchEventType(web_event->type));
    const blink::WebTouchEvent* touch_event =
        static_cast<const blink::WebTouchEvent*>(web_event);
    ASSERT_EQ(touch_event->touchesLength, (unsigned int)1);

    if (!started_) {
      ASSERT_EQ(touch_event->type, blink::WebInputEvent::TouchStart);
      anchor_y_ = touch_event->touches[0].position.y;
      started_ = true;
    } else {
      ASSERT_NE(touch_event->type, blink::WebInputEvent::TouchStart);
      ASSERT_NE(touch_event->type, blink::WebInputEvent::TouchCancel);
      // Ignore move events.

      if (touch_event->type == blink::WebInputEvent::TouchEnd)
        scroll_distance_ = anchor_y_ - touch_event->touches[0].position.y;
    }
  }

  float scroll_distance() const { return scroll_distance_; }

 private:
  float scroll_distance_;
  float anchor_y_;
  bool started_;
};

class MockSyntheticPinchTouchTarget : public MockSyntheticGestureTarget {
 public:
  enum ZoomDirection {
    ZOOM_DIRECTION_UNKNOWN,
    ZOOM_IN,
    ZOOM_OUT
  };

  MockSyntheticPinchTouchTarget()
      : total_num_pixels_covered_(0),
        last_pointer_distance_(0),
        zoom_direction_(ZOOM_DIRECTION_UNKNOWN),
        started_(false) {}
  virtual ~MockSyntheticPinchTouchTarget() {}

  virtual void DispatchInputEventToPlatform(const InputEvent& event) OVERRIDE {
    const blink::WebInputEvent* web_event = event.web_event.get();
    ASSERT_TRUE(blink::WebInputEvent::isTouchEventType(web_event->type));
    const blink::WebTouchEvent* touch_event =
        static_cast<const blink::WebTouchEvent*>(web_event);
    ASSERT_EQ(touch_event->touchesLength, (unsigned int)2);

    if (!started_) {
      ASSERT_EQ(touch_event->type, blink::WebInputEvent::TouchStart);

      start_0_ = gfx::Point(touch_event->touches[0].position);
      start_1_ = gfx::Point(touch_event->touches[1].position);
      last_pointer_distance_ = (start_0_ - start_1_).Length();

      started_ = true;
    } else {
      ASSERT_NE(touch_event->type, blink::WebInputEvent::TouchStart);
      ASSERT_NE(touch_event->type, blink::WebInputEvent::TouchCancel);

      gfx::PointF current_0 = gfx::Point(touch_event->touches[0].position);
      gfx::PointF current_1 = gfx::Point(touch_event->touches[1].position);

      total_num_pixels_covered_ =
          (current_0 - start_0_).Length() + (current_1 - start_1_).Length();
      float pointer_distance = (current_0 - current_1).Length();

      if (last_pointer_distance_ != pointer_distance) {
        if (zoom_direction_ == ZOOM_DIRECTION_UNKNOWN)
          zoom_direction_ =
              ComputeZoomDirection(last_pointer_distance_, pointer_distance);
        else
          EXPECT_EQ(
              zoom_direction_,
              ComputeZoomDirection(last_pointer_distance_, pointer_distance));
      }

      last_pointer_distance_ = pointer_distance;
    }
  }

  float total_num_pixels_covered() const { return total_num_pixels_covered_; }
  ZoomDirection zoom_direction() const { return zoom_direction_; }

 private:
  ZoomDirection ComputeZoomDirection(float last_pointer_distance,
                                     float current_pointer_distance) {
    DCHECK_NE(last_pointer_distance, current_pointer_distance);
    return last_pointer_distance < current_pointer_distance ? ZOOM_IN
                                                            : ZOOM_OUT;
  }

  float total_num_pixels_covered_;
  float last_pointer_distance_;
  ZoomDirection zoom_direction_;
  gfx::PointF start_0_;
  gfx::PointF start_1_;
  bool started_;
};

class SyntheticGestureControllerTest : public testing::Test {
 public:
  SyntheticGestureControllerTest() {}
  virtual ~SyntheticGestureControllerTest() {}

 protected:
  template<typename MockGestureTarget>
  void CreateControllerAndTarget() {
    target_ = new MockGestureTarget();

    controller_.reset(new SyntheticGestureController(
        scoped_ptr<SyntheticGestureTarget>(target_)));
  }

  virtual void SetUp() OVERRIDE {
    start_time_ = base::TimeTicks::Now();
    time_ = start_time_;
  }

  virtual void TearDown() OVERRIDE {
    controller_.reset();
    target_ = NULL;
    time_ = base::TimeTicks();
  }

  void FlushInputUntilComplete() {
    while (target_->flush_requested()) {
      target_->ClearFlushRequest();
      time_ += base::TimeDelta::FromMilliseconds(kFlushInputRateInMs);
      controller_->Flush(time_);
    }
  }

  base::TimeDelta GetTotalTime() const { return time_ - start_time_; }

  MockSyntheticGestureTarget* target_;
  scoped_ptr<SyntheticGestureController> controller_;
  base::TimeTicks start_time_;
  base::TimeTicks time_;
};

TEST_F(SyntheticGestureControllerTest, SingleGesture) {
  CreateControllerAndTarget<MockSyntheticGestureTarget>();

  bool finished;
  scoped_ptr<MockSyntheticGesture> gesture(
      new MockSyntheticGesture(&finished, 3));
  controller_->QueueSyntheticGesture(gesture.PassAs<SyntheticGesture>());
  FlushInputUntilComplete();

  EXPECT_TRUE(finished);
  EXPECT_EQ(1, target_->num_success());
  EXPECT_EQ(0, target_->num_failure());
}

TEST_F(SyntheticGestureControllerTest, GestureFailed) {
  CreateControllerAndTarget<MockSyntheticGestureTarget>();

  bool finished;
  scoped_ptr<MockSyntheticGesture> gesture(
      new MockSyntheticGesture(&finished, 0));
  controller_->QueueSyntheticGesture(gesture.PassAs<SyntheticGesture>());
  FlushInputUntilComplete();

  EXPECT_TRUE(finished);
  EXPECT_EQ(1, target_->num_failure());
  EXPECT_EQ(0, target_->num_success());
}

TEST_F(SyntheticGestureControllerTest, SuccessiveGestures) {
  CreateControllerAndTarget<MockSyntheticGestureTarget>();

  bool finished_1, finished_2;
  scoped_ptr<MockSyntheticGesture> gesture_1(
      new MockSyntheticGesture(&finished_1, 2));
  scoped_ptr<MockSyntheticGesture> gesture_2(
      new MockSyntheticGesture(&finished_2, 4));

  // Queue first gesture and wait for it to finish
  controller_->QueueSyntheticGesture(gesture_1.PassAs<SyntheticGesture>());
  FlushInputUntilComplete();

  EXPECT_TRUE(finished_1);
  EXPECT_EQ(1, target_->num_success());
  EXPECT_EQ(0, target_->num_failure());

  // Queue second gesture.
  controller_->QueueSyntheticGesture(gesture_2.PassAs<SyntheticGesture>());
  FlushInputUntilComplete();

  EXPECT_TRUE(finished_2);
  EXPECT_EQ(2, target_->num_success());
  EXPECT_EQ(0, target_->num_failure());
}

TEST_F(SyntheticGestureControllerTest, TwoGesturesInFlight) {
  CreateControllerAndTarget<MockSyntheticGestureTarget>();

  bool finished_1, finished_2;
  scoped_ptr<MockSyntheticGesture> gesture_1(
      new MockSyntheticGesture(&finished_1, 2));
  scoped_ptr<MockSyntheticGesture> gesture_2(
      new MockSyntheticGesture(&finished_2, 4));

  controller_->QueueSyntheticGesture(gesture_1.PassAs<SyntheticGesture>());
  controller_->QueueSyntheticGesture(gesture_2.PassAs<SyntheticGesture>());
  FlushInputUntilComplete();

  EXPECT_TRUE(finished_1);
  EXPECT_TRUE(finished_2);

  EXPECT_EQ(2, target_->num_success());
  EXPECT_EQ(0, target_->num_failure());
}

TEST_F(SyntheticGestureControllerTest, SmoothScrollGestureTouch) {
  CreateControllerAndTarget<MockSyntheticSmoothScrollTouchTarget>();

  SyntheticSmoothScrollGestureParams params;
  params.gesture_source_type = SyntheticGestureParams::TOUCH_INPUT;
  params.distance = 123;

  scoped_ptr<SyntheticSmoothScrollGesture> gesture(
      new SyntheticSmoothScrollGesture(params));
  controller_->QueueSyntheticGesture(gesture.PassAs<SyntheticGesture>());
  FlushInputUntilComplete();

  EXPECT_EQ(1, target_->num_success());
  EXPECT_EQ(0, target_->num_failure());
  EXPECT_FLOAT_EQ(params.distance,
                  static_cast<MockSyntheticSmoothScrollTouchTarget*>(target_)
                      ->scroll_distance());
}

TEST_F(SyntheticGestureControllerTest, SmoothScrollGestureTouchLongStop) {
  CreateControllerAndTarget<MockSyntheticSmoothScrollTouchTarget>();

  // Create a smooth scroll with a short distance and set the pointer assumed
  // stopped time high, so that the stopping should dominate the time the
  // gesture is active.
  SyntheticSmoothScrollGestureParams params;
  params.gesture_source_type = SyntheticGestureParams::TOUCH_INPUT;
  params.distance = 21;

  target_->set_pointer_assumed_stopped_time_ms(543);

  scoped_ptr<SyntheticSmoothScrollGesture> gesture(
      new SyntheticSmoothScrollGesture(params));
  controller_->QueueSyntheticGesture(gesture.PassAs<SyntheticGesture>());
  FlushInputUntilComplete();

  EXPECT_EQ(1, target_->num_success());
  EXPECT_EQ(0, target_->num_failure());
  EXPECT_FLOAT_EQ(params.distance,
                  static_cast<MockSyntheticSmoothScrollTouchTarget*>(target_)
                      ->scroll_distance());
  EXPECT_GE(GetTotalTime(), target_->PointerAssumedStoppedTime());
}

TEST_F(SyntheticGestureControllerTest, SmoothScrollGestureMouse) {
  CreateControllerAndTarget<MockSyntheticSmoothScrollMouseTarget>();

  SyntheticSmoothScrollGestureParams params;
  params.gesture_source_type = SyntheticGestureParams::MOUSE_INPUT;
  params.distance = -234;

  scoped_ptr<SyntheticSmoothScrollGesture> gesture(
      new SyntheticSmoothScrollGesture(params));
  controller_->QueueSyntheticGesture(gesture.PassAs<SyntheticGesture>());
  FlushInputUntilComplete();

  EXPECT_EQ(1, target_->num_success());
  EXPECT_EQ(0, target_->num_failure());
  EXPECT_FLOAT_EQ(params.distance,
                  static_cast<MockSyntheticSmoothScrollTouchTarget*>(target_)
                      ->scroll_distance());
}

TEST_F(SyntheticGestureControllerTest, PinchGestureTouchZoomIn) {
  CreateControllerAndTarget<MockSyntheticPinchTouchTarget>();

  SyntheticPinchGestureParams params;
  params.gesture_source_type = SyntheticGestureParams::TOUCH_INPUT;
  params.zoom_in = true;
  params.total_num_pixels_covered = 345;

  scoped_ptr<SyntheticPinchGesture> gesture(new SyntheticPinchGesture(params));
  controller_->QueueSyntheticGesture(gesture.PassAs<SyntheticGesture>());
  FlushInputUntilComplete();

  EXPECT_EQ(1, target_->num_success());
  EXPECT_EQ(0, target_->num_failure());
  EXPECT_EQ(
      static_cast<MockSyntheticPinchTouchTarget*>(target_)->zoom_direction(),
      MockSyntheticPinchTouchTarget::ZOOM_IN);
  EXPECT_FLOAT_EQ(params.total_num_pixels_covered,
                  static_cast<MockSyntheticPinchTouchTarget*>(target_)
                      ->total_num_pixels_covered());
}

TEST_F(SyntheticGestureControllerTest, PinchGestureTouchZoomOut) {
  CreateControllerAndTarget<MockSyntheticPinchTouchTarget>();

  SyntheticPinchGestureParams params;
  params.gesture_source_type = SyntheticGestureParams::TOUCH_INPUT;
  params.zoom_in = false;
  params.total_num_pixels_covered = 456;

  scoped_ptr<SyntheticPinchGesture> gesture(new SyntheticPinchGesture(params));
  controller_->QueueSyntheticGesture(gesture.PassAs<SyntheticGesture>());
  FlushInputUntilComplete();

  EXPECT_EQ(1, target_->num_success());
  EXPECT_EQ(0, target_->num_failure());
  EXPECT_EQ(
      static_cast<MockSyntheticPinchTouchTarget*>(target_)->zoom_direction(),
      MockSyntheticPinchTouchTarget::ZOOM_OUT);
  EXPECT_FLOAT_EQ(params.total_num_pixels_covered,
                  static_cast<MockSyntheticPinchTouchTarget*>(target_)
                      ->total_num_pixels_covered());
}

}  // namespace

}  // namespace content
