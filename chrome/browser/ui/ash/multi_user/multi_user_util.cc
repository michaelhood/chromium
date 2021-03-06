// Copyright 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/ui/ash/multi_user/multi_user_util.h"

#include <vector>

#include "chrome/browser/browser_process.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/profiles/profile_manager.h"
#include "google_apis/gaia/gaia_auth_util.h"

#if defined(OS_CHROMEOS)
#include "chrome/browser/chromeos/login/user_manager.h"
#endif

namespace multi_user_util {

std::string GetUserIDFromProfile(Profile* profile) {
  return GetUserIDFromEmail(profile->GetOriginalProfile()->GetProfileName());
}

std::string GetUserIDFromEmail(const std::string& email) {
  return gaia::CanonicalizeEmail(gaia::SanitizeEmail(email));
}

Profile* GetProfileFromUserID(const std::string& user_id) {
  // Unit tests can end up here without a |g_browser_process|.
  if (!g_browser_process || !g_browser_process->profile_manager())
    return NULL;

  std::vector<Profile*> profiles =
      g_browser_process->profile_manager()->GetLoadedProfiles();

  std::vector<Profile*>::const_iterator profile_iterator = profiles.begin();
  for (; profile_iterator != profiles.end(); ++profile_iterator) {
    if (GetUserIDFromProfile(*profile_iterator) == user_id)
      return *profile_iterator;
  }
  return NULL;
}

bool IsProfileFromActiveUser(Profile* profile) {
#if defined(OS_CHROMEOS)
  return GetUserIDFromProfile(profile) ==
         chromeos::UserManager::Get()->GetActiveUser()->email();
#else
  return profile->GetOriginalProfile() == ProfileManager::GetDefaultProfile();
#endif
}

}  // namespace multi_user_util
