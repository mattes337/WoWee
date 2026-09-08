#include <catch_amalgamated.hpp>
#include "rendering/model_replacement.hpp"
#include <string>
#include <unordered_map>

TEST_CASE("model replacement preserves live cached pointers", "[model-lifetime]") {
    struct Model {
        std::string name; unsigned vertices; int* oldDestructions;
        ~Model() { if (name == "old") ++*oldDestructions; }
    };
    int oldDestructions = 0;
    std::unordered_map<unsigned, Model> models;
    auto& first = wowee::rendering::replaceModelInPlace(models, 7u, Model{"old", 500, &oldDestructions});
    const Model* liveInstance = &first;
    oldDestructions = 0; // Ignore the initial construction temporary.
    auto& other = wowee::rendering::replaceModelInPlace(models, 9u, Model{"other", 100, &oldDestructions});
    const Model* unrelatedInstance = &other;
    // Rehash is legal while instances hold pointers (iterators are not held).
    models.rehash(1024);
    auto& replacement = wowee::rendering::replaceModelInPlace(models, 7u, Model{"new", 3, &oldDestructions});
    REQUIRE(oldDestructions == 0); // Erase/reinsert must fail even if allocator reuses the address.
    CHECK(&replacement == liveInstance);
    CHECK(liveInstance->name == "new");
    CHECK(liveInstance->vertices == 3);
    CHECK(&models.at(9) == unrelatedInstance);
    CHECK(unrelatedInstance->name == "other");
    CHECK(models.size() == 2);
}
