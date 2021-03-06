// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "base/compiler_specific.h"
#include "base/file_util.h"
#include "base/files/scoped_temp_dir.h"
#include "base/json/json_string_value_serializer.h"
#include "base/memory/ref_counted.h"
#include "base/message_loop/message_loop.h"
#include "base/message_loop/message_loop_proxy.h"
#include "base/sequenced_task_runner.h"
#include "base/strings/string_number_conversions.h"
#include "base/values.h"
#include "chrome/browser/policy/async_policy_provider.h"
#include "chrome/browser/policy/config_dir_policy_loader.h"
#include "chrome/browser/policy/configuration_policy_provider_test.h"
#include "components/policy/core/common/policy_bundle.h"
#include "components/policy/core/common/policy_map.h"

namespace policy {

namespace {

// Subdirectory of the config dir that contains mandatory policies.
const base::FilePath::CharType kMandatoryPath[] = FILE_PATH_LITERAL("managed");

class TestHarness : public PolicyProviderTestHarness {
 public:
  TestHarness();
  virtual ~TestHarness();

  virtual void SetUp() OVERRIDE;

  virtual ConfigurationPolicyProvider* CreateProvider(
      SchemaRegistry* registry,
      scoped_refptr<base::SequencedTaskRunner> task_runner) OVERRIDE;

  virtual void InstallEmptyPolicy() OVERRIDE;
  virtual void InstallStringPolicy(const std::string& policy_name,
                                   const std::string& policy_value) OVERRIDE;
  virtual void InstallIntegerPolicy(const std::string& policy_name,
                                    int policy_value) OVERRIDE;
  virtual void InstallBooleanPolicy(const std::string& policy_name,
                                    bool policy_value) OVERRIDE;
  virtual void InstallStringListPolicy(
      const std::string& policy_name,
      const base::ListValue* policy_value) OVERRIDE;
  virtual void InstallDictionaryPolicy(
      const std::string& policy_name,
      const base::DictionaryValue* policy_value) OVERRIDE;
  virtual void Install3rdPartyPolicy(
      const base::DictionaryValue* policies) OVERRIDE;

  const base::FilePath& test_dir() { return test_dir_.path(); }

  // JSON-encode a dictionary and write it to a file.
  void WriteConfigFile(const base::DictionaryValue& dict,
                       const std::string& file_name);

  // Returns a unique name for a policy file. Each subsequent call returns a new
  // name that comes lexicographically after the previous one.
  std::string NextConfigFileName();

  static PolicyProviderTestHarness* Create();

 private:
  base::ScopedTempDir test_dir_;
  int next_policy_file_index_;

  DISALLOW_COPY_AND_ASSIGN(TestHarness);
};

TestHarness::TestHarness()
    : PolicyProviderTestHarness(POLICY_LEVEL_MANDATORY, POLICY_SCOPE_MACHINE),
      next_policy_file_index_(100) {}

TestHarness::~TestHarness() {}

void TestHarness::SetUp() {
  ASSERT_TRUE(test_dir_.CreateUniqueTempDir());
}

ConfigurationPolicyProvider* TestHarness::CreateProvider(
    SchemaRegistry* registry,
    scoped_refptr<base::SequencedTaskRunner> task_runner) {
  scoped_ptr<AsyncPolicyLoader> loader(new ConfigDirPolicyLoader(
      task_runner, test_dir(), POLICY_SCOPE_MACHINE));
  return new AsyncPolicyProvider(registry, loader.Pass());
}

void TestHarness::InstallEmptyPolicy() {
  base::DictionaryValue dict;
  WriteConfigFile(dict, NextConfigFileName());
}

void TestHarness::InstallStringPolicy(const std::string& policy_name,
                                      const std::string& policy_value) {
  base::DictionaryValue dict;
  dict.SetString(policy_name, policy_value);
  WriteConfigFile(dict, NextConfigFileName());
}

void TestHarness::InstallIntegerPolicy(const std::string& policy_name,
                                       int policy_value) {
  base::DictionaryValue dict;
  dict.SetInteger(policy_name, policy_value);
  WriteConfigFile(dict, NextConfigFileName());
}

void TestHarness::InstallBooleanPolicy(const std::string& policy_name,
                                       bool policy_value) {
  base::DictionaryValue dict;
  dict.SetBoolean(policy_name, policy_value);
  WriteConfigFile(dict, NextConfigFileName());
}

void TestHarness::InstallStringListPolicy(const std::string& policy_name,
                                          const base::ListValue* policy_value) {
  base::DictionaryValue dict;
  dict.Set(policy_name, policy_value->DeepCopy());
  WriteConfigFile(dict, NextConfigFileName());
}

void TestHarness::InstallDictionaryPolicy(
    const std::string& policy_name,
    const base::DictionaryValue* policy_value) {
  base::DictionaryValue dict;
  dict.Set(policy_name, policy_value->DeepCopy());
  WriteConfigFile(dict, NextConfigFileName());
}

void TestHarness::Install3rdPartyPolicy(const base::DictionaryValue* policies) {
  base::DictionaryValue dict;
  dict.Set("3rdparty", policies->DeepCopy());
  WriteConfigFile(dict, NextConfigFileName());
}

void TestHarness::WriteConfigFile(const base::DictionaryValue& dict,
                                  const std::string& file_name) {
  std::string data;
  JSONStringValueSerializer serializer(&data);
  serializer.Serialize(dict);
  const base::FilePath mandatory_dir(test_dir().Append(kMandatoryPath));
  ASSERT_TRUE(file_util::CreateDirectory(mandatory_dir));
  const base::FilePath file_path(mandatory_dir.AppendASCII(file_name));
  ASSERT_EQ((int) data.size(),
            file_util::WriteFile(file_path, data.c_str(), data.size()));
}

std::string TestHarness::NextConfigFileName() {
  EXPECT_LE(next_policy_file_index_, 999);
  return std::string("policy") + base::IntToString(next_policy_file_index_++);
}

// static
PolicyProviderTestHarness* TestHarness::Create() {
  return new TestHarness();
}

}  // namespace

// Instantiate abstract test case for basic policy reading tests.
INSTANTIATE_TEST_CASE_P(
    ConfigDirPolicyLoaderTest,
    ConfigurationPolicyProviderTest,
    testing::Values(TestHarness::Create));

// Instantiate abstract test case for 3rd party policy reading tests.
INSTANTIATE_TEST_CASE_P(
    ConfigDir3rdPartyPolicyLoaderTest,
    Configuration3rdPartyPolicyProviderTest,
    testing::Values(TestHarness::Create));

// Some tests that exercise special functionality in ConfigDirPolicyLoader.
class ConfigDirPolicyLoaderTest : public PolicyTestBase {
 protected:
  virtual void SetUp() OVERRIDE {
    PolicyTestBase::SetUp();
    harness_.SetUp();
  }

