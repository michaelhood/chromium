// Copyright 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "components/wifi/wifi_service.h"

#include <iphlpapi.h>
#include <objbase.h>
#include <wlanapi.h>

#include <set>

#include "base/base_paths_win.h"
#include "base/bind.h"
#include "base/files/file_path.h"
#include "base/memory/ref_counted.h"
#include "base/message_loop/message_loop.h"
#include "base/path_service.h"
#include "base/strings/string16.h"
#include "base/strings/string_util.h"
#include "base/strings/utf_string_conversions.h"
#include "base/win/registry.h"
#include "components/onc/onc_constants.h"

namespace {
const char kWiFiServiceError[] = "Error.WiFiService";
const wchar_t kNwCategoryWizardRegKey[] =
    L"Software\\Microsoft\\Windows NT\\CurrentVersion\\Network\\"
    L"NwCategoryWizard";
const wchar_t kNwCategoryWizardRegValue[] = L"Show";
const wchar_t kNwCategoryWizardSavedRegValue[] = L"ShowSaved";
const wchar_t kNwCategoryWizardDeleteRegValue[] = L"ShowDelete";
const wchar_t kWlanApiDll[] = L"wlanapi.dll";

// WlanApi function names
const char kWlanConnect[] = "WlanConnect";
const char kWlanCloseHandle[] = "WlanCloseHandle";
const char kWlanDisconnect[] = "WlanDisconnect";
const char kWlanEnumInterfaces[] = "WlanEnumInterfaces";
const char kWlanFreeMemory[] = "WlanFreeMemory";
const char kWlanGetAvailableNetworkList[] = "WlanGetAvailableNetworkList";
const char kWlanGetNetworkBssList[] = "WlanGetNetworkBssList";
const char kWlanGetProfile[] = "WlanGetProfile";
const char kWlanOpenHandle[] = "WlanOpenHandle";
const char kWlanRegisterNotification[] = "WlanRegisterNotification";
const char kWlanSaveTemporaryProfile[] = "WlanSaveTemporaryProfile";
const char kWlanScan[] = "WlanScan";

// WlanApi function definitions
typedef DWORD (WINAPI* WlanConnectFunction)(
    HANDLE hClientHandle,
    CONST GUID *pInterfaceGuid,
    CONST PWLAN_CONNECTION_PARAMETERS pConnectionParameters,
    PVOID pReserved);

typedef DWORD (WINAPI* WlanCloseHandleFunction)(
    HANDLE hClientHandle,
    PVOID pReserved);

typedef DWORD (WINAPI* WlanDisconnectFunction)(
    HANDLE hClientHandle,
    CONST GUID *pInterfaceGuid,
    PVOID pReserved);

typedef DWORD (WINAPI* WlanEnumInterfacesFunction)(
    HANDLE hClientHandle,
    PVOID pReserved,
    PWLAN_INTERFACE_INFO_LIST *ppInterfaceList);

typedef VOID (WINAPI* WlanFreeMemoryFunction)(
    _In_ PVOID pMemory);

typedef DWORD (WINAPI* WlanGetAvailableNetworkListFunction)(
    HANDLE hClientHandle,
    CONST GUID *pInterfaceGuid,
    DWORD dwFlags,
    PVOID pReserved,
    PWLAN_AVAILABLE_NETWORK_LIST *ppAvailableNetworkList);

typedef DWORD (WINAPI* WlanGetNetworkBssListFunction)(
    HANDLE hClientHandle,
    const GUID* pInterfaceGuid,
    const  PDOT11_SSID pDot11Ssid,
    DOT11_BSS_TYPE dot11BssType,
    BOOL bSecurityEnabled,
    PVOID pReserved,
    PWLAN_BSS_LIST* ppWlanBssList);

typedef DWORD (WINAPI* WlanGetProfileFunction)(
    HANDLE hClientHandle,
    CONST GUID *pInterfaceGuid,
    LPCWSTR strProfileName,
    PVOID pReserved,
    LPWSTR *pstrProfileXml,
    DWORD *pdwFlags,
    DWORD *pdwGrantedAccess);

typedef DWORD (WINAPI* WlanOpenHandleFunction)(
    DWORD dwClientVersion,
    PVOID pReserved,
    PDWORD pdwNegotiatedVersion,
    PHANDLE phClientHandle);

typedef DWORD (WINAPI* WlanRegisterNotificationFunction)(
    HANDLE hClientHandle,
    DWORD dwNotifSource,
    BOOL bIgnoreDuplicate,
    WLAN_NOTIFICATION_CALLBACK funcCallback,
    PVOID pCallbackContext,
    PVOID pReserved,
    PDWORD pdwPrevNotifSource);

typedef DWORD (WINAPI* WlanSaveTemporaryProfileFunction)(
    HANDLE hClientHandle,
    CONST GUID* pInterfaceGuid,
    LPCWSTR strProfileName,
    LPCWSTR strAllUserProfileSecurity,
    DWORD dwFlags,
    BOOL bOverWrite,
    PVOID pReserved);

typedef DWORD (WINAPI* WlanScanFunction)(
    HANDLE hClientHandle,
    CONST GUID *pInterfaceGuid,
    CONST PDOT11_SSID pDot11Ssid,
    CONST PWLAN_RAW_DATA pIeData,
    PVOID pReserved);

}  // namespace

namespace wifi {

// Implementation of WiFiService for Windows.
class WiFiServiceImpl : public WiFiService, base::NonThreadSafe {
 public:
  WiFiServiceImpl();
  virtual ~WiFiServiceImpl();

  // WiFiService interface implementation.
  virtual void Initialize(
      scoped_refptr<base::SequencedTaskRunner> task_runner) OVERRIDE;

  virtual void UnInitialize() OVERRIDE;

  // Get Properties of network identified by |network_guid|. Populates
  // |properties| on success, |error| on failure.
  virtual void GetProperties(const std::string& network_guid,
                             DictionaryValue* properties,
                             std::string* error) OVERRIDE;

  // Set Properties of network identified by |network_guid|. Populates |error|
  // on failure.
  virtual void SetProperties(const std::string& network_guid,
                             scoped_ptr<base::DictionaryValue> properties,
                             std::string* error) OVERRIDE;

  // Get list of visible networks of |network_type| (one of onc::network_type).
  // Populates |network_list| on success.
  virtual void GetVisibleNetworks(const std::string& network_type,
                                   ListValue* network_list) OVERRIDE;

