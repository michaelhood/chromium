// Copyright 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "ui/base/ime/chromeos/ibus_bridge.h"

#include <map>
#include "base/logging.h"
#include "base/memory/singleton.h"

namespace chromeos {

static IBusBridge* g_ibus_bridge = NULL;

// An implementation of IBusBridge.
class IBusBridgeImpl : public IBusBridge {
 public:
  IBusBridgeImpl()
    : input_context_handler_(NULL),
      engine_handler_(NULL),
      candidate_window_handler_(NULL) {
  }

  virtual ~IBusBridgeImpl() {
  }

  // IBusBridge override.
  virtual IBusInputContextHandlerInterface*
      GetInputContextHandler() const OVERRIDE {
    return input_context_handler_;
  }

  // IBusBridge override.
  virtual void SetInputContextHandler(
      IBusInputContextHandlerInterface* handler) OVERRIDE {
    input_context_handler_ = handler;
  }

  // IBusBridge override.
  virtual IBusEngineHandlerInterface* GetEngineHandler() const OVERRIDE {
    return engine_handler_;
  }

  // IBusBridge override.
  virtual void SetEngineHandler(IBusEngineHandlerInterface* handler) OVERRIDE {
    engine_handler_ = handler;
  }

  // IBusBridge override.
  virtual IBusPanelCandidateWindowHandlerInterface*
  GetCandidateWindowHandler() const OVERRIDE {
    return candidate_window_handler_;
  }

  // IBusBridge override.
  virtual void SetCandidateWindowHandler(
      IBusPanelCandidateWindowHandlerInterface* handler) OVERRIDE {
    candidate_window_handler_ = handler;
  }

  virtual void SetCreateEngineHandler(
      const std::string& engine_id,
      const CreateEngineHandler& handler) OVERRIDE {
    create_engine_handler_map_[engine_id] = handler;
  }

  // IBusBridge override.
  virtual void UnsetCreateEngineHandler(const std::string& engine_id) OVERRIDE {
    create_engine_handler_map_.erase(engine_id);
  }

  // IBusBridge override.
  virtual void CreateEngine(const std::string& engine_id) OVERRIDE {
    // TODO(nona): Change following condition to DCHECK once all legacy IME is
    // migrated to extension IME.
    if (create_engine_handler_map_[engine_id].is_null())
      return;
    create_engine_handler_map_[engine_id].Run();
  }

 private:
  IBusInputContextHandlerInterface* input_context_handler_;
  IBusEngineHandlerInterface* engine_handler_;
  IBusPanelCandidateWindowHandlerInterface* candidate_window_handler_;
  std::map<std::string, CreateEngineHandler> create_engine_handler_map_;

  DISALLOW_COPY_AND_ASSIGN(IBusBridgeImpl);
};

///////////////////////////////////////////////////////////////////////////////
// IBusBridge
IBusBridge::IBusBridge() {
}

IBusBridge::~IBusBridge() {
}

// static.
void IBusBridge::Initialize() {
  if (!g_ibus_bridge)
    g_ibus_bridge = new IBusBridgeImpl();
}

// static.
void IBusBridge::Shutdown() {
  delete g_ibus_bridge;
  g_ibus_bridge = NULL;
}

// static.
IBusBridge* IBusBridge::Get() {
  return g_ibus_bridge;
}

}  // namespace chromeos
