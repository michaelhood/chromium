// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/policy/async_policy_loader.h"

#include "base/bind.h"
#include "base/location.h"
#include "base/sequenced_task_runner.h"
#include "components/policy/core/common/policy_bundle.h"

using base::Time;
using base::TimeDelta;

namespace policy {

namespace {

// Amount of time to wait for the files on disk to settle before trying to load
// them. This alleviates the problem of reading partially written files and
// makes it possible to batch quasi-simultaneous changes.
const int kSettleIntervalSeconds = 5;

// The time interval for rechecking policy. This is the fallback in case the
// implementation never detects changes.
const int kReloadIntervalSeconds = 15 * 60;

}  // namespace

AsyncPolicyLoader::AsyncPolicyLoader(
    scoped_refptr<base::SequencedTaskRunner> task_runner)
    : task_runner_(task_runner),
      weak_factory_(this) {}

AsyncPolicyLoader::~AsyncPolicyLoader() {}

Time AsyncPolicyLoader::LastModificationTime() {
  return Time();
}

void AsyncPolicyLoader::Reload(bool force) {
  DCHECK(task_runner_->RunsTasksOnCurrentThread());

  TimeDelta delay;
  Time now = Time::Now();
  // Check if there was a recent modification to the underlying files.
  if (!force && !IsSafeToReload(now, &delay)) {
    ScheduleNextReload(delay);
    return;
  }

  scoped_ptr<PolicyBundle> bundle(Load());

  // Check if there was a modification while reading.
  if (!force && !IsSafeToReload(now, &delay)) {
    ScheduleNextReload(delay);
    return;
  }

  // Filter out mismatching policies.
  schema_map_->FilterBundle(bundle.get());

  update_callback_.Run(bundle.Pass());
  ScheduleNextReload(TimeDelta::FromSeconds(kReloadIntervalSeconds));
}

scoped_ptr<PolicyBundle> AsyncPolicyLoader::InitialLoad(
    const scoped_refptr<SchemaMap>& schema_map) {
  // This is the first load, early during startup. Use this to record the
  // initial |last_modification_time_|, so that potential changes made before
  // installing the watches can be detected.
  last_modification_time_ = LastModificationTime();
  schema_map_ = schema_map;
  scoped_ptr<PolicyBundle> bundle(Load());
  // Filter out mismatching policies.
  schema_map_->FilterBundle(bundle.get());
  return bundle.Pass();
}

void AsyncPolicyLoader::Init(const UpdateCallback& update_callback) {
  DCHECK(task_runner_->RunsTasksOnCurrentThread());
  DCHECK(update_callback_.is_null());
  DCHECK(!update_callback.is_null());
  update_callback_ = update_callback;

  InitOnBackgroundThread();

  // There might have been changes to the underlying files since the initial
  // load and before the watchers have been created.
  if (LastModificationTime() != last_modification_time_)
    Reload(false);

  // Start periodic refreshes.
  ScheduleNextReload(TimeDelta::FromSeconds(kReloadIntervalSeconds));
}

void AsyncPolicyLoader::RefreshPolicies(scoped_refptr<SchemaMap> schema_map) {
  DCHECK(task_runner_->RunsTasksOnCurrentThread());
  schema_map_ = schema_map;
  Reload(true);
}

void AsyncPolicyLoader::ScheduleNextReload(TimeDelta delay) {
  DCHECK(task_runner_->RunsTasksOnCurrentThread());
  weak_factory_.InvalidateWeakPtrs();
  task_runner_->PostDelayedTask(FROM_HERE,
                                base::Bind(&AsyncPolicyLoader::Reload,
                                           weak_factory_.GetWeakPtr(),
                                           false /* force */),
                                delay);
}

bool AsyncPolicyLoader::IsSafeToReload(const Time& now, TimeDelta* delay) {
  Time last_modification = LastModificationTime();
  if (last_modification.is_null())
    return true;

  // If there was a change since the last recorded modification, wait some more.
  const TimeDelta kSettleInterval(
      TimeDelta::FromSeconds(kSettleIntervalSeconds));
  if (last_modification != last_modification_time_) {
    last_modification_time_ = last_modification;
    last_modification_clock_ = now;
    *delay = kSettleInterval;
    return false;
  }

  // Check whether the settle interval has elapsed.
  const TimeDelta age = now - last_modification_clock_;
  if (age < kSettleInterval) {
    *delay = kSettleInterval - age;
    return false;
  }

  return true;
}

}  // namespace policy
