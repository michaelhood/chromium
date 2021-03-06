// Copyright 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

define([
    "console",
    "mojo/apps/js/threading",
    "mojo/public/bindings/js/connector",
    "mojom/hello_world_service",
], function(console, threading, connector, hello) {

  function HelloWorldClientImpl() {
  }

  HelloWorldClientImpl.prototype =
      Object.create(hello.HelloWorldClientStub.prototype);

  HelloWorldClientImpl.prototype.didReceiveGreeting = function(result) {
    console.log("DidReceiveGreeting from pipe: " + result);
    connection.close();
    threading.quit();
  };

  var connection = null;

  return function(handle) {
    connection = new connector.Connection(handle,
                                          HelloWorldClientImpl,
                                          hello.HelloWorldServiceProxy);

    connection.remote.greeting("hello, world!");
  };
});
