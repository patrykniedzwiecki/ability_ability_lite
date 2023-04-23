/*
 * Copyright (c) 2023 Huawei Device Co., Ltd.
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include "ability_mgr_service_slite.h"

#include "ability_errors.h"
#include "ability_info.h"
#include "ability_service_interface.h"
#include "ability_thread_loader.h"
#include "abilityms_slite_client.h"
#include "abilityms_log.h"
#include "dummy_js_ability.h"
#include "iunknown.h"
#include "js_ability_thread.h"
#include "native_ability_thread.h"
#include "ohos_init.h"
#include "samgr_lite.h"
#include "slite_ability_loader.h"
#include "slite_ace_ability.h"
#include "want.h"

namespace OHOS {
namespace AbilitySlite {
typedef struct {
    INHERIT_IUNKNOWNENTRY(AmsSliteInterface);
    AbilityMgrServiceSlite *ams;
} AbilityMgrServiceSliteImpl;

AbilityMgrServiceSliteImpl g_amsSliteImpl = {
    DEFAULT_IUNKNOWN_ENTRY_BEGIN,
    .StartAbility = AbilityMgrServiceSlite::StartAbility,
    .TerminateAbility = AbilityMgrServiceSlite::TerminateAbility,
    .SchedulerLifecycleDone = AbilityMgrServiceSlite::SchedulerLifecycleDone,
    .ForceStopBundle = AbilityMgrServiceSlite::ForceStopBundle,
    .GetTopAbility = AbilityMgrServiceSlite::GetTopAbility,
    DEFAULT_IUNKNOWN_ENTRY_END
};

static void InitService()
{
    SamgrLite *sm = SAMGR_GetInstance();
    CHECK_NULLPTR_RETURN(sm, "AbilityManagerService", "get samgr error");
    BOOL result = sm->RegisterService(AbilityMgrServiceSlite::GetInstance());
    HILOG_INFO(HILOG_MODULE_AAFWK, "ams starts %{public}s", result ? "successfully" : "unsuccessfully");
}

SYSEX_SERVICE_INIT(InitService);

static void InitFeature()
{
    SamgrLite *samgrLite = SAMGR_GetInstance();
    CHECK_NULLPTR_RETURN(samgrLite, "AbilityMgrServiceSlite", "get samgr error");
    BOOL result = samgrLite->RegisterFeature(AMS_SERVICE, AbilityMgrServiceSlite::GetInstance());
    if (result == FALSE) {
        HILOG_ERROR(HILOG_MODULE_AAFWK, "ams register feature failure");
        return;
    }
    g_amsSliteImpl.ams = AbilityMgrServiceSlite::GetInstance();
    auto publicApi = GET_IUNKNOWN(g_amsSliteImpl);
    CHECK_NULLPTR_RETURN(publicApi, "AbilityMgrServiceSlite", "publicApi is nullptr");
    BOOL apiResult = samgrLite->RegisterFeatureApi(AMS_SERVICE, AMS_SLITE_FEATURE, publicApi);
    HILOG_INFO(HILOG_MODULE_AAFWK, "ams feature init %{public}s", apiResult ? "success" : "failure");
}

SYSEX_FEATURE_INIT(InitFeature);

AbilityMgrServiceSlite::AbilityMgrServiceSlite() : Service(), Feature()
{
    this->Feature::GetName = AbilityMgrServiceSlite::GetFeatureName;
    this->Feature::OnInitialize = AbilityMgrServiceSlite::OnFeatureInitialize;
    this->Feature::OnStop = AbilityMgrServiceSlite::OnFeatureStop;
    this->Feature::OnMessage = AbilityMgrServiceSlite::OnFeatureMessage;
    this->Service::GetName = AbilityMgrServiceSlite::GetServiceName;
    this->Service::Initialize = AbilityMgrServiceSlite::ServiceInitialize;
    this->Service::MessageHandle = AbilityMgrServiceSlite::ServiceMessageHandle;
    this->Service::GetTaskConfig = AbilityMgrServiceSlite::GetServiceTaskConfig;
}

AbilityMgrServiceSlite::~AbilityMgrServiceSlite() = default;

AbilityMgrServiceSlite *AbilityMgrServiceSlite::GetInstance()
{
    static AbilityMgrServiceSlite instance;
    return &instance;
}

const Identity *AbilityMgrServiceSlite::GetIdentity()
{
    return &serviceIdentity_;
}

const char *AbilityMgrServiceSlite::GetFeatureName(Feature *feature)
{
    (void) feature;
    return AMS_SLITE_FEATURE;
}

void AbilityMgrServiceSlite::OnFeatureInitialize(Feature *feature, Service *parent, Identity identity)
{
    (void) parent;
    CHECK_NULLPTR_RETURN(feature, "AbilityMgrServiceSlite", "feature initialize fail");
    auto *abilityMgrService = static_cast<AbilityMgrServiceSlite *>(feature);
    abilityMgrService->featureIdentity_ = identity;
    AbilityRecordManager::GetInstance().StartLauncher();
}

void AbilityMgrServiceSlite::OnFeatureStop(Feature *feature, Identity identity)
{
    (void) feature;
    (void) identity;
}

BOOL AbilityMgrServiceSlite::OnFeatureMessage(Feature *feature, Request *request)
{
    if (feature == nullptr || request == nullptr) {
        return FALSE;
    }
    return TRUE;
}

const char *AbilityMgrServiceSlite::GetServiceName(Service *service)
{
    (void) service;
    return AMS_SERVICE;
}

BOOL AbilityMgrServiceSlite::ServiceInitialize(Service *service, Identity identity)
{
    if (service == nullptr) {
        return FALSE;
    }
    auto *abilityMgrService = static_cast<AbilityMgrServiceSlite *>(service);
    abilityMgrService->serviceIdentity_ = identity;
    InitAbilityThreadLoad();
    InitAbilityLoad();
    AbilityMsClient::GetInstance().SetServiceIdentity(&abilityMgrService->serviceIdentity_);
    return TRUE;
}

BOOL AbilityMgrServiceSlite::ServiceMessageHandle(Service *service, Request *request)
{
    if (request == nullptr) {
        return FALSE;
    }
    int32_t ret = ERR_OK;
    if (request->msgId == START_ABILITY) {
        auto *data = static_cast<StartAbilityData *>(request->data);
        if (data == nullptr) {
            return FALSE;
        }
        AbilityRecordManager::GetInstance().curTask_ = data->curTask;
        ret = AbilityRecordManager::GetInstance().StartAbility(data->want);
        ClearWant(data->want);
        AdapterFree(data->want);
        AdapterFree(request->data);
        request->data = nullptr;
        request->len = 0;
    } else if (request->msgId == ABILITY_TRANSACTION_DONE) {
        uint32_t token = request->msgValue & TRANSACTION_MSG_TOKEN_MASK;
        uint32_t state = (request->msgValue >> TRANSACTION_MSG_STATE_OFFSET) & TRANSACTION_MSG_STATE_MASK;
        ret = AbilityRecordManager::GetInstance().SchedulerLifecycleDone(token, state);
    } else if (request->msgId == TERMINATE_ABILITY) {
        ret = AbilityRecordManager::GetInstance().TerminateAbility(request->msgValue);
    } else if (request->msgId == TERMINATE_APP) {
        ret = AbilityRecordManager::GetInstance().ForceStopBundle(request->msgValue);
    } else if (request->msgId == TERMINATE_APP_BY_BUNDLENAME) {
        char *bundleName = reinterpret_cast<char *>(request->data);
        ret = AbilityRecordManager::GetInstance().ForceStop(bundleName);
    }
    return ret == ERR_OK;
}

TaskConfig AbilityMgrServiceSlite::GetServiceTaskConfig(Service *service)
{
    constexpr uint16 QUEUE_SIZE = 20;
    TaskConfig config = { LEVEL_HIGH, PRI_NORMAL, AMS_TASK_STACK_SIZE, QUEUE_SIZE, SINGLE_TASK };
    return config;
}

int32_t AbilityMgrServiceSlite::StartAbility(const Want *want)
{
    int32 ret = AbilityRecordManager::GetInstance().StartAbility(want);
    ClearWant(const_cast<Want *>(want));
    return ret;
}

int32_t AbilityMgrServiceSlite::TerminateAbility(uint64_t token)
{
    uint32_t slitToken = token & TRANSACTION_MSG_TOKEN_MASK;
    return AbilityRecordManager::GetInstance().TerminateAbility(slitToken);
}

int32_t AbilityMgrServiceSlite::SchedulerLifecycleDone(uint64_t token, int state)
{
    return AbilityRecordManager::GetInstance().SchedulerLifecycleDone(token, state);
}

int32_t AbilityMgrServiceSlite::ForceStopBundle(uint64_t token)
{
    uint16_t slitToken = token & TRANSACTION_MSG_TOKEN_MASK;
    return AbilityRecordManager::GetInstance().ForceStopBundle(slitToken);
}

ElementName *AbilityMgrServiceSlite::GetTopAbility()
{
    return AbilityRecordManager::GetInstance().GetTopAbility();
}

static AbilityThread *createJsAbilityThread()
{
    auto *jsThread = new JsAbilityThread();
    return jsThread;
}

static AbilityThread *createNativeAbilityThread()
{
    auto *nativeThread = new NativeAbilityThread();
    return nativeThread;
}

void AbilityMgrServiceSlite::InitAbilityThreadLoad()
{
    AbilityThreadLoader::GetInstance().SetCreatorFunc(AbilityThreadCreatorType::JS_CREATOR, createJsAbilityThread);
    AbilityThreadLoader::GetInstance().SetCreatorFunc(AbilityThreadCreatorType::NATIVE_CREATOR,
        createNativeAbilityThread);
}

static SliteAbility *createJsAbility(const char *bundleName)
{
    SliteAbility *jsAbility = new DummyJsAbility();
    return jsAbility;
}

void AbilityMgrServiceSlite::InitAbilityLoad()
{
    SliteAbilityLoader::GetInstance().SetAbilityCreatorFunc(SliteAbilityType::JS_ABILITY, createJsAbility);
}
} // namespace AbilitySlite
} // namespace OHOS