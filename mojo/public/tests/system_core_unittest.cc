// Copyright 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// This file tests the C API.

#include "mojo/public/system/core.h"

#include <string.h>

#include "testing/gtest/include/gtest/gtest.h"

namespace mojo {
namespace {

TEST(SystemTest, Basic) {
  MojoHandle h_0;
  MojoWaitFlags wf;
  char buffer[10] = { 0 };
  uint32_t buffer_size;

  // The only handle that's guaranteed to be invalid is |MOJO_HANDLE_INVALID|.
  EXPECT_EQ(MOJO_RESULT_INVALID_ARGUMENT, MojoClose(MOJO_HANDLE_INVALID));
  EXPECT_EQ(MOJO_RESULT_INVALID_ARGUMENT,
            MojoWait(MOJO_HANDLE_INVALID, MOJO_WAIT_FLAG_EVERYTHING, 1000000));
  h_0 = MOJO_HANDLE_INVALID;
  wf = MOJO_WAIT_FLAG_EVERYTHING;
  EXPECT_EQ(MOJO_RESULT_INVALID_ARGUMENT,
            MojoWaitMany(&h_0, &wf, 1, MOJO_DEADLINE_INDEFINITE));
  EXPECT_EQ(MOJO_RESULT_INVALID_ARGUMENT,
            MojoWriteMessage(h_0,
                             buffer, 3,
                             NULL, 0,
                             MOJO_WRITE_MESSAGE_FLAG_NONE));
  buffer_size = static_cast<uint32_t>(sizeof(buffer));
  EXPECT_EQ(MOJO_RESULT_INVALID_ARGUMENT,
            MojoReadMessage(h_0,
                            buffer, &buffer_size,
                            NULL, NULL,
                            MOJO_READ_MESSAGE_FLAG_NONE));

  MojoHandle h_1;
  EXPECT_EQ(MOJO_RESULT_OK, MojoCreateMessagePipe(&h_0, &h_1));

  // Shouldn't be readable.
  EXPECT_EQ(MOJO_RESULT_DEADLINE_EXCEEDED,
            MojoWait(h_0, MOJO_WAIT_FLAG_READABLE, 0));

  // Should be writable.
  EXPECT_EQ(MOJO_RESULT_OK,
            MojoWait(h_0, MOJO_WAIT_FLAG_WRITABLE, 0));

  // Try to read.
  EXPECT_EQ(MOJO_RESULT_NOT_FOUND,
            MojoReadMessage(h_0,
                            buffer, &buffer_size,
                            NULL, NULL,
                            MOJO_READ_MESSAGE_FLAG_NONE));

  // Write to |h_1|.
  static const char hello[] = "hello";
  memcpy(buffer, hello, sizeof(hello));
  buffer_size = static_cast<uint32_t>(sizeof(hello));
  EXPECT_EQ(MOJO_RESULT_OK,
            MojoWriteMessage(h_1,
                             hello, buffer_size,
                             NULL, 0,
                             MOJO_WRITE_MESSAGE_FLAG_NONE));

  // |h_0| should be readable.
  wf = MOJO_WAIT_FLAG_READABLE;
  EXPECT_EQ(MOJO_RESULT_OK,
            MojoWaitMany(&h_0, &wf, 1, MOJO_DEADLINE_INDEFINITE));

  // Read from |h_0|.
  memset(buffer, 0, sizeof(buffer));
  buffer_size = static_cast<uint32_t>(sizeof(buffer));
  EXPECT_EQ(MOJO_RESULT_OK,
            MojoReadMessage(h_0,
                            buffer, &buffer_size,
                            NULL, NULL,
                            MOJO_READ_MESSAGE_FLAG_NONE));
  EXPECT_EQ(static_cast<uint32_t>(sizeof(hello)), buffer_size);
  EXPECT_EQ(0, memcmp(hello, buffer, sizeof(hello)));

  // |h_0| should no longer be readable.
  EXPECT_EQ(MOJO_RESULT_DEADLINE_EXCEEDED,
            MojoWait(h_0, MOJO_WAIT_FLAG_READABLE, 10));

  // Close |h_0|.
  EXPECT_EQ(MOJO_RESULT_OK, MojoClose(h_0));

  // |h_1| should no longer be readable or writable.
  EXPECT_EQ(MOJO_RESULT_FAILED_PRECONDITION,
            MojoWait(h_1,
                     MOJO_WAIT_FLAG_READABLE | MOJO_WAIT_FLAG_WRITABLE,
                     1000));

  EXPECT_EQ(MOJO_RESULT_OK, MojoClose(h_1));
}

// TODO(vtl): Add multi-threaded tests.

}  // namespace
}  // namespace mojo
