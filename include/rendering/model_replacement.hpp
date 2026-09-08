#pragma once
#include <utility>

namespace wowee::rendering {
// Instances cache pointers into the model map. Replacing a mapped value must
// retain its node; erase + insert invalidates every such live pointer.
template<class ModelMap, class Key, class Model>
auto& replaceModelInPlace(ModelMap& models, const Key& id, Model&& replacement) {
    return models.insert_or_assign(id, std::forward<Model>(replacement)).first->second;
}
}
