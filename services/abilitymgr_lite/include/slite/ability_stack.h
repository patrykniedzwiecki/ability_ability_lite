/*
 * Copyright (c) 2020 Huawei Device Co., Ltd.
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

#ifndef OHOS_ABILITY_SLITE_ABILITY_STACK_H
#define OHOS_ABILITY_SLITE_ABILITY_STACK_H

#include "ability_record.h"
#include "utils_list.h"
#include <new>

namespace OHOS {
namespace AbilitySlite {
class AbilityStack {
public:
    AbilityStack() = default;

    ~AbilityStack() = default;

    const AbilityRecord *GetTopAbility() const;

    void PushAbility(AbilityRecord *record);

    void PopAbility();

    void Erase(AbilityRecord *record);

private:
    List<AbilityRecord *> abilityStack_ {};
};
} // namespace AbilitySlite
} // namespace OHOS
#endif  // OHOS_ABILITY_SLITE_ABILITY_STACK_H
