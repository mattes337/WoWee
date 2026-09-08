#include <catch_amalgamated.hpp>
#include "rendering/model_replacement.hpp"
#include <string>
#include <unordered_map>
#include <vector>
#include <functional>

TEST_CASE("model replacement preserves live cached pointers", "[model-lifetime]") {
    struct Model {
        std::string name;
        unsigned vertices;
    };
    std::unordered_map<unsigned, Model> models;
    auto noRetire = [](Model&) {};
    auto& first = wowee::rendering::installPreparedModel(
        models, models.end(), 7u, Model{"old", 500}, noRetire);
    const Model* liveInstance = &first;
    auto& other = wowee::rendering::installPreparedModel(
        models, models.end(), 9u, Model{"other", 100}, noRetire);
    const Model* unrelatedInstance = &other;
    // Rehash is legal while instances hold pointers (iterators are not held).
    models.rehash(1024);
    auto& replacement = wowee::rendering::installPreparedModel(
        models, models.find(7u), 7u, Model{"new", 3}, noRetire);
    CHECK(&replacement == liveInstance);
    CHECK(liveInstance->name == "new");
    CHECK(liveInstance->vertices == 3);
    CHECK(&models.at(9) == unrelatedInstance);
    CHECK(unrelatedInstance->name == "other");
    CHECK(models.size() == 2);
}

TEST_CASE("model replacement resets only matching model-relative animation state", "[model-lifetime]") {
    struct Model { struct Data { std::vector<int> bones; } data; };
    struct Instance {
        unsigned modelId;
        Model* cachedModel;
        int currentSequenceIndex;
        float animationTime;
        std::vector<int> boneMatrices;
        int position;
    };
    Model old{{{1, 2, 3}}}, replacement{{{1}}}, unrelated{{{1, 2}}};
    std::unordered_map<unsigned, Instance> instances{
        {1, {7, &old, 12, 40.0f, {8, 8, 8}, 99}},
        {2, {9, &unrelated, 3, 20.0f, {4, 5}, 42}}};
    wowee::rendering::rebindReplacedModelInstances(instances, 7u, replacement, 1);
    const auto& changed = instances.at(1);
    CHECK(changed.cachedModel == &replacement);
    CHECK(changed.currentSequenceIndex == -1);
    CHECK(changed.animationTime == 0.0f);
    CHECK(changed.boneMatrices == std::vector<int>{1});
    CHECK(changed.position == 99);
    const auto& retained = instances.at(2);
    CHECK(retained.cachedModel == &unrelated);
    CHECK(retained.currentSequenceIndex == 3);
    CHECK(retained.animationTime == 20.0f);
    CHECK(retained.boneMatrices == std::vector<int>{4, 5});
    CHECK(retained.position == 42);
    replacement.data.bones.clear();
    wowee::rendering::rebindReplacedModelInstances(instances, 7u, replacement, 1);
    CHECK(changed.boneMatrices == std::vector<int>{1});
}

TEST_CASE("prepared replacement transfers old payload to deferred cleanup", "[model-lifetime]") {
    struct Model {
        int handle = 0;
        Model() = default;
        explicit Model(int value) : handle(value) {}
        Model(Model&& other) noexcept : handle(std::exchange(other.handle, 0)) {}
        Model& operator=(Model&& other) noexcept {
            handle = std::exchange(other.handle, 0);
            return *this;
        }
    };
    std::unordered_map<unsigned, Model> models;
    auto [existing, inserted] = models.emplace(7u, Model{41});
    REQUIRE(inserted);
    Model* liveInstance = &existing->second;

    std::vector<std::function<void()>> frameCleanups[2];
    unsigned remainingFences = 2;
    int retiredHandle = 0;
    auto retireAfterAllFences = [&](Model& oldModel) {
        const int capturedHandle = std::exchange(oldModel.handle, 0);
        for (auto& queue : frameCleanups) {
            queue.emplace_back([&, capturedHandle] {
                if (--remainingFences == 0) retiredHandle = capturedHandle;
            });
        }
    };

    auto& installed = wowee::rendering::installPreparedModel(
        models, existing, 7u, Model{99}, retireAfterAllFences);
    CHECK(&installed == liveInstance);
    CHECK(liveInstance->handle == 99);
    CHECK(retiredHandle == 0);
    frameCleanups[0].front()();
    CHECK(retiredHandle == 0);
    frameCleanups[1].front()();
    CHECK(retiredHandle == 41);
}

TEST_CASE("prepared model is inserted without retiring unrelated payload", "[model-lifetime]") {
    struct Model { int handle; };
    std::unordered_map<unsigned, Model> models{{7u, {41}}};
    bool retired = false;
    auto& installed = wowee::rendering::installPreparedModel(
        models, models.end(), 9u, Model{99}, [&](Model&) { retired = true; });
    CHECK(installed.handle == 99);
    CHECK_FALSE(retired);
    CHECK(models.at(7).handle == 41);
}