  // Request network scan. Send |NetworkListChanged| event on completion.
  virtual void RequestNetworkScan() OVERRIDE;

  // Start connect to network identified by |network_guid|. Populates |error|
  // on failure.
  virtual void StartConnect(const std::string& network_guid,
                            std::string* error) OVERRIDE;

  // Start disconnect from network identified by |network_guid|. Populates
  // |error| on failure.
  virtual void StartDisconnect(const std::string& network_guid,
                               std::string* error) OVERRIDE;

  // Set observers to run when |NetworksChanged| and |NetworksListChanged|
  // events needs to be sent. Notifications are posted on |message_loop_proxy|.
  virtual void SetEventObservers(
      scoped_refptr<base::MessageLoopProxy> message_loop_proxy,
      const NetworkGuidListCallback& networks_changed_observer,
      const NetworkGuidListCallback& network_list_changed_observer) OVERRIDE;

 private:
  // Static callback for Windows WLAN_NOTIFICATION. Calls OnWlanNotification
  // on WiFiServiceImpl passed back as |context|.
  static void __stdcall OnWlanNotificationCallback(
      PWLAN_NOTIFICATION_DATA wlan_notification_data,
      PVOID context);

  // Callback for Windows WLAN_NOTIFICATION. Called on random thread from
  // OnWlanNotificationCallback. Handles network connectivity and scan complete
  // notification and posts tasks to main thread.
  void OnWlanNotification(PWLAN_NOTIFICATION_DATA wlan_notification_data);

  // Handles NetworkScanComplete notification on main thread. Sends
  // |NetworkListChanged| event with new list of visible networks.
  void OnNetworkScanCompleteOnMainThread();

  // Wait up to |kMaxAttempts| with |kAttemptDelayMs| delay for connection
  // to network with |network_guid|. Reset DHCP and Notify that |NetworkChanged|
  // upon success.
  void WaitForNetworkConnect(const std::string& network_guid, int attempt);

  // Check |error_code| and if is not |ERROR_SUCCESS|, then store |error_name|
  // into |error|.
  bool CheckError(DWORD error_code,
                  const std::string& error_name,
                  std::string* error) const;

  // Return |iterator| to network identified by |network_guid| in |networks|
  // list.
  NetworkList::iterator FindNetwork(NetworkList& networks,
                                    const std::string& network_guid);

  // Save currently connected network profile and return its
  // |connected_network_guid|, so it can be re-connected later.
  DWORD SaveCurrentConnectedNetwork(std::string* connected_network_guid);

  // Sort networks, so connected/connecting is up front, then by type:
  // Ethernet, WiFi, Cellular, VPN
  static void SortNetworks(NetworkList* networks);

  // Open a WLAN client handle, register for WLAN notifications.
  DWORD OpenClientHandle();

  // Reset DHCP on wireless network to work around an issue when Windows
  // takes forever to connect to the network, e.g. after Chromecast
  // device reset.
  DWORD ResetDHCP();

  // Find |adapter_index_map| by |interface_guid| for DHCP reset.
  DWORD FindAdapterIndexMapByGUID(const GUID& interface_guid,
                                  IP_ADAPTER_INDEX_MAP* adapter_index_map);

  // Avoid the network location wizard to pop up when network is connected.
  // Preserve current value in |saved_nw_category_wizard_|.
  DWORD DisableNwCategoryWizard();

  // Restore network location wizard to value saved by DisableNwCategoryWizard.
  DWORD RestoreNwCategoryWizard();

  // Ensure that |client_| handle is initialized.
  DWORD EnsureInitialized();

  // Close |client_| handle if it is open.
  DWORD CloseClientHandle();

  // Get |profile_name| from unique |network_guid|.
  base::string16 ProfileNameFromGUID(const std::string& network_guid) const {
    return base::UTF8ToUTF16(network_guid);
  }

  // Get |dot11_ssid| from unique |network_guid|.
  DOT11_SSID SSIDFromGUID(const std::string& network_guid) const;

  // Get unique |network_guid| string based on |dot11_ssid|.
  std::string GUIDFromSSID(const DOT11_SSID& dot11_ssid) const {
    return std::string(reinterpret_cast<const char*>(dot11_ssid.ucSSID),
                       dot11_ssid.uSSIDLength);
  }

  // Get network |ssid| string based on |wlan|.
  std::string SSIDFromWLAN(const WLAN_AVAILABLE_NETWORK& wlan) const {
    return GUIDFromSSID(wlan.dot11Ssid);
  }

  // Get unique |network_guid| string based on |wlan|.
  std::string GUIDFromWLAN(const WLAN_AVAILABLE_NETWORK& wlan) const {
    return SSIDFromWLAN(wlan);
  }

  // Deduce |onc::wifi| security from |alg|.
  std::string SecurityFromDot11AuthAlg(DOT11_AUTH_ALGORITHM alg) const;

  // Populate |properties| based on |wlan| and its corresponding bss info from
  // |wlan_bss_list|.
  void NetworkPropertiesFromAvailableNetwork(const WLAN_AVAILABLE_NETWORK& wlan,
                                             const WLAN_BSS_LIST& wlan_bss_list,
                                             NetworkProperties* properties);

  // Get the list of visible wireless networks.
  DWORD GetVisibleNetworkList(NetworkList* network_list);

  // Find currently connected network if any. Populate |connected_network_guid|
  // on success.
  DWORD FindConnectedNetwork(std::string* connected_network_guid);

  // Connect to network |network_guid| using previosly stored profile if exists,
  // or just network sid. If |frequency| is not |kFrequencyUnknown| then
  // connects only to BSS which uses that frequency and returns
  // |ERROR_NOT_FOUND| if such BSS cannot be found.
  DWORD Connect(const std::string& network_guid, Frequency frequency);

  // Disconnect from currently connected network if any.
  DWORD Disconnect();

  // Get DOT11_BSSID_LIST of desired BSSIDs to connect to |ssid| network on
  // given |frequency|.
  DWORD GetDesiredBssList(DOT11_SSID& ssid,
                          Frequency frequency,
                          scoped_ptr<DOT11_BSSID_LIST>* desired_list);

  // Save temporary wireless profile for |network_guid|.
  DWORD SaveTempProfile(const std::string& network_guid);

  // Get previously stored |profile_xml| for |network_guid|.
  DWORD GetProfile(const std::string& network_guid, std::string* profile_xml);

  // Return true if there is previously stored profile xml for |network_guid|.
  bool HaveProfile(const std::string& network_guid);

