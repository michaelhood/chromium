// Copyright 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/ui/views/autofill/tooltip_icon.h"

#include "base/basictypes.h"
#include "base/timer/timer.h"
#include "chrome/browser/ui/views/autofill/info_bubble.h"
#include "grit/theme_resources.h"
#include "ui/base/resource/resource_bundle.h"
#include "ui/views/bubble/bubble_frame_view.h"
#include "ui/views/focus_border.h"
#include "ui/views/mouse_watcher_view_host.h"

namespace autofill {

namespace {

gfx::Insets GetPreferredInsets(views::View* view) {
  gfx::Size pref_size = view->GetPreferredSize();
  gfx::Rect local_bounds = view->GetLocalBounds();
  gfx::Point origin = local_bounds.CenterPoint();
  origin.Offset(-pref_size.width() / 2, -pref_size.height() / 2);
  return gfx::Insets(origin.y(),
                     origin.x(),
                     local_bounds.bottom() - (origin.y() + pref_size.height()),
                     local_bounds.right() - (origin.x() + pref_size.width()));
}

// An info bubble with some extra positioning magic for tooltip icons.
class TooltipBubble : public InfoBubble {
 public:
  TooltipBubble(views::View* anchor, const base::string16& message)
      : InfoBubble(anchor, message) {}
  virtual ~TooltipBubble() {}

 protected:
  // InfoBubble:
  virtual gfx::Rect GetAnchorRect() OVERRIDE {
    gfx::Rect bounds = views::BubbleDelegateView::GetAnchorRect();
    bounds.Inset(GetPreferredInsets(anchor()));
    return bounds;
  }

 private:
  DISALLOW_COPY_AND_ASSIGN(TooltipBubble);
};

}  // namespace

TooltipIcon::TooltipIcon(const base::string16& tooltip)
    : tooltip_(tooltip),
      mouse_inside_(false),
      bubble_(NULL) {
  ChangeImageTo(IDR_AUTOFILL_TOOLTIP_ICON);
  set_focusable(true);
}

TooltipIcon::~TooltipIcon() {
  HideBubble();
}

// static
const char TooltipIcon::kViewClassName[] = "autofill/TooltipIcon";

const char* TooltipIcon::GetClassName() const {
  return TooltipIcon::kViewClassName;
}

void TooltipIcon::OnMouseEntered(const ui::MouseEvent& event) {
  mouse_watcher_.reset();
  mouse_inside_ = true;
  show_timer_.Start(FROM_HERE, base::TimeDelta::FromMilliseconds(150), this,
                    &TooltipIcon::ShowBubble);
}

void TooltipIcon::OnMouseExited(const ui::MouseEvent& event) {
  show_timer_.Stop();

  if (!bubble_)
    return;

  scoped_ptr<views::MouseWatcherHost> host;
  views::View* frame = bubble_->GetWidget()->non_client_view()->frame_view();
  host.reset(new views::MouseWatcherViewHost(frame, gfx::Insets()));
  mouse_watcher_.reset(new views::MouseWatcher(host.release(), this));
  mouse_watcher_->Start();
}

void TooltipIcon::OnBoundsChanged(const gfx::Rect& prev_bounds) {
  gfx::Insets insets = GetPreferredInsets(this);
  set_focus_border(views::FocusBorder::CreateDashedFocusBorder(
      insets.left(), insets.top(), insets.right(), insets.bottom()));
}

void TooltipIcon::OnFocus() {
  ShowBubble();
}

void TooltipIcon::OnBlur() {
  HideBubble();
}

void TooltipIcon::MouseMovedOutOfHost() {
  mouse_inside_ = false;
  HideBubble();
}

void TooltipIcon::ChangeImageTo(int idr) {
  ui::ResourceBundle& rb = ui::ResourceBundle::GetSharedInstance();
  SetImage(rb.GetImageNamed(idr).ToImageSkia());
}

void TooltipIcon::ShowBubble() {
  DCHECK(mouse_inside_ || HasFocus());

  if (bubble_)
    return;

  ChangeImageTo(IDR_AUTOFILL_TOOLTIP_ICON_H);

  bubble_ = new TooltipBubble(this, tooltip_);
  bubble_->Show();
}

void TooltipIcon::HideBubble() {
  if (HasFocus() || mouse_inside_ || !bubble_)
    return;

  ChangeImageTo(IDR_AUTOFILL_TOOLTIP_ICON);

  bubble_->Hide();
  bubble_ = NULL;
}

}  // namespace autofill
