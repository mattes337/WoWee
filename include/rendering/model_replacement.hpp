#pragma once
#include <utility>
#include <algorithm>
#include <cstddef>
#include <functional>
#include <type_traits>

namespace wowee::rendering {
// Commit a fully prepared replacement without another map lookup. The retire
// callback snapshots ownership of the old payload (typically into a deferred
// cleanup queue), then a no-throw move keeps the mapped node and cached pointers
// valid. Preparation failures happen before this function and leave the old
// model untouched.
template<class ModelMap, class Iterator, class Key, class Model, class Retire>
auto& installPreparedModel(ModelMap& models, Iterator existing, const Key& id,
                           Model&& replacement, Retire&& retire) {
    using Mapped = typename ModelMap::mapped_type;
    if (existing == models.end()) {
        return models.emplace(id, std::forward<Model>(replacement)).first->second;
    }
    static_assert(std::is_nothrow_assignable_v<Mapped&, Model&&>,
                  "model replacement must commit without throwing after retirement");
    std::invoke(std::forward<Retire>(retire), existing->second);
    existing->second = std::forward<Model>(replacement);
    return existing->second;
}
// Reset only references and model-relative animation state. Placement, identity
// and unrelated models survive a hot replacement unchanged.
template<class Instances, class Key, class Model, class Matrix>
void rebindReplacedModelInstances(Instances& instances, const Key& id,
                                 Model& model, const Matrix& identity) {
    for (auto& [instanceId, instance] : instances) {
        (void)instanceId;
        if (instance.modelId != id) continue;
        instance.cachedModel = &model;
        instance.currentSequenceIndex = -1;
        instance.animationTime = 0.0f;
        instance.boneMatrices.assign(std::max(std::size_t{1}, model.data.bones.size()), identity);
    }
}

}