  // Notify |network_list_changed_observer_| that list of visible networks has
  // changed to |networks|.
  void NotifyNetworkListChanged(const NetworkList& networks);

  // Notify |networks_changed_observer_| that network |network_guid| status has
  // changed.
  void NotifyNetworkChanged(const std::string& network_guid);

  // Load WlanApi.dll from SystemDirectory and get Api function pointers.
  DWORD LoadWlanLibrary();
  // Instance of WlanApi.dll.
  HINSTANCE wlan_api_library_;
  // WlanApi function pointers
  WlanConnectFunction WlanConnect_function_;
  WlanCloseHandleFunction WlanCloseHandle_function_;
  WlanDisconnectFunction WlanDisconnect_function_;
  WlanEnumInterfacesFunction WlanEnumInterfaces_function_;
  WlanFreeMemoryFunction WlanFreeMemory_function_;
  WlanGetAvailableNetworkListFunction WlanGetAvailableNetworkList_function_;
  // WlanGetNetworkBssList function may not be avaiable on Windows XP.
  WlanGetNetworkBssListFunction WlanGetNetworkBssList_function_;
  WlanGetProfileFunction WlanGetProfile_function_;
  WlanOpenHandleFunction WlanOpenHandle_function_;
  WlanRegisterNotificationFunction WlanRegisterNotification_function_;
  WlanScanFunction WlanScan_function_;
  // WlanSaveTemporaryProfile function may not be avaiable on Windows XP.
  WlanSaveTemporaryProfileFunction WlanSaveTemporaryProfile_function_;

