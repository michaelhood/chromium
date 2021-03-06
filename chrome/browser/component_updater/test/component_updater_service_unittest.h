// Copyright 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_COMPONENT_UPDATER_TEST_COMPONENT_UPDATER_SERVICE_UNITTEST_H_
#define CHROME_BROWSER_COMPONENT_UPDATER_TEST_COMPONENT_UPDATER_SERVICE_UNITTEST_H_

#include <list>
#include <map>
#include <string>
#include <utility>
#include <vector>
#include "base/basictypes.h"
#include "base/compiler_specific.h"
#include "base/files/file_path.h"
#include "base/memory/ref_counted.h"
#include "base/memory/scoped_ptr.h"
#include "chrome/browser/component_updater/component_updater_service.h"
#include "chrome/browser/component_updater/test/component_patcher_mock.h"
#include "chrome/browser/component_updater/test/url_request_post_interceptor.h"
#include "content/public/test/test_browser_thread_bundle.h"
#include "content/test/net/url_request_prepackaged_interceptor.h"
#include "net/url_request/url_request_test_util.h"
#include "testing/gmock/include/gmock/gmock.h"
#include "testing/gtest/include/gtest/gtest.h"

class TestInstaller;

namespace component_updater {

// Intercepts HTTP GET requests sent to "localhost".
typedef content::URLLocalHostRequestPrepackagedInterceptor GetInterceptor;

// Intercepts HTTP POST requests sent to "localhost2".
class InterceptorFactory : public URLRequestPostInterceptorFactory {
 public:
  InterceptorFactory();
  ~InterceptorFactory();

  URLRequestPostInterceptor* CreateInterceptor();

 private:
  DISALLOW_COPY_AND_ASSIGN(InterceptorFactory);
};

// component 1 has extension id "jebgalgnebhfojomionfpkfelancnnkf", and
// the RSA public key the following hash:
const uint8 jebg_hash[] = {0x94, 0x16, 0x0b, 0x6d, 0x41, 0x75, 0xe9, 0xec,
                           0x8e, 0xd5, 0xfa, 0x54, 0xb0, 0xd2, 0xdd, 0xa5,
                           0x6e, 0x05, 0x6b, 0xe8, 0x73, 0x47, 0xf6, 0xc4,
                           0x11, 0x9f, 0xbc, 0xb3, 0x09, 0xb3, 0x5b, 0x40};
// component 2 has extension id "abagagagagagagagagagagagagagagag", and
// the RSA public key the following hash:
const uint8 abag_hash[] = {0x01, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06,
                           0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06,
                           0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06,
                           0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x01};
// component 3 has extension id "ihfokbkgjpifnbbojhneepfflplebdkc", and
// the RSA public key the following hash:
const uint8 ihfo_hash[] = {0x87, 0x5e, 0xa1, 0xa6, 0x9f, 0x85, 0xd1, 0x1e,
                           0x97, 0xd4, 0x4f, 0x55, 0xbf, 0xb4, 0x13, 0xa2,
                           0xe7, 0xc5, 0xc8, 0xf5, 0x60, 0x19, 0x78, 0x1b,
                           0x6d, 0xe9, 0x4c, 0xeb, 0x96, 0x05, 0x42, 0x17};

class TestConfigurator : public ComponentUpdateService::Configurator {
 public:
  explicit TestConfigurator();

  virtual ~TestConfigurator();

  virtual int InitialDelay() OVERRIDE;

  typedef std::pair<CrxComponent*, int> CheckAtLoopCount;

  virtual int NextCheckDelay() OVERRIDE;

  virtual int StepDelay() OVERRIDE;

  virtual int StepDelayMedium() OVERRIDE;

  virtual int MinimumReCheckWait() OVERRIDE;

  virtual int OnDemandDelay() OVERRIDE;

  virtual GURL UpdateUrl() OVERRIDE;

  virtual GURL PingUrl() OVERRIDE;

  virtual const char* ExtraRequestParams() OVERRIDE;

  virtual size_t UrlSizeLimit() OVERRIDE;

  virtual net::URLRequestContextGetter* RequestContext() OVERRIDE;

  // Don't use the utility process to run component updater code.
  virtual bool InProcess() OVERRIDE;

  virtual ComponentPatcher* CreateComponentPatcher() OVERRIDE;

  virtual bool DeltasEnabled() const OVERRIDE;

  void SetLoopCount(int times);

  void SetRecheckTime(int seconds);

  void SetOnDemandTime(int seconds);

  void SetComponentUpdateService(ComponentUpdateService* cus);

  void SetQuitClosure(const base::Closure& quit_closure);

  void SetInitialDelay(int seconds);

 private:
  int initial_time_;
  int times_;
  int recheck_time_;
  int ondemand_time_;

  ComponentUpdateService* cus_;
  scoped_refptr<net::TestURLRequestContextGetter> context_;
  base::Closure quit_closure_;
};

class ComponentUpdaterTest : public testing::Test {
 public:
  enum TestComponents {
    kTestComponent_abag,
    kTestComponent_jebg,
    kTestComponent_ihfo,
  };

  ComponentUpdaterTest();

  virtual ~ComponentUpdaterTest();

  virtual void SetUp();

  virtual void TearDown();

  ComponentUpdateService* component_updater();

  // Makes the full path to a component updater test file.
  const base::FilePath test_file(const char* file);

  TestConfigurator* test_configurator();

  ComponentUpdateService::Status RegisterComponent(CrxComponent* com,
                                                   TestComponents component,
                                                   const Version& version,
                                                   TestInstaller* installer);

 protected:
  void RunThreads();
  void RunThreadsUntilIdle();

  scoped_ptr<component_updater::InterceptorFactory> interceptor_factory_;
  URLRequestPostInterceptor* post_interceptor_;   // Owned by the factory.

  scoped_ptr<GetInterceptor> get_interceptor_;
 private:
  TestConfigurator* test_config_;
  base::FilePath test_data_dir_;
  content::TestBrowserThreadBundle thread_bundle_;
  scoped_ptr<ComponentUpdateService> component_updater_;
};

const char expected_crx_url[] =
    "http://localhost/download/jebgalgnebhfojomionfpkfelancnnkf.crx";

class MockComponentObserver : public ComponentObserver {
 public:
  MockComponentObserver();
  ~MockComponentObserver();
  MOCK_METHOD2(OnEvent, void(Events event, int extra));
};

class OnDemandTester {
 public:
  static ComponentUpdateService::Status OnDemand(
      ComponentUpdateService* cus, const std::string& component_id);
};

}  // namespace component_updater

#endif  // CHROME_BROWSER_COMPONENT_UPDATER_TEST_COMPONENT_UPDATER_SERVICE_UNITTEST_H_
