// Copyright (c) 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/chromeos/login/oauth2_login_manager.h"

#include "base/command_line.h"
#include "base/metrics/histogram.h"
#include "base/prefs/pref_service.h"
#include "base/strings/string_util.h"
#include "chrome/browser/browser_process.h"
#include "chrome/browser/chromeos/login/user_manager.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/signin/profile_oauth2_token_service.h"
#include "chrome/browser/signin/profile_oauth2_token_service_factory.h"
#include "chrome/common/chrome_switches.h"
#include "chromeos/chromeos_switches.h"
#include "google_apis/gaia/gaia_constants.h"
#include "google_apis/gaia/gaia_urls.h"
#include "net/url_request/url_request_context_getter.h"

namespace chromeos {

static const char kServiceScopeGetUserInfo[] =
    "https://www.googleapis.com/auth/userinfo.email";
static const int kMaxRetries = 5;

OAuth2LoginManager::OAuth2LoginManager(Profile* user_profile)
    : user_profile_(user_profile),
      restore_strategy_(RESTORE_FROM_COOKIE_JAR),
      state_(SESSION_RESTORE_NOT_STARTED),
      loading_reported_(false) {
  GetTokenService()->AddObserver(this);
  if (CommandLine::ForCurrentProcess()->
          HasSwitch(chromeos::switches::kOobeSkipPostLogin)) {
    // For telemetry we should mark session restore completed to avoid
    // warnings from MergeSessionThrottle.
    SetSessionRestoreState(SESSION_RESTORE_DONE);
  }
}

OAuth2LoginManager::~OAuth2LoginManager() {
}

void OAuth2LoginManager::AddObserver(OAuth2LoginManager::Observer* observer) {
  observer_list_.AddObserver(observer);
}

void OAuth2LoginManager::RemoveObserver(
    OAuth2LoginManager::Observer* observer) {
  observer_list_.RemoveObserver(observer);
}

void OAuth2LoginManager::RestoreSession(
    net::URLRequestContextGetter* auth_request_context,
    SessionRestoreStrategy restore_strategy,
    const std::string& oauth2_refresh_token,
    const std::string& auth_code) {
  DCHECK(user_profile_);
  auth_request_context_ = auth_request_context;
  restore_strategy_ = restore_strategy;
  refresh_token_ = oauth2_refresh_token;
  auth_code_ = auth_code;
  session_restore_start_ = base::Time::Now();
  SetSessionRestoreState(OAuth2LoginManager::SESSION_RESTORE_PREPARING);
  ContinueSessionRestore();
}

void OAuth2LoginManager::ContinueSessionRestore() {
  if (restore_strategy_ == RESTORE_FROM_COOKIE_JAR ||
      restore_strategy_ == RESTORE_FROM_AUTH_CODE) {
    FetchOAuth2Tokens();
    return;
  }

  // Save passed OAuth2 refresh token.
  if (restore_strategy_ == RESTORE_FROM_PASSED_OAUTH2_REFRESH_TOKEN) {
    DCHECK(!refresh_token_.empty());
    restore_strategy_ = RESTORE_FROM_SAVED_OAUTH2_REFRESH_TOKEN;
    GetAccountIdOfRefreshToken(refresh_token_);
    return;
  }

  DCHECK(restore_strategy_ == RESTORE_FROM_SAVED_OAUTH2_REFRESH_TOKEN);
  GetTokenService()->LoadCredentials();
}

void OAuth2LoginManager::Stop() {
  oauth2_token_fetcher_.reset();
  login_verifier_.reset();
}

bool OAuth2LoginManager::ShouldBlockTabLoading() {
  return state_ == SESSION_RESTORE_PREPARING ||
         state_ == SESSION_RESTORE_IN_PROGRESS;
}

void OAuth2LoginManager::OnRefreshTokenAvailable(
    const std::string& account_id) {
  if (state_ == SESSION_RESTORE_NOT_STARTED)
    return;
  // TODO(fgorski): Once ProfileOAuth2TokenService supports multi-login, make
  // sure to restore session cookies in the context of the correct account_id.
  VLOG(1) << "OnRefreshTokenAvailable";
  // Do not validate tokens for supervised users, as they don't actually have
  // oauth2 token.
  if (UserManager::Get()->IsLoggedInAsLocallyManagedUser()) {
    LOG(WARNING) << "Logged in as managed user, skip token validation.";
    return;
  }
  RestoreSessionCookies();
}

ProfileOAuth2TokenService* OAuth2LoginManager::GetTokenService() {
  ProfileOAuth2TokenService* token_service =
      ProfileOAuth2TokenServiceFactory::GetForProfile(user_profile_);
  return token_service;
}

void OAuth2LoginManager::GetAccountIdOfRefreshToken(
    const std::string& refresh_token) {
  gaia::OAuthClientInfo client_info;
  GaiaUrls* gaia_urls = GaiaUrls::GetInstance();
  client_info.client_id = gaia_urls->oauth2_chrome_client_id();
  client_info.client_secret = gaia_urls->oauth2_chrome_client_secret();

  account_id_fetcher_.reset(new gaia::GaiaOAuthClient(
      auth_request_context_.get()));
  account_id_fetcher_->RefreshToken(client_info, refresh_token,
      std::vector<std::string>(1, kServiceScopeGetUserInfo), kMaxRetries,
      this);
}

void OAuth2LoginManager::OnRefreshTokenResponse(
    const std::string& access_token,
    int expires_in_seconds) {
  account_id_fetcher_->GetUserEmail(access_token, kMaxRetries, this);
}

void OAuth2LoginManager::OnGetUserEmailResponse(
    const std::string& user_email)  {
  DCHECK(!refresh_token_.empty());
  account_id_fetcher_.reset();
  GetTokenService()->UpdateCredentials(user_email, refresh_token_);

  FOR_EACH_OBSERVER(Observer, observer_list_,
                    OnNewRefreshTokenAvaiable(user_profile_));
}

void OAuth2LoginManager::OnOAuthError() {
  account_id_fetcher_.reset();
  LOG(ERROR) << "Account id fetch failed!";
  SetSessionRestoreState(OAuth2LoginManager::SESSION_RESTORE_FAILED);
}

void OAuth2LoginManager::OnNetworkError(int response_code) {
  account_id_fetcher_.reset();
  LOG(ERROR) << "Account id fetch failed! response_code=" << response_code;
  SetSessionRestoreState(OAuth2LoginManager::SESSION_RESTORE_FAILED);
}

void OAuth2LoginManager::FetchOAuth2Tokens() {
  DCHECK(auth_request_context_.get());
  // If we have authenticated cookie jar, get OAuth1 token first, then fetch
  // SID/LSID cookies through OAuthLogin call.
  if (restore_strategy_ == RESTORE_FROM_COOKIE_JAR) {
    oauth2_token_fetcher_.reset(
        new OAuth2TokenFetcher(this, auth_request_context_.get()));
    oauth2_token_fetcher_->StartExchangeFromCookies();
  } else if (restore_strategy_ == RESTORE_FROM_AUTH_CODE) {
    DCHECK(!auth_code_.empty());
    oauth2_token_fetcher_.reset(
        new OAuth2TokenFetcher(this,
                               g_browser_process->system_request_context()));
    oauth2_token_fetcher_->StartExchangeFromAuthCode(auth_code_);
  } else {
    NOTREACHED();
    SetSessionRestoreState(OAuth2LoginManager::SESSION_RESTORE_FAILED);
  }
}

void OAuth2LoginManager::OnOAuth2TokensAvailable(
    const GaiaAuthConsumer::ClientOAuthResult& oauth2_tokens) {
  VLOG(1) << "OAuth2 tokens fetched";
  DCHECK(refresh_token_.empty());
  refresh_token_.assign(oauth2_tokens.refresh_token);
  GetAccountIdOfRefreshToken(refresh_token_);
}

void OAuth2LoginManager::OnOAuth2TokensFetchFailed() {
  LOG(ERROR) << "OAuth2 tokens fetch failed!";
  UMA_HISTOGRAM_ENUMERATION("OAuth2Login.SessionRestore",
                            SESSION_RESTORE_TOKEN_FETCH_FAILED,
                            SESSION_RESTORE_COUNT);
  SetSessionRestoreState(OAuth2LoginManager::SESSION_RESTORE_FAILED);
}

void OAuth2LoginManager::RestoreSessionCookies() {
  DCHECK(!login_verifier_.get());
  SetSessionRestoreState(SESSION_RESTORE_IN_PROGRESS);
  login_verifier_.reset(
      new OAuth2LoginVerifier(this,
                              g_browser_process->system_request_context(),
                              user_profile_->GetRequestContext()));
  login_verifier_->VerifyProfileTokens(user_profile_);
}

void OAuth2LoginManager::Shutdown() {
  GetTokenService()->RemoveObserver(this);
  login_verifier_.reset();
  oauth2_token_fetcher_.reset();
}

void OAuth2LoginManager::OnOAuthLoginSuccess(
    const GaiaAuthConsumer::ClientLoginResult& gaia_credentials) {
  VLOG(1) << "OAuth2 refresh token successfully exchanged for GAIA token.";

  FOR_EACH_OBSERVER(Observer, observer_list_,
                    OnSessionAuthenticated(user_profile_));
}

void OAuth2LoginManager::OnOAuthLoginFailure(bool connection_error) {
  LOG(ERROR) << "OAuth2 refresh token verification failed!"
             << " connection_error: " << connection_error;
  UMA_HISTOGRAM_ENUMERATION("OAuth2Login.SessionRestore",
                            SESSION_RESTORE_OAUTHLOGIN_FAILED,
                            SESSION_RESTORE_COUNT);
  SetSessionRestoreState(connection_error ?
      OAuth2LoginManager::SESSION_RESTORE_CONNECTION_FAILED :
      OAuth2LoginManager::SESSION_RESTORE_FAILED);
}

void OAuth2LoginManager::OnSessionMergeSuccess() {
  VLOG(1) << "OAuth2 refresh and/or GAIA token verification succeeded.";
  UMA_HISTOGRAM_ENUMERATION("OAuth2Login.SessionRestore",
                            SESSION_RESTORE_SUCCESS,
                            SESSION_RESTORE_COUNT);
  SetSessionRestoreState(OAuth2LoginManager::SESSION_RESTORE_DONE);
}

void OAuth2LoginManager::OnSessionMergeFailure(bool connection_error) {
  LOG(ERROR) << "OAuth2 refresh and GAIA token verification failed!"
             << " connection_error: " << connection_error;
  UMA_HISTOGRAM_ENUMERATION("OAuth2Login.SessionRestore",
                            SESSION_RESTORE_MERGE_SESSION_FAILED,
                            SESSION_RESTORE_COUNT);
  SetSessionRestoreState(connection_error ?
      OAuth2LoginManager::SESSION_RESTORE_CONNECTION_FAILED :
      OAuth2LoginManager::SESSION_RESTORE_FAILED);
}

void OAuth2LoginManager::SetSessionRestoreState(
    OAuth2LoginManager::SessionRestoreState state) {
  if (state_ == state)
    return;

  state_ = state;
  if (state == OAuth2LoginManager::SESSION_RESTORE_FAILED) {
    UMA_HISTOGRAM_TIMES("OAuth2Login.SessionRestoreTimeToFailure",
                        base::Time::Now() - session_restore_start_);
  } else if (state == OAuth2LoginManager::SESSION_RESTORE_DONE) {
    UMA_HISTOGRAM_TIMES("OAuth2Login.SessionRestoreTimeToSuccess",
                        base::Time::Now() - session_restore_start_);
  }

  FOR_EACH_OBSERVER(Observer, observer_list_,
                    OnSessionRestoreStateChanged(user_profile_, state_));
}

void OAuth2LoginManager::SetSessionRestoreStartForTesting(
    const base::Time& time) {
  session_restore_start_ = time;
}

}  // namespace chromeos