  // WLAN service handle.
  HANDLE client_;
  // GUID of the currently connected interface, if any, otherwise the GUID of
  // one of the WLAN interfaces.
  GUID interface_guid_;
  // Preserved WLAN profile xml.
  std::map<std::string, std::string> saved_profiles_xml_;
  // Observer to get notified when network(s) have changed (e.g. connect).
  NetworkGuidListCallback networks_changed_observer_;
  // Observer to get notified when network list has changed (scan complete).
  NetworkGuidListCallback network_list_changed_observer_;
  // Saved value of network location wizard show value.
  scoped_ptr<DWORD> saved_nw_category_wizard_;
  // MessageLoopProxy to post events on UI thread.
  scoped_refptr<base::MessageLoopProxy> message_loop_proxy_;
  // Task runner for worker tasks.
  scoped_refptr<base::SequencedTaskRunner> task_runner_;
  // If |false|, then |networks_changed_observer_| is not notified.
  bool enable_notify_network_changed_;
  // Number of attempts to check that network has connected successfully.
  static const int kMaxAttempts = 100;
  // Delay between attempts to check that network has connected successfully.
  static const int kAttemptDelayMs = 100;
  DISALLOW_COPY_AND_ASSIGN(WiFiServiceImpl);
};

WiFiServiceImpl::WiFiServiceImpl()
    : wlan_api_library_(NULL),
      WlanConnect_function_(NULL),
      WlanCloseHandle_function_(NULL),
      WlanDisconnect_function_(NULL),
      WlanEnumInterfaces_function_(NULL),
      WlanFreeMemory_function_(NULL),
      WlanGetAvailableNetworkList_function_(NULL),
      WlanGetNetworkBssList_function_(NULL),
      WlanGetProfile_function_(NULL),
      WlanOpenHandle_function_(NULL),
      WlanRegisterNotification_function_(NULL),
      WlanSaveTemporaryProfile_function_(NULL),
      WlanScan_function_(NULL),
      client_(NULL),
      enable_notify_network_changed_(true) {}

WiFiServiceImpl::~WiFiServiceImpl() { UnInitialize(); }

void WiFiServiceImpl::Initialize(
    scoped_refptr<base::SequencedTaskRunner> task_runner) {
  DCHECK(!client_);
  task_runner_.swap(task_runner);
  // Restore NwCategoryWizard in case if we crashed during connect.
  RestoreNwCategoryWizard();
  OpenClientHandle();
}

void WiFiServiceImpl::UnInitialize() {
  CloseClientHandle();
}

void WiFiServiceImpl::GetProperties(const std::string& network_guid,
                                    DictionaryValue* properties,
                                    std::string* error) {
  DWORD error_code = EnsureInitialized();
  if (error_code == ERROR_SUCCESS) {
    NetworkList network_list;
    error_code = GetVisibleNetworkList(&network_list);
    if (error_code == ERROR_SUCCESS && !network_list.empty()) {
      NetworkList::const_iterator it = FindNetwork(network_list, network_guid);
      if (it != network_list.end()) {
        DVLOG(1) << "Get Properties: " << network_guid << ":"
                   << it->connection_state;
        properties->Swap(it->ToValue(false).get());
        return;
      } else {
        error_code = ERROR_NOT_FOUND;
      }
    }
  }

  CheckError(error_code, kWiFiServiceError, error);
}

void WiFiServiceImpl::SetProperties(
    const std::string& network_guid,
    scoped_ptr<base::DictionaryValue> properties,
    std::string* error) {
  // This method is not implemented in first version as it is not used by
  // Google Cast extension.
  CheckError(ERROR_CALL_NOT_IMPLEMENTED, kWiFiServiceError, error);
}

void WiFiServiceImpl::GetVisibleNetworks(const std::string& network_type,
                                         ListValue* network_list) {
  if (!network_type.empty() &&
      network_type != onc::network_type::kAllTypes &&
      network_type != onc::network_type::kWiFi) {
    return;
  }

  DWORD error = EnsureInitialized();
  if (error == ERROR_SUCCESS) {
    NetworkList networks;
    error = GetVisibleNetworkList(&networks);
    if (error == ERROR_SUCCESS && !networks.empty()) {
      SortNetworks(&networks);
      for (WiFiService::NetworkList::const_iterator it = networks.begin();
           it != networks.end();
           ++it) {
        scoped_ptr<DictionaryValue> network(it->ToValue(true));
        network_list->Append(network.release());
      }
    }
  }
}

void WiFiServiceImpl::RequestNetworkScan() {
  DWORD error = EnsureInitialized();
  if (error == ERROR_SUCCESS) {
    WlanScan_function_(client_, &interface_guid_, NULL, NULL, NULL);
  }
}

void WiFiServiceImpl::StartConnect(const std::string& network_guid,
                                   std::string* error) {
  DVLOG(1) << "Start Connect: " << network_guid;
  DWORD error_code = EnsureInitialized();
  if (error_code == ERROR_SUCCESS) {
    std::string connected_network_guid;
    error_code = SaveCurrentConnectedNetwork(&connected_network_guid);
    if (error_code == ERROR_SUCCESS) {
      Frequency frequency = kFrequencyAny;
      // Connect only if network |network_guid| is not connected already.
      if (network_guid != connected_network_guid)
        error_code = Connect(network_guid, frequency);
      if (error_code == ERROR_SUCCESS) {
        // Notify that previously connected network has changed.
        NotifyNetworkChanged(connected_network_guid);
        // Start waiting for network connection state change.
        if (!networks_changed_observer_.is_null()) {
          DisableNwCategoryWizard();
          // Disable automatic network change notifications as they get fired
          // when network is just connected, but not yet accessible (doesn't
          // have valid IP address).
          enable_notify_network_changed_ = false;
          WaitForNetworkConnect(network_guid, 0);
          return;
        }
      }
    }
  }
  CheckError(error_code, kWiFiServiceError, error);
}

void WiFiServiceImpl::StartDisconnect(const std::string& network_guid,
                                      std::string* error) {
  DVLOG(1) << "Start Disconnect: " << network_guid;
  DWORD error_code = EnsureInitialized();
  if (error_code == ERROR_SUCCESS) {
    std::string connected_network_guid;
    error_code = SaveCurrentConnectedNetwork(&connected_network_guid);
    if (error_code == ERROR_SUCCESS && network_guid == connected_network_guid) {
      error_code = Disconnect();
      if (error_code == ERROR_SUCCESS) {
        NotifyNetworkChanged(network_guid);
        return;
      }
    }
  }
  CheckError(error_code, kWiFiServiceError, error);
}

void WiFiServiceImpl::SetEventObservers(
    scoped_refptr<base::MessageLoopProxy> message_loop_proxy,
    const NetworkGuidListCallback& networks_changed_observer,
    const NetworkGuidListCallback& network_list_changed_observer) {
  message_loop_proxy_.swap(message_loop_proxy);
  networks_changed_observer_ = networks_changed_observer;
  network_list_changed_observer_ = network_list_changed_observer;
}

void WiFiServiceImpl::OnWlanNotificationCallback(
    PWLAN_NOTIFICATION_DATA wlan_notification_data,
    PVOID context) {
  WiFiServiceImpl* service = reinterpret_cast<WiFiServiceImpl*>(context);
  service->OnWlanNotification(wlan_notification_data);
}

void WiFiServiceImpl::OnWlanNotification(
    PWLAN_NOTIFICATION_DATA wlan_notification_data) {
  if (message_loop_proxy_ == NULL)
    return;
  switch (wlan_notification_data->NotificationCode) {
    case wlan_notification_acm_disconnected:
    case wlan_notification_acm_connection_complete:
    case wlan_notification_acm_connection_attempt_fail: {
      PWLAN_CONNECTION_NOTIFICATION_DATA wlan_connection_data =
          reinterpret_cast<PWLAN_CONNECTION_NOTIFICATION_DATA>(
              wlan_notification_data->pData);
      message_loop_proxy_->PostTask(
          FROM_HERE,
          base::Bind(&WiFiServiceImpl::NotifyNetworkChanged,
                     base::Unretained(this),
                     GUIDFromSSID(wlan_connection_data->dot11Ssid)));
      break;
    }
    case wlan_notification_acm_scan_complete:
      message_loop_proxy_->PostTask(
          FROM_HERE,
          base::Bind(&WiFiServiceImpl::OnNetworkScanCompleteOnMainThread,
                     base::Unretained(this)));
      break;
  }
}

void WiFiServiceImpl::OnNetworkScanCompleteOnMainThread() {
  NetworkList networks;
  // Get current list of visible networks and notify that network list has
  // changed.
  DWORD error = GetVisibleNetworkList(&networks);
  DCHECK(error == ERROR_SUCCESS);
  if (error == ERROR_SUCCESS)
    NotifyNetworkListChanged(networks);
}

void WiFiServiceImpl::WaitForNetworkConnect(const std::string& network_guid,
                                            int attempt) {
  // If network didn't get connected in |kMaxAttempts|, then restore automatic
  // network change notifications and stop waiting.
  if (attempt > kMaxAttempts) {
    DLOG(ERROR) << kMaxAttempts << " attempts exceeded waiting for connect to "
                << network_guid;
    enable_notify_network_changed_ = true;
    RestoreNwCategoryWizard();
    return;
  }
  std::string connected_network_guid;
  DWORD error = FindConnectedNetwork(&connected_network_guid);
  if (network_guid == connected_network_guid) {
    DVLOG(1) << "WiFi Connected, Reset DHCP: " << network_guid;
    // Even though wireless network is now connected, it may still be unusable,
    // e.g. after Chromecast device reset. Reset DHCP on wireless network to
    // work around this issue.
    error = ResetDHCP();
    // Restore previously suppressed notifications.
    enable_notify_network_changed_ = true;
    RestoreNwCategoryWizard();
    NotifyNetworkChanged(network_guid);
  } else {
    // Continue waiting for network connection state change.
    task_runner_->PostDelayedTask(
        FROM_HERE,
        base::Bind(&WiFiServiceImpl::WaitForNetworkConnect,
                   base::Unretained(this),
                   network_guid,
                   ++attempt),
        base::TimeDelta::FromMilliseconds(kAttemptDelayMs));
  }
}

bool WiFiServiceImpl::CheckError(DWORD error_code,
                                 const std::string& error_name,
                                 std::string* error) const {
  if (error_code != ERROR_SUCCESS) {
    DLOG(ERROR) << "WiFiService Error " << error_code << ": " << error_name;
    *error = error_name;
    return true;
  }
  return false;
}

WiFiService::NetworkList::iterator WiFiServiceImpl::FindNetwork(
    NetworkList& networks,
    const std::string& network_guid) {
  for (NetworkList::iterator it = networks.begin(); it != networks.end();
       ++it) {
    if (it->guid == network_guid)
      return it;
  }
  return networks.end();
}

DWORD WiFiServiceImpl::SaveCurrentConnectedNetwork(
    std::string* connected_network_guid) {
  // Find currently connected network.
  DWORD error = FindConnectedNetwork(connected_network_guid);
  if (error == ERROR_SUCCESS && !connected_network_guid->empty()) {
    if (error == ERROR_SUCCESS) {
      SaveTempProfile(*connected_network_guid);
      std::string profile_xml;
      error = GetProfile(*connected_network_guid, &profile_xml);
      if (error == ERROR_SUCCESS) {
        saved_profiles_xml_[*connected_network_guid] = profile_xml;
      }
    }
  }
  return error;
}

void WiFiServiceImpl::SortNetworks(NetworkList* networks) {
  networks->sort(NetworkProperties::OrderByType);
}

DWORD WiFiServiceImpl::LoadWlanLibrary() {
  // Use an absolute path to load the DLL to avoid DLL preloading attacks.
  base::FilePath path;
  if (!PathService::Get(base::DIR_SYSTEM, &path)) {
    DLOG(ERROR) << "Unable to get system path.";
    return ERROR_NOT_FOUND;
  }
  wlan_api_library_ = ::LoadLibraryEx(path.Append(kWlanApiDll).value().c_str(),
                                      NULL,
                                      LOAD_WITH_ALTERED_SEARCH_PATH);
  if (!wlan_api_library_) {
    DLOG(ERROR) << "Unable to load WlanApi.dll.";
    return ERROR_NOT_FOUND;
  }

  // Initialize WlanApi function pointers
  WlanConnect_function_ =
      reinterpret_cast<WlanConnectFunction>(
          ::GetProcAddress(wlan_api_library_, kWlanConnect));
  WlanCloseHandle_function_ =
      reinterpret_cast<WlanCloseHandleFunction>(
          ::GetProcAddress(wlan_api_library_, kWlanCloseHandle));
  WlanDisconnect_function_ =
      reinterpret_cast<WlanDisconnectFunction>(
          ::GetProcAddress(wlan_api_library_, kWlanDisconnect));
  WlanEnumInterfaces_function_ =
      reinterpret_cast<WlanEnumInterfacesFunction>(
          ::GetProcAddress(wlan_api_library_, kWlanEnumInterfaces));
  WlanFreeMemory_function_ =
      reinterpret_cast<WlanFreeMemoryFunction>(
          ::GetProcAddress(wlan_api_library_, kWlanFreeMemory));
  WlanGetAvailableNetworkList_function_ =
      reinterpret_cast<WlanGetAvailableNetworkListFunction>(
          ::GetProcAddress(wlan_api_library_, kWlanGetAvailableNetworkList));
  WlanGetNetworkBssList_function_ =
      reinterpret_cast<WlanGetNetworkBssListFunction>(
          ::GetProcAddress(wlan_api_library_, kWlanGetNetworkBssList));
  WlanGetProfile_function_ =
      reinterpret_cast<WlanGetProfileFunction>(
          ::GetProcAddress(wlan_api_library_, kWlanGetProfile));
  WlanOpenHandle_function_ =
      reinterpret_cast<WlanOpenHandleFunction>(
          ::GetProcAddress(wlan_api_library_, kWlanOpenHandle));
  WlanRegisterNotification_function_ =
      reinterpret_cast<WlanRegisterNotificationFunction>(
          ::GetProcAddress(wlan_api_library_, kWlanRegisterNotification));
  WlanSaveTemporaryProfile_function_ =
      reinterpret_cast<WlanSaveTemporaryProfileFunction>(
          ::GetProcAddress(wlan_api_library_, kWlanSaveTemporaryProfile));
  WlanScan_function_ =
      reinterpret_cast<WlanScanFunction>(
          ::GetProcAddress(wlan_api_library_, kWlanScan));

  if (!WlanConnect_function_ ||
      !WlanCloseHandle_function_ ||
      !WlanDisconnect_function_ ||
      !WlanEnumInterfaces_function_ ||
      !WlanFreeMemory_function_ ||
      !WlanGetAvailableNetworkList_function_ ||
      !WlanGetProfile_function_ ||
      !WlanOpenHandle_function_ ||
      !WlanRegisterNotification_function_ ||
      !WlanScan_function_) {
    DLOG(ERROR) << "Unable to find required WlanApi function.";
    FreeLibrary(wlan_api_library_);
    wlan_api_library_ = NULL;
    return ERROR_NOT_FOUND;
  }

  // Some WlanApi functions may not be available on XP.
  if (!WlanGetNetworkBssList_function_ ||
      !WlanSaveTemporaryProfile_function_) {
    DVLOG(1) << "WlanApi function is not be available on XP.";
  }

  return ERROR_SUCCESS;
}

DWORD WiFiServiceImpl::OpenClientHandle() {
  DWORD error = LoadWlanLibrary();
  DWORD service_version = 0;

  if (error != ERROR_SUCCESS)
    return error;

  // Open a handle to the service.
  error = WlanOpenHandle_function_(1, NULL, &service_version, &client_);

  PWLAN_INTERFACE_INFO_LIST interface_list = NULL;
  if (error == ERROR_SUCCESS) {
    // Enumerate wireless interfaces.
    error = WlanEnumInterfaces_function_(client_, NULL, &interface_list);
    if (error == ERROR_SUCCESS) {
      if (interface_list != NULL && interface_list->dwNumberOfItems != 0) {
        // Remember first interface just in case if none are connected.
        interface_guid_ = interface_list->InterfaceInfo[0].InterfaceGuid;
        // Try to find a connected interface.
        for (DWORD itf = 0; itf < interface_list->dwNumberOfItems; ++itf) {
          if (interface_list->InterfaceInfo[itf].isState ==
              wlan_interface_state_connected) {
            // Found connected interface, remember it!
            interface_guid_ = interface_list->InterfaceInfo[itf].InterfaceGuid;
            break;
          }
        }
        WlanRegisterNotification_function_(client_,
                                           WLAN_NOTIFICATION_SOURCE_ALL,
                                           FALSE,
                                           OnWlanNotificationCallback,
                                           this,
                                           NULL,
                                           NULL);
      } else {
        error = ERROR_NOINTERFACE;
      }
    }
    // Clean up.
    if (interface_list != NULL)
      WlanFreeMemory_function_(interface_list);
  }
  return error;
}

DWORD WiFiServiceImpl::ResetDHCP() {
  IP_ADAPTER_INDEX_MAP adapter_index_map = {0};
  DWORD error = FindAdapterIndexMapByGUID(interface_guid_, &adapter_index_map);
  if (error == ERROR_SUCCESS) {
    error = ::IpReleaseAddress(&adapter_index_map);
    if (error == ERROR_SUCCESS) {
      error = ::IpRenewAddress(&adapter_index_map);
    }
  }
  return error;
}

DWORD WiFiServiceImpl::FindAdapterIndexMapByGUID(
    const GUID& interface_guid,
    IP_ADAPTER_INDEX_MAP* adapter_index_map) {
  string16 guid_string;
  const int kGUIDSize = 39;
  ::StringFromGUID2(
      interface_guid, WriteInto(&guid_string, kGUIDSize), kGUIDSize);

  ULONG buffer_length = 0;
  DWORD error = ::GetInterfaceInfo(NULL, &buffer_length);
  if (error == ERROR_INSUFFICIENT_BUFFER) {
    scoped_ptr<unsigned char[]> buffer(new unsigned char[buffer_length]);
    IP_INTERFACE_INFO* interface_info =
        reinterpret_cast<IP_INTERFACE_INFO*>(buffer.get());
    error = GetInterfaceInfo(interface_info, &buffer_length);
    if (error == ERROR_SUCCESS) {
      for (int adapter = 0; adapter < interface_info->NumAdapters; ++adapter) {
        if (EndsWith(
                interface_info->Adapter[adapter].Name, guid_string, false)) {
          *adapter_index_map = interface_info->Adapter[adapter];
          break;
        }
      }
    }
  }
  return error;
}

DWORD WiFiServiceImpl::DisableNwCategoryWizard() {
  base::win::RegKey nw_category_wizard;
  DWORD error = nw_category_wizard.Open(HKEY_CURRENT_USER,
                                        kNwCategoryWizardRegKey,
                                        KEY_READ | KEY_SET_VALUE);
  if (error == ERROR_SUCCESS) {
    // Save current value if present.
    if (nw_category_wizard.HasValue(kNwCategoryWizardRegValue)) {
      DWORD saved = 0u;
      error = nw_category_wizard.ReadValueDW(kNwCategoryWizardRegValue,
                                             &saved);
      if (error == ERROR_SUCCESS) {
        error = nw_category_wizard.WriteValue(kNwCategoryWizardSavedRegValue,
                                              saved);
      }
    } else {
      // Mark that temporary value has to be deleted.
      error = nw_category_wizard.WriteValue(kNwCategoryWizardDeleteRegValue,
                                            1u);
    }

    // Disable network location wizard.
    error = nw_category_wizard.WriteValue(kNwCategoryWizardRegValue,
                                          static_cast<DWORD>(0));
  }

  return error;
}

DWORD WiFiServiceImpl::RestoreNwCategoryWizard() {
  base::win::RegKey nw_category_wizard;
  DWORD error = nw_category_wizard.Open(HKEY_CURRENT_USER,
                                        kNwCategoryWizardRegKey,
                                        KEY_SET_VALUE);
  if (error == ERROR_SUCCESS) {
    // Restore saved value if present.
    if (nw_category_wizard.HasValue(kNwCategoryWizardSavedRegValue)) {
      DWORD saved = 0u;
      error = nw_category_wizard.ReadValueDW(kNwCategoryWizardSavedRegValue,
                                             &saved);
      if (error == ERROR_SUCCESS) {
        error = nw_category_wizard.WriteValue(kNwCategoryWizardRegValue,
                                              saved);
        error = nw_category_wizard.DeleteValue(kNwCategoryWizardSavedRegValue);
      }
    } else if (nw_category_wizard.HasValue(kNwCategoryWizardDeleteRegValue)) {
      error = nw_category_wizard.DeleteValue(kNwCategoryWizardRegValue);
      error = nw_category_wizard.DeleteValue(kNwCategoryWizardDeleteRegValue);
    }
  }

  return error;
}

DWORD WiFiServiceImpl::EnsureInitialized() {
  if (client_ != NULL)
    return ERROR_SUCCESS;
  return ERROR_NOINTERFACE;
}

DWORD WiFiServiceImpl::CloseClientHandle() {
  DWORD error = ERROR_SUCCESS;
  if (client_ != NULL) {
    error = WlanCloseHandle_function_(client_, NULL);
    client_ = NULL;
  }
  if (wlan_api_library_ != NULL) {
    WlanConnect_function_ = NULL;
    WlanCloseHandle_function_ = NULL;
    WlanDisconnect_function_ = NULL;
    WlanEnumInterfaces_function_ = NULL;
    WlanFreeMemory_function_ = NULL;
    WlanGetAvailableNetworkList_function_ = NULL;
    WlanGetNetworkBssList_function_ = NULL;
    WlanGetProfile_function_ = NULL;
    WlanOpenHandle_function_ = NULL;
    WlanRegisterNotification_function_ = NULL;
    WlanSaveTemporaryProfile_function_ = NULL;
    WlanScan_function_ = NULL;
    ::FreeLibrary(wlan_api_library_);
    wlan_api_library_ = NULL;
  }
  return error;
}

DOT11_SSID WiFiServiceImpl::SSIDFromGUID(
    const std::string& network_guid) const {
  DOT11_SSID ssid = {0};
  if (network_guid.length() <= DOT11_SSID_MAX_LENGTH) {
    ssid.uSSIDLength = static_cast<ULONG>(network_guid.length());
    strncpy(reinterpret_cast<char*>(ssid.ucSSID),
            network_guid.c_str(),
            ssid.uSSIDLength);
  } else {
    NOTREACHED();
  }
  return ssid;
}

std::string WiFiServiceImpl::SecurityFromDot11AuthAlg(
    DOT11_AUTH_ALGORITHM alg) const {
  switch (alg) {
    case DOT11_AUTH_ALGO_RSNA:
      return onc::wifi::kWPA_EAP;
    case DOT11_AUTH_ALGO_RSNA_PSK:
      return onc::wifi::kWPA_PSK;
    case DOT11_AUTH_ALGO_80211_SHARED_KEY:
      return onc::wifi::kWEP_PSK;
    case DOT11_AUTH_ALGO_80211_OPEN:
      return onc::wifi::kNone;
    default:
      return onc::wifi::kWPA_EAP;
  }
}

void WiFiServiceImpl::NetworkPropertiesFromAvailableNetwork(
    const WLAN_AVAILABLE_NETWORK& wlan,
    const WLAN_BSS_LIST& wlan_bss_list,
    NetworkProperties* properties) {
  if (wlan.dwFlags & WLAN_AVAILABLE_NETWORK_CONNECTED) {
    properties->connection_state = onc::connection_state::kConnected;
  } else {
    properties->connection_state = onc::connection_state::kNotConnected;
  }

  properties->ssid = SSIDFromWLAN(wlan);
  properties->name = properties->ssid;
  properties->guid = GUIDFromWLAN(wlan);
  properties->type = onc::network_type::kWiFi;

  for (size_t bss = 0; bss < wlan_bss_list.dwNumberOfItems; ++bss) {
    const WLAN_BSS_ENTRY& bss_entry(wlan_bss_list.wlanBssEntries[bss]);
    if (bss_entry.dot11Ssid.uSSIDLength == wlan.dot11Ssid.uSSIDLength &&
        0 == memcmp(bss_entry.dot11Ssid.ucSSID,
                    wlan.dot11Ssid.ucSSID,
                    bss_entry.dot11Ssid.uSSIDLength)) {
      if (bss_entry.ulChCenterFrequency < 3000000)
        properties->frequency = kFrequency2400;
      else
        properties->frequency = kFrequency5000;
      properties->frequency_list.push_back(properties->frequency);
      properties->bssid = NetworkProperties::MacAddressAsString(
          bss_entry.dot11Bssid);
    }
  }
  properties->frequency_list.sort();
  properties->frequency_list.unique();
  properties->security =
      SecurityFromDot11AuthAlg(wlan.dot11DefaultAuthAlgorithm);
  properties->signal_strength = wlan.wlanSignalQuality;
}

// Get the list of visible wireless networks
DWORD WiFiServiceImpl::GetVisibleNetworkList(NetworkList* network_list) {
  if (client_ == NULL) {
    NOTREACHED();
    return ERROR_NOINTERFACE;
  }

  DWORD error = ERROR_SUCCESS;
  PWLAN_AVAILABLE_NETWORK_LIST available_network_list = NULL;
  PWLAN_BSS_LIST bss_list = NULL;

  error = WlanGetAvailableNetworkList_function_(
      client_,
      &interface_guid_,
      WLAN_AVAILABLE_NETWORK_INCLUDE_ALL_MANUAL_HIDDEN_PROFILES,
      NULL,
      &available_network_list);

  std::set<std::string> network_guids;

  if (error == ERROR_SUCCESS &&
      available_network_list &&
      WlanGetNetworkBssList_function_) {
    // TODO(mef): WlanGetNetworkBssList is not available on XP. If XP support is
    // needed, then different method of getting BSS (e.g. OID query) will have
    // to be used.
    error = WlanGetNetworkBssList_function_(client_,
                                            &interface_guid_,
                                            NULL,
                                            dot11_BSS_type_any,
                                            FALSE,
                                            NULL,
                                            &bss_list);
    if (error == ERROR_SUCCESS && NULL != bss_list) {
      for (DWORD i = 0; i < available_network_list->dwNumberOfItems; ++i) {
        NetworkProperties network_properties;
        NetworkPropertiesFromAvailableNetwork(
            available_network_list->Network[i],
            *bss_list,
            &network_properties);
        // Check for duplicate network guids.
        if (network_guids.count(network_properties.guid)) {
          // There should be no difference between properties except for
          // |connection_state|, so mark it as |kConnected| if either one is.
          if (network_properties.connection_state ==
              onc::connection_state::kConnected) {
            NetworkList::iterator previous_network_properties =
                FindNetwork(*network_list, network_properties.guid);
            DCHECK(previous_network_properties != network_list->end());
            previous_network_properties->connection_state =
                network_properties.connection_state;
          }
        } else {
          network_list->push_back(network_properties);
        }
        network_guids.insert(network_properties.guid);
      }
    }
  }

  // clean up
  if (available_network_list != NULL) {
    WlanFreeMemory_function_(available_network_list);
  }
  if (bss_list != NULL) {
    WlanFreeMemory_function_(bss_list);
  }
  return error;
}

// Find currently connected network.
DWORD WiFiServiceImpl::FindConnectedNetwork(
    std::string* connected_network_guid) {
  if (client_ == NULL) {
    NOTREACHED();
    return ERROR_NOINTERFACE;
  }

  DWORD error = ERROR_SUCCESS;
  PWLAN_AVAILABLE_NETWORK_LIST available_network_list = NULL;
  error = WlanGetAvailableNetworkList_function_(
      client_, &interface_guid_, 0, NULL, &available_network_list);

  if (error == ERROR_SUCCESS && NULL != available_network_list) {
    for (DWORD i = 0; i < available_network_list->dwNumberOfItems; ++i) {
      const WLAN_AVAILABLE_NETWORK& wlan = available_network_list->Network[i];
      if (wlan.dwFlags & WLAN_AVAILABLE_NETWORK_CONNECTED) {
        *connected_network_guid = GUIDFromWLAN(wlan);
        break;
      }
    }
  }

  // clean up
  if (available_network_list != NULL) {
    WlanFreeMemory_function_(available_network_list);
  }

  return error;
}

DWORD WiFiServiceImpl::GetDesiredBssList(
    DOT11_SSID& ssid,
    Frequency frequency,
    scoped_ptr<DOT11_BSSID_LIST>* desired_list) {
  if (client_ == NULL) {
    NOTREACHED();
    return ERROR_NOINTERFACE;
  }

  desired_list->reset();

  if (frequency == kFrequencyAny)
    return ERROR_SUCCESS;

  // TODO(mef): WlanGetNetworkBssList is not available on XP. If XP support is
  // needed, then different method of getting BSS (e.g. OID query) will have
  // to be used.
  if (!WlanGetNetworkBssList_function_)
    return ERROR_NOT_SUPPORTED;

  DWORD error = ERROR_SUCCESS;
  PWLAN_BSS_LIST bss_list = NULL;

  error = WlanGetNetworkBssList_function_(client_,
                                          &interface_guid_,
                                          NULL,
                                          dot11_BSS_type_any,
                                          FALSE,
                                          NULL,
                                          &bss_list);
  if (error == ERROR_SUCCESS && NULL != bss_list) {
    unsigned int best_quality = 0u;
    size_t best_index = 0;
    Frequency bss_frequency;

    // Go through bss_list and find best quality BSSID with matching frequency.
    for (size_t bss = 0; bss < bss_list->dwNumberOfItems; ++bss) {
      const WLAN_BSS_ENTRY& bss_entry(bss_list->wlanBssEntries[bss]);
      if (bss_entry.dot11Ssid.uSSIDLength != ssid.uSSIDLength ||
          0 != memcmp(bss_entry.dot11Ssid.ucSSID,
                      ssid.ucSSID,
                      bss_entry.dot11Ssid.uSSIDLength))
        continue;

      if (bss_entry.ulChCenterFrequency < 3000000)
        bss_frequency = kFrequency2400;
      else
        bss_frequency = kFrequency5000;

      if (bss_frequency == frequency &&
          bss_entry.uLinkQuality > best_quality) {
        best_quality = bss_entry.uLinkQuality;
        best_index = bss;
      }
    }

    // If any matching BSS were found, prepare the header.
    if (best_quality > 0) {
      const WLAN_BSS_ENTRY& bss_entry(bss_list->wlanBssEntries[best_index]);
      scoped_ptr<DOT11_BSSID_LIST> selected_list(new DOT11_BSSID_LIST);

      selected_list->Header.Revision = DOT11_BSSID_LIST_REVISION_1;
      selected_list->Header.Size = sizeof(DOT11_BSSID_LIST);
      selected_list->Header.Type = NDIS_OBJECT_TYPE_DEFAULT;
      selected_list->uNumOfEntries = 1;
      selected_list->uTotalNumOfEntries = 1;
      std::copy(bss_entry.dot11Bssid,
                bss_entry.dot11Bssid+sizeof(bss_entry.dot11Bssid),
                selected_list->BSSIDs[0]);
      desired_list->swap(selected_list);
      DVLOG(1) << "Quality: " << best_quality << " BSS: "
          << NetworkProperties::MacAddressAsString(bss_entry.dot11Bssid);
    } else {
      error = ERROR_NOT_FOUND;
    }
  }

  // clean up
  if (bss_list != NULL) {
    WlanFreeMemory_function_(bss_list);
  }
  return error;
}


DWORD WiFiServiceImpl::Connect(const std::string& network_guid,
                               Frequency frequency) {
  if (client_ == NULL) {
    NOTREACHED();
    return ERROR_NOINTERFACE;
  }

  DWORD error = ERROR_SUCCESS;
  DOT11_SSID ssid = SSIDFromGUID(network_guid);
  scoped_ptr<DOT11_BSSID_LIST> desired_bss_list;
  error = GetDesiredBssList(ssid, frequency, &desired_bss_list);
  if (error == ERROR_SUCCESS) {
    if (HaveProfile(network_guid)) {
      base::string16 profile_name = ProfileNameFromGUID(network_guid);
      WLAN_CONNECTION_PARAMETERS wlan_params = {
          wlan_connection_mode_profile,
          profile_name.c_str(),
          NULL,
          desired_bss_list.get(),
          dot11_BSS_type_any,
          0};
      error = WlanConnect_function_(
          client_, &interface_guid_, &wlan_params, NULL);
    } else {
      // TODO(mef): wlan_connection_mode_discovery_unsecure is not available on
      // XP. If XP support is needed, then temporary profile will have to be
      // created.
      WLAN_CONNECTION_PARAMETERS wlan_params = {
          wlan_connection_mode_discovery_unsecure,
          NULL,
          &ssid,
          desired_bss_list.get(),
          dot11_BSS_type_infrastructure,
          0};
      error = WlanConnect_function_(
          client_, &interface_guid_, &wlan_params, NULL);
    }
  }

  return error;
}

DWORD WiFiServiceImpl::Disconnect() {
  if (client_ == NULL) {
    NOTREACHED();
    return ERROR_NOINTERFACE;
  }

  DWORD error = ERROR_SUCCESS;
  error = WlanDisconnect_function_(client_, &interface_guid_, NULL);
  return error;
}

DWORD WiFiServiceImpl::SaveTempProfile(const std::string& network_guid) {
  if (client_ == NULL) {
    NOTREACHED();
    return ERROR_NOINTERFACE;
  }

  DWORD error = ERROR_SUCCESS;
  base::string16 profile_name = ProfileNameFromGUID(network_guid);
  // TODO(mef): WlanSaveTemporaryProfile is not available on XP. If XP support
  // is needed, then different method of saving network profile will have to be
  // used.
  if (WlanSaveTemporaryProfile_function_) {
    error = WlanSaveTemporaryProfile_function_(client_,
                                               &interface_guid_,
                                               profile_name.c_str(),
                                               NULL,
                                               WLAN_PROFILE_USER,
                                               true,
                                               NULL);
  } else {
    error = ERROR_NOT_SUPPORTED;
  }
  return error;
}

DWORD WiFiServiceImpl::GetProfile(const std::string& network_guid,
                                  std::string* profile_xml) {
  if (client_ == NULL) {
    NOTREACHED();
    return ERROR_NOINTERFACE;
  }

  DWORD error = ERROR_SUCCESS;
  base::string16 profile_name = ProfileNameFromGUID(network_guid);
  LPWSTR str_profile_xml = NULL;
  error = WlanGetProfile_function_(client_,
                                   &interface_guid_,
                                   profile_name.c_str(),
                                   NULL,
                                   &str_profile_xml,
                                   NULL,
                                   NULL);

  if (error == ERROR_SUCCESS && str_profile_xml != NULL) {
    *profile_xml = base::UTF16ToUTF8(str_profile_xml);
  }
  // clean up
  if (str_profile_xml != NULL) {
    WlanFreeMemory_function_(str_profile_xml);
  }

  return error;
}

bool WiFiServiceImpl::HaveProfile(const std::string& network_guid) {
  DWORD error = ERROR_SUCCESS;
  std::string profile_xml;
  return GetProfile(network_guid, &profile_xml) == ERROR_SUCCESS;
}

void WiFiServiceImpl::NotifyNetworkListChanged(const NetworkList& networks) {
  if (network_list_changed_observer_.is_null())
    return;

  NetworkGuidList current_networks;
  for (NetworkList::const_iterator it = networks.begin();
       it != networks.end();
       ++it) {
    current_networks.push_back(it->guid);
  }

  message_loop_proxy_->PostTask(
      FROM_HERE,
      base::Bind(network_list_changed_observer_, current_networks));
}

void WiFiServiceImpl::NotifyNetworkChanged(const std::string& network_guid) {
  if (enable_notify_network_changed_ && !networks_changed_observer_.is_null()) {
    DVLOG(1) << "NotifyNetworkChanged: " << network_guid;
    NetworkGuidList changed_networks(1, network_guid);
    message_loop_proxy_->PostTask(
        FROM_HERE,
        base::Bind(networks_changed_observer_, changed_networks));
  }
}

WiFiService* WiFiService::Create() { return new WiFiServiceImpl(); }

}  // namespace wifi