  TestHarness harness_;
};

// The preferences dictionary is expected to be empty when there are no files to
// load.
TEST_F(ConfigDirPolicyLoaderTest, ReadPrefsEmpty) {
  ConfigDirPolicyLoader loader(
      loop_.message_loop_proxy(), harness_.test_dir(), POLICY_SCOPE_MACHINE);
  scoped_ptr<PolicyBundle> bundle(loader.Load());
  ASSERT_TRUE(bundle.get());
  const PolicyBundle kEmptyBundle;
  EXPECT_TRUE(bundle->Equals(kEmptyBundle));
}

// Reading from a non-existent directory should result in an empty preferences
// dictionary.
TEST_F(ConfigDirPolicyLoaderTest, ReadPrefsNonExistentDirectory) {
  base::FilePath non_existent_dir(
      harness_.test_dir().Append(FILE_PATH_LITERAL("not_there")));
  ConfigDirPolicyLoader loader(
      loop_.message_loop_proxy(), non_existent_dir, POLICY_SCOPE_MACHINE);
  scoped_ptr<PolicyBundle> bundle(loader.Load());
  ASSERT_TRUE(bundle.get());
  const PolicyBundle kEmptyBundle;
  EXPECT_TRUE(bundle->Equals(kEmptyBundle));
}

// Test merging values from different files.
TEST_F(ConfigDirPolicyLoaderTest, ReadPrefsMergePrefs) {
  // Write a bunch of data files in order to increase the chance to detect the
  // provider not respecting lexicographic ordering when reading them. Since the
  // filesystem may return files in arbitrary order, there is no way to be sure,
  // but this is better than nothing.
  base::DictionaryValue test_dict_bar;
  test_dict_bar.SetString("HomepageLocation", "http://bar.com");
  for (unsigned int i = 1; i <= 4; ++i)
    harness_.WriteConfigFile(test_dict_bar, base::IntToString(i));
  base::DictionaryValue test_dict_foo;
  test_dict_foo.SetString("HomepageLocation", "http://foo.com");
  harness_.WriteConfigFile(test_dict_foo, "9");
  for (unsigned int i = 5; i <= 8; ++i)
    harness_.WriteConfigFile(test_dict_bar, base::IntToString(i));

  ConfigDirPolicyLoader loader(
      loop_.message_loop_proxy(), harness_.test_dir(), POLICY_SCOPE_USER);
  scoped_ptr<PolicyBundle> bundle(loader.Load());
  ASSERT_TRUE(bundle.get());
  PolicyBundle expected_bundle;
  expected_bundle.Get(PolicyNamespace(POLICY_DOMAIN_CHROME, std::string()))
      .LoadFrom(&test_dict_foo, POLICY_LEVEL_MANDATORY, POLICY_SCOPE_USER);
  EXPECT_TRUE(bundle->Equals(expected_bundle));
}

}  // namespace policy
