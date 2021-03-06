// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_CHROMEOS_INPUT_METHOD_INPUT_METHOD_ENGINE_IBUS_H_
#define CHROME_BROWSER_CHROMEOS_INPUT_METHOD_INPUT_METHOD_ENGINE_IBUS_H_

#include <map>
#include <string>
#include <vector>
#include "base/memory/weak_ptr.h"
#include "chrome/browser/chromeos/input_method/input_method_engine.h"
#include "ui/base/ime/chromeos/ibus_bridge.h"
#include "url/gurl.h"

namespace ui {
class KeyEvent;
}  // namespace ui

namespace chromeos {

class IBusText;

namespace input_method {
class CandidateWindow;
struct InputMethodProperty;
struct KeyEventHandle;
}  // namespace input_method

class InputMethodEngineIBus : public InputMethodEngine,
                              public IBusEngineHandlerInterface {
 public:
  InputMethodEngineIBus();

  virtual ~InputMethodEngineIBus();

  void Initialize(
      InputMethodEngine::Observer* observer,
      const char* engine_name,
      const char* extension_id,
      const char* engine_id,
      const char* description,
      const std::vector<std::string>& languages,
      const std::vector<std::string>& layouts,
      const GURL& options_page,
      const GURL& input_view,
      std::string* error);

  // InputMethodEngine overrides.
  virtual void StartIme() OVERRIDE;
  virtual bool SetComposition(int context_id,
                              const char* text,
                              int selection_start,
                              int selection_end,
                              int cursor,
                              const std::vector<SegmentInfo>& segments,
                              std::string* error) OVERRIDE;
  virtual bool ClearComposition(int context_id, std::string* error) OVERRIDE;
  virtual bool CommitText(int context_id, const char* text,
                          std::string* error) OVERRIDE;
  virtual const CandidateWindowProperty&
    GetCandidateWindowProperty() const OVERRIDE;
  virtual void SetCandidateWindowProperty(
      const CandidateWindowProperty& property) OVERRIDE;
  virtual bool SetCandidateWindowVisible(bool visible,
                                         std::string* error) OVERRIDE;
  virtual void SetCandidateWindowAuxText(const char* text) OVERRIDE;
  virtual void SetCandidateWindowAuxTextVisible(bool visible) OVERRIDE;
  virtual bool SetCandidates(int context_id,
                             const std::vector<Candidate>& candidates,
                             std::string* error) OVERRIDE;
  virtual bool SetCursorPosition(int context_id, int candidate_id,
                                 std::string* error) OVERRIDE;
  virtual bool SetMenuItems(const std::vector<MenuItem>& items) OVERRIDE;
  virtual bool UpdateMenuItems(const std::vector<MenuItem>& items) OVERRIDE;
  virtual bool IsActive() const OVERRIDE;
  virtual void KeyEventDone(input_method::KeyEventHandle* key_data,
                            bool handled) OVERRIDE;
  virtual bool DeleteSurroundingText(int context_id,
                                     int offset,
                                     size_t number_of_chars,
                                     std::string* error) OVERRIDE;

  // IBusEngineHandlerInterface overrides.
  virtual void FocusIn(
      const IBusEngineHandlerInterface::InputContext& input_context) OVERRIDE;
  virtual void FocusOut() OVERRIDE;
  virtual void Enable() OVERRIDE;
  virtual void Disable() OVERRIDE;
  virtual void PropertyActivate(const std::string& property_name) OVERRIDE;
  virtual void Reset() OVERRIDE;
  virtual void ProcessKeyEvent(const ui::KeyEvent& key_event,
                               const KeyEventDoneCallback& callback) OVERRIDE;
  virtual void CandidateClicked(uint32 index, ibus::IBusMouseButton button,
                                uint32 state) OVERRIDE;
  virtual void SetSurroundingText(const std::string& text, uint32 cursor_pos,
                                  uint32 anchor_pos) OVERRIDE;

 private:
  // Converts MenuItem to InputMethodProperty.
  void MenuItemToProperty(const MenuItem& item,
                          input_method::InputMethodProperty* property);

  // Registers the engine component.
  void RegisterComponent();

  // Called when the IBusBrige executes CreateEngine with
  // corresponding engine id.
  void CreateEngineHandler();

  // True if the current context has focus.
  bool focused_;

  // True if this engine is active.
  bool active_;

  // ID that is used for the current input context.  False if there is no focus.
  int context_id_;

  // Next id that will be assigned to a context.
  int next_context_id_;

  // This IME ID in Chrome Extension.
  std::string engine_id_;

  // This IME ID in ibus.
  std::string ibus_id_;

  // The current auxialy text and it's visiblity.
  scoped_ptr<IBusText> aux_text_;
  bool aux_text_visible_;

  // Pointer to the object recieving events for this IME.
  InputMethodEngine::Observer* observer_;

  // The current preedit text, and it's cursor position.
  scoped_ptr<IBusText> preedit_text_;
  int preedit_cursor_;

  // The current candidate window.
  scoped_ptr<input_method::CandidateWindow> candidate_window_;

  // The current candidate window property.
  CandidateWindowProperty candidate_window_property_;

  // Indicates whether the candidate window is visible.
  bool window_visible_;

  // Mapping of candidate index to candidate id.
  std::vector<int> candidate_ids_;

  // Mapping of candidate id to index.
  std::map<int, int> candidate_indexes_;

  // Used for making callbacks.
  base::WeakPtrFactory<InputMethodEngineIBus> weak_ptr_factory_;

  // Used for input view window.
  GURL input_view_url_;
};

}  // namespace chromeos

#endif  // CHROME_BROWSER_CHROMEOS_INPUT_METHOD_INPUT_METHOD_ENGINE_IBUS_H_
