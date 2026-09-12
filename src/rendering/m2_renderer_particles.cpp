#include "rendering/m2_renderer.hpp"
#include <unordered_set>
#include "rendering/m2_renderer_internal.h"
#include "rendering/vk_context.hpp"
#include "rendering/vk_buffer.hpp"
#include "rendering/vk_texture.hpp"
#include "rendering/vk_pipeline.hpp"
#include "rendering/vk_shader.hpp"
#include "rendering/vk_utils.hpp"
#include "rendering/camera.hpp"

#include <set>
#include "pipeline/asset_manager.hpp"
#include "core/logger.hpp"
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <glm/gtx/quaternion.hpp>
#include <algorithm>
#include <cmath>
#include <random>

namespace wowee {
namespace rendering {

// --- M2 Particle Emitter Helpers ---

float M2Renderer::interpFloat(const pipeline::M2AnimationTrack& track, float animTime,
                                float globalTime, int seqIdx,
                                const std::vector<uint32_t>& globalSeqDurations) {
    return m2_track::sampleFloat(track, seqIdx, animTime, globalTime,
                                 globalSeqDurations, 0.0f);
}

// Interpolate an M2 FBlock (particle lifetime curve) at a given life ratio [0..1].
// FBlocks store per-lifetime keyframes for particle color, alpha, and scale.
// NOTE: interpFBlockFloat and interpFBlockVec3 share identical interpolation logic -
// if you fix a bug in one, update the other to match.
float M2Renderer::interpFBlockFloat(const pipeline::M2FBlock& fb, float lifeRatio) {
    if (fb.floatValues.empty()) return 1.0f;
    if (fb.floatValues.size() == 1 || fb.timestamps.empty()) return fb.floatValues[0];
    lifeRatio = glm::clamp(lifeRatio, 0.0f, 1.0f);
    for (size_t i = 0; i < fb.timestamps.size() - 1; i++) {
        if (lifeRatio <= fb.timestamps[i + 1]) {
            float t0 = fb.timestamps[i];
            float t1 = fb.timestamps[i + 1];
            float dur = t1 - t0;
            float frac = (dur > 0.0f) ? (lifeRatio - t0) / dur : 0.0f;
            size_t v0 = std::min(i, fb.floatValues.size() - 1);
            size_t v1 = std::min(i + 1, fb.floatValues.size() - 1);
            return glm::mix(fb.floatValues[v0], fb.floatValues[v1], frac);
        }
    }
    return fb.floatValues.back();
}

glm::vec3 M2Renderer::interpFBlockVec3(const pipeline::M2FBlock& fb, float lifeRatio) {
    if (fb.vec3Values.empty()) return glm::vec3(1.0f);
    if (fb.vec3Values.size() == 1 || fb.timestamps.empty()) return fb.vec3Values[0];
    lifeRatio = glm::clamp(lifeRatio, 0.0f, 1.0f);
    for (size_t i = 0; i < fb.timestamps.size() - 1; i++) {
        if (lifeRatio <= fb.timestamps[i + 1]) {
            float t0 = fb.timestamps[i];
            float t1 = fb.timestamps[i + 1];
            float dur = t1 - t0;
            float frac = (dur > 0.0f) ? (lifeRatio - t0) / dur : 0.0f;
            size_t v0 = std::min(i, fb.vec3Values.size() - 1);
            size_t v1 = std::min(i + 1, fb.vec3Values.size() - 1);
            return glm::mix(fb.vec3Values[v0], fb.vec3Values[v1], frac);
        }
    }
    return fb.vec3Values.back();
}

std::vector<glm::vec3> M2Renderer::getWaterVegetationPositions(const glm::vec3& camPos, float maxDist) const {
    std::vector<glm::vec3> result;
    float maxDistSq = maxDist * maxDist;
    for (const auto& inst : instances) {
        if (!inst.cachedModel || !inst.cachedModel->isWaterVegetation) continue;
        glm::vec3 diff = inst.position - camPos;
        if (glm::dot(diff, diff) <= maxDistSq) {
            result.push_back(inst.position);
        }
    }
    return result;
}

void M2Renderer::emitParticles(M2Instance& inst, const M2ModelGPU& gpu, float dt) {
    if (gpu.isInstancePortal) return;

    if (inst.emitterAccumulators.size() != gpu.particleEmitters.size()) {
        inst.emitterAccumulators.resize(gpu.particleEmitters.size(), 0.0f);
    }

    std::uniform_real_distribution<float> dist01(0.0f, 1.0f);
    std::uniform_real_distribution<float> distN(-1.0f, 1.0f);
    std::uniform_int_distribution<int> distTile;

    for (size_t ei = 0; ei < gpu.particleEmitters.size(); ei++) {
        const auto& em = gpu.particleEmitters[ei];
        if (!em.enabled) continue;

        float rate = interpFloat(em.emissionRate, inst.animTime, inst.globalSequenceTime,
                                 inst.currentSequenceIndex, gpu.globalSequenceDurations);
        float life = interpFloat(em.lifespan, inst.animTime, inst.globalSequenceTime,
                                 inst.currentSequenceIndex, gpu.globalSequenceDurations);
        // What the player asked to see of it, before the floor below. The order
        // is the whole point: thinning first and flooring second lets a low
        // setting take smoke, dust and spell effects down while a candle is
        // pulled back up to the handful of particles that still reads as fire.
        rate *= particleDensity_;

        // A flame reads as a flame only when enough particles are alive at once.
        // Authored rates vary wildly for the same visual intent - a candle asks
        // for 40/s over half a second, CHANDELIER01 for 1/s over six seconds,
        // which sustains a single speck per candle and looks like a bare glow.
        // Steady-state population is rate x lifespan, so floor the rate against
        // the lifespan to hold every fixture at a comparable density. Do not lower
        // this: at 7 the flames disappear entirely, as they do under any change
        // that reduces how many particles are alive or how far they travel. The
        // effect is only visible because a scattering of particles reaches open
        // air, so thinning it drops the whole thing below the threshold.
        if (rate > 0.0f && life > 0.0f &&
            (gpu.isLanternLike || gpu.isTorch || gpu.isBrazierOrFire || gpu.isKoboldFlame)) {
            constexpr float kMinLiveParticles = 15.0f;
            rate = std::max(rate, kMinLiveParticles / std::max(life, 0.1f));
        }

        // WOWEE_SCENE_DIAG=1 says what each emitter of a model is actually
        // being asked for, once per emitter. A scene whose glow is missing is
        // either not emitting or not drawing, and nothing else tells them apart.
        static const bool kEmitterDiag = envFlagEnabled("WOWEE_SCENE_DIAG");
        if (kEmitterDiag) {
            static std::set<std::pair<std::string, size_t>> saidEmitter;
            if (saidEmitter.insert({gpu.name, ei}).second) {
                LOG_WARNING("EMITTER ", gpu.name, " #", ei,
                            " rate=", rate, " life=", life,
                            " enabled=", em.enabled ? 1 : 0,
                            " tex=", (ei < gpu.particleTextures.size() && gpu.particleTextures[ei])
                                         ? "yes" : "NO",
                            " bone=", em.bone,
                            " scaleKeys=", em.particleScale.floatValues.size(),
                            " scale0=", em.particleScale.floatValues.empty() ? -1.0f : em.particleScale.floatValues[0],
                            " alphaKeys=", em.particleAlpha.floatValues.size(),
                            " alpha0=", em.particleAlpha.floatValues.empty() ? -1.0f : em.particleAlpha.floatValues[0],
                            " live=", inst.particles.size());
            }
        }

        if (rate <= 0.0f || life <= 0.0f) {
            // Diagnostic: a lamp or flame whose emitter never fires produces a
            // fixture that glows but shows no flame. Report each model once so a
            // default-level log says whether emission is the cause.
            if (gpu.isLanternLike || gpu.isTorch || gpu.isBrazierOrFire) {
                static std::unordered_set<std::string> reported;
                if (reported.insert(gpu.name).second) {
                    LOG_WARNING("Flame emitter idle: '", gpu.name, "' emitter=", ei,
                                " rate=", rate, " life=", life,
                                " animTime=", inst.animTime,
                                " seqIdx=", inst.currentSequenceIndex,
                                " gsTime=", inst.globalSequenceTime,
                                " rateSeqs=", em.emissionRate.sequences.size(),
                                " lifeSeqs=", em.lifespan.sequences.size(),
                                " rateGlobalSeq=", em.emissionRate.globalSequence);
                }
            }
            continue;
        }

        inst.emitterAccumulators[ei] += rate * dt;

        while (inst.emitterAccumulators[ei] >= 1.0f && inst.particles.size() < MAX_M2_PARTICLES) {
            inst.emitterAccumulators[ei] -= 1.0f;

            M2Particle p;
            p.emitterIndex = static_cast<int>(ei);
            p.life = 0.0f;
            p.maxLife = life;
            p.tileIndex = 0.0f;

            // Birth, the way the client's generators do it: a point in the
            // emitter's bone space and a direction about the bone's +Z, then
            // both through model x bone - the point with w = 1, the direction
            // with w = 0, so a scaled bone scales the velocity as well.
            //
            // A plane emitter is born on a rectangle of emissionAreaLength by
            // emissionAreaWidth in the bone's XY and sent into a cone: polar
            // angle up to verticalRange, azimuth up to horizontalRange, both
            // drawn uniformly and signed. A sphere emitter is born on a shell
            // between the two radii and sent outward along its own radius,
            // or straight up when HemisphereUp says so. zSource, when set,
            // aims the velocity from (0, 0, zSource) through the birth point.
            glm::mat4 boneXform = glm::mat4(1.0f);
            if (em.bone < inst.boneMatrices.size()) {
                boneXform = inst.boneMatrices[em.bone];
            }
            const glm::mat4 emitterXform = inst.modelMatrix * boneXform;

            auto sample = [&](const pipeline::M2AnimationTrack& track) {
                return interpFloat(track, inst.animTime, inst.globalSequenceTime,
                                   inst.currentSequenceIndex, gpu.globalSequenceDurations);
            };
            float speed = sample(em.emissionSpeed);
            speed *= 1.0f + sample(em.speedVariation) * distN(particleRng_);
            const float polar = sample(em.verticalRange) * distN(particleRng_);
            const float azimuth = sample(em.horizontalRange) * distN(particleRng_);
            const float areaLength = sample(em.emissionAreaLength);
            const float areaWidth = sample(em.emissionAreaWidth);
            const float zSource = sample(em.zSource);

            glm::vec3 offset(0.0f);
            glm::vec3 dir(0.0f, 0.0f, 1.0f);
            if (em.emitterType == 2) {
                const glm::vec3 radial(std::cos(polar) * std::cos(azimuth),
                                       std::cos(polar) * std::sin(azimuth),
                                       std::sin(polar));
                offset = radial * (areaLength + (areaWidth - areaLength) * dist01(particleRng_));
                dir = (em.flags & kParticleFlagHemisphereUp) ? glm::vec3(0.0f, 0.0f, 1.0f) : radial;
            } else {
                offset = glm::vec3(distN(particleRng_) * areaLength * 0.5f,
                                   distN(particleRng_) * areaWidth * 0.5f, 0.0f);
                dir = glm::vec3(std::cos(azimuth) * std::sin(polar),
                                std::sin(azimuth) * std::sin(polar),
                                std::cos(polar));
            }
            if (zSource > 0.001f) {
                const glm::vec3 fromSource = offset - glm::vec3(0.0f, 0.0f, zSource);
                if (glm::dot(fromSource, fromSource) > 1.0e-8f) dir = glm::normalize(fromSource);
            }

            p.position = glm::vec3(emitterXform * glm::vec4(em.position + offset, 1.0f));
            const glm::mat3 rotMat = glm::mat3(emitterXform);
            p.velocity = rotMat * (dir * speed);

            // The world's drift, and not a scene's - see setSceneMode. When
            // emission speed is ~0 and bone animation isn't loaded (.anim
            // files), particles pile up at the same position. Give them a
            // drift so they spread outward like a mist/spray effect instead of
            // clustering. An authored scene's still emitters are still on
            // purpose: the wyrm's frost is born at speed 0 and rides its bone.
            if (!sceneMode_ && std::abs(speed) < 0.01f) {
                if (gpu.isFireflyEffect) {
                    // Fireflies: gentle random drift in all directions
                    p.velocity = rotMat * glm::vec3(
                        distN(particleRng_) * 0.6f,
                        distN(particleRng_) * 0.6f,
                        distN(particleRng_) * 0.3f
                    );
                } else {
                    p.velocity = rotMat * glm::vec3(
                        distN(particleRng_) * 1.0f,
                        distN(particleRng_) * 1.0f,
                        -dist01(particleRng_) * 0.5f
                    );
                }
            }

            // The particle's own cell of a tiled texture: a random one when the
            // emitter asks for one (ChooseRandomTexture), or the random start
            // of its cell track (RandFlipbookStart). renderM2Particles decides
            // which of the two it means.
            const uint32_t tilesX = std::max<uint16_t>(em.textureCols, 1);
            const uint32_t tilesY = std::max<uint16_t>(em.textureRows, 1);
            const uint32_t totalTiles = tilesX * tilesY;
            if (totalTiles > 1 &&
                (em.flags & (kParticleFlagChooseRandomTexture | kParticleFlagRandFlipbookStart))) {
                distTile = std::uniform_int_distribution<int>(0, static_cast<int>(totalTiles - 1));
                p.tileIndex = static_cast<float>(distTile(particleRng_));
            }

            inst.particles.push_back(p);

            // Diagnostic: log first particle birth per spell effect instance
            if (gpu.isSpellEffect && inst.particles.size() == 1) {
                LOG_INFO("SpellEffect: first particle for '", gpu.name,
                         "' pos=(", p.position.x, ",", p.position.y, ",", p.position.z,
                         ") rate=", rate, " life=", life,
                         " bone=", em.bone, " boneCount=", inst.boneMatrices.size(),
                         " globalSeqs=", gpu.globalSequenceDurations.size());
            }
        }
        // Cap accumulator to avoid bursts after lag
        if (inst.emitterAccumulators[ei] > 2.0f) {
            inst.emitterAccumulators[ei] = 0.0f;
        }
    }
}

void M2Renderer::updateParticles(M2Instance& inst, float dt) {
    if (!inst.cachedModel) return;
    const auto& gpu = *inst.cachedModel;
    const size_t numEm = gpu.particleEmitters.size();
    if (numEm == 0) return;

    // Per-emitter work, hoisted out of the per-particle loop: gravity and the
    // FollowPosition delta depend only on the emitter and the clock, not on
    // the particle, and hundreds of particles share one emitter.
    //
    // Gravity is the authored value. It used to be replaced, when authored
    // zero, by 1.5 or 4.0 u/s^2 - every emitter of the login model authors
    // zero, and the citadel's smoke fell twenty units out of the frame under
    // it. A world tuning that wants weight on a weightless emitter belongs
    // behind !sceneMode_ and a model classifier, never behind "authored 0".
    //
    // FollowPosition (0x4000) carries live particles by the emitter's travel
    // since the previous update, scaled by the authored follow line and
    // clamped at one. The wyrm's frost moves with the bone that spawned it
    // instead of being left along its path - at 8.5 u/s during the swoop an
    // unfollowed particle was 4-12 u behind the spike it belonged to, inside
    // or behind the depth-writing skull. Only particles born before this
    // update take the delta: emitParticles has just run, its births carry
    // life == 0 and already sit at the emitter's current position. The
    // client gates on 2*dt < age, which at sixty frames a second is the same
    // thing to within a frame and at the capture harness's one frame a
    // second would exempt everything younger than two seconds; the birth
    // bookkeeping is exact at any rate.
    struct EmitterStep {
        float gravity = 0.0f;
        glm::vec3 worldPos{0.0f};
        glm::vec3 follow{0.0f};
        bool follows = false;
    };
    constexpr size_t kMaxStackEmitters = 16;
    EmitterStep stepStack[kMaxStackEmitters];
    std::vector<EmitterStep> stepHeap;
    EmitterStep* step = stepStack;
    if (numEm > kMaxStackEmitters) {
        stepHeap.resize(numEm);
        step = stepHeap.data();
    }

    static const bool kPosDiag = envFlagEnabled("WOWEE_SCENE_DIAG");
    const bool posDiag = kPosDiag && sceneMode_;
    bool anyFollow = false;
    for (size_t e = 0; e < numEm; ++e) {
        const auto& pem = gpu.particleEmitters[e];
        step[e] = EmitterStep{};
        step[e].gravity = interpFloat(pem.gravity, inst.animTime, inst.globalSequenceTime,
                                      inst.currentSequenceIndex, gpu.globalSequenceDurations);
        step[e].follows = (pem.flags & kParticleFlagFollowPosition) != 0;
        anyFollow = anyFollow || step[e].follows;
        if (step[e].follows || posDiag) {
            glm::mat4 boneXform(1.0f);
            if (pem.bone < inst.boneMatrices.size()) boneXform = inst.boneMatrices[pem.bone];
            step[e].worldPos = glm::vec3(inst.modelMatrix * boneXform * glm::vec4(pem.position, 1.0f));
        }
    }

    if (anyFollow || posDiag) {
        if (inst.emitterLastWorldPos.size() != numEm) {
            inst.emitterLastWorldPos.assign(numEm, glm::vec3(0.0f));
            inst.emitterLastWorldPosValid = false;
        }
        if (inst.emitterLastWorldPosValid && dt > 0.0f) {
            for (size_t e = 0; e < numEm; ++e) {
                if (!step[e].follows) continue;
                const auto& pem = gpu.particleEmitters[e];
                const glm::vec3 travel = step[e].worldPos - inst.emitterLastWorldPos[e];
                const float speedSpan = pem.followSpeed2 - pem.followSpeed1;
                float fraction = 0.0f;
                if (std::abs(speedSpan) > 1.0e-6f) {
                    const float slope = (pem.followScale2 - pem.followScale1) / speedSpan;
                    const float speed = glm::length(travel) / dt;
                    fraction = pem.followScale1 + (speed - pem.followSpeed1) * slope;
                    fraction = std::clamp(fraction, 0.0f, 1.0f);
                }
                step[e].follow = travel * fraction;
            }
        }
        for (size_t e = 0; e < numEm; ++e) inst.emitterLastWorldPos[e] = step[e].worldPos;
        inst.emitterLastWorldPosValid = true;
    }

    for (size_t i = 0; i < inst.particles.size(); ) {
        auto& p = inst.particles[i];
        const bool bornEarlier = p.life > 0.0f;
        p.life += dt;
        if (p.life >= p.maxLife) {
            // Swap-and-pop removal
            inst.particles[i] = inst.particles.back();
            inst.particles.pop_back();
            continue;
        }
        if (p.emitterIndex >= 0 && static_cast<size_t>(p.emitterIndex) < numEm) {
            const EmitterStep& s = step[p.emitterIndex];
            if (bornEarlier) p.position += s.follow;
            p.velocity.z -= s.gravity * dt;
        }
        p.position += p.velocity * dt;
        i++;
    }

    // WOWEE_SCENE_DIAG=1: every few frames, where each emitter's particles
    // sit relative to the emitter - dz in world Z, and the two camera
    // distances - so a cloud that is drawn behind the surface it belongs to
    // shows up as a number. scripts alongside the diag runs summarise these.
    if (posDiag) {
        static uint32_t diagFrame = 0;
        if (++diagFrame % 5 == 1) {
            std::vector<glm::vec3> sum(numEm, glm::vec3(0.0f));
            std::vector<float> sumDist(numEm, 0.0f);
            std::vector<int> cnt(numEm, 0);
            for (const auto& p : inst.particles) {
                if (p.emitterIndex < 0 || static_cast<size_t>(p.emitterIndex) >= numEm) continue;
                sum[p.emitterIndex] += p.position;
                sumDist[p.emitterIndex] += glm::length(p.position - cachedCamPos_);
                cnt[p.emitterIndex]++;
            }
            for (size_t e = 0; e < numEm; ++e) {
                if (cnt[e] == 0) continue;
                const glm::vec3 mean = sum[e] / static_cast<float>(cnt[e]);
                const glm::vec3 emw = step[e].worldPos;
                LOG_WARNING("PDIAG ", gpu.name, " em#", e, " n=", cnt[e],
                            " animT=", inst.animTime,
                            " emitter=(", emw.x, ",", emw.y, ",", emw.z, ")",
                            " mean=(", mean.x, ",", mean.y, ",", mean.z, ")",
                            " dz=", mean.z - emw.z,
                            " camDistEmitter=", glm::length(emw - cachedCamPos_),
                            " camDistParticles=", sumDist[e] / static_cast<float>(cnt[e]),
                            " grav=", step[e].gravity);
            }
        }
    }
}

// ---------------------------------------------------------------------------
// Ribbon emitter simulation
// ---------------------------------------------------------------------------
void M2Renderer::updateRibbons(M2Instance& inst, const M2ModelGPU& gpu, float dt) {
    if (gpu.isInstancePortal) return;

    const auto& emitters = gpu.ribbonEmitters;
    if (emitters.empty()) return;

    // Grow per-instance state arrays if needed
    if (inst.ribbonEdges.size() != emitters.size()) {
        inst.ribbonEdges.resize(emitters.size());
    }
    if (inst.ribbonEdgeAccumulators.size() != emitters.size()) {
        inst.ribbonEdgeAccumulators.resize(emitters.size(), 0.0f);
    }

    for (size_t ri = 0; ri < emitters.size(); ri++) {
        const auto& em = emitters[ri];
        auto& edges    = inst.ribbonEdges[ri];
        auto& accum    = inst.ribbonEdgeAccumulators[ri];

        // Determine bone world position for spine
        glm::vec3 spineWorld = inst.position;
        // Use referenced bone; fall back to bone 0 if out of range (common for spell effects
        // where ribbon bone fields may be unset/garbage, e.g. bone=4294967295)
        uint32_t boneIdx = em.bone;
        if (boneIdx >= inst.boneMatrices.size() && !inst.boneMatrices.empty()) {
            boneIdx = 0;
        }
        if (boneIdx < inst.boneMatrices.size()) {
            glm::vec4 local(em.position.x, em.position.y, em.position.z, 1.0f);
            spineWorld = glm::vec3(inst.modelMatrix * inst.boneMatrices[boneIdx] * local);
        } else {
            glm::vec4 local(em.position.x, em.position.y, em.position.z, 1.0f);
            spineWorld = glm::vec3(inst.modelMatrix * local);
        }

        // Skip emitters that produce NaN positions (garbage bone/position data)
        if (std::isnan(spineWorld.x) || std::isnan(spineWorld.y) || std::isnan(spineWorld.z))
            continue;

        // Evaluate animated tracks (use first available sequence key, or fallback value)
        auto getFloatVal = [&](const pipeline::M2AnimationTrack& track, float fallback) -> float {
            for (const auto& seq : track.sequences) {
                if (!seq.floatValues.empty()) return seq.floatValues[0];
            }
            return fallback;
        };
        auto getVec3Val = [&](const pipeline::M2AnimationTrack& track, glm::vec3 fallback) -> glm::vec3 {
            for (const auto& seq : track.sequences) {
                if (!seq.vec3Values.empty()) return seq.vec3Values[0];
            }
            return fallback;
        };

        float visibility  = getFloatVal(em.visibilityTrack, 1.0f);
        float heightAbove = getFloatVal(em.heightAboveTrack, 0.5f);
        float heightBelow = getFloatVal(em.heightBelowTrack, 0.5f);
        glm::vec3 color   = getVec3Val(em.colorTrack, glm::vec3(1.0f));
        float alpha       = getFloatVal(em.alphaTrack, 1.0f);

        // Age existing edges and remove expired ones
        for (auto& e : edges) {
            e.age += dt;
            // Apply gravity
            if (em.gravity != 0.0f) {
                e.worldPos.z -= em.gravity * dt * dt * 0.5f;
            }
        }
        while (!edges.empty() && edges.front().age >= em.edgeLifetime) {
            edges.pop_front();
        }

        // Emit new edges based on edgesPerSecond
        if (visibility > 0.5f) {
            accum += em.edgesPerSecond * dt;
            while (accum >= 1.0f) {
                accum -= 1.0f;
                M2Instance::RibbonEdge e;
                e.worldPos    = spineWorld;
                e.color       = color;
                e.alpha       = alpha;
                e.heightAbove = heightAbove;
                e.heightBelow = heightBelow;
                e.age         = 0.0f;
                edges.push_back(e);

                // Diagnostic: log first ribbon edge per spell effect instance+emitter
                if (gpu.isSpellEffect && edges.size() == 1) {
                    LOG_INFO("SpellEffect: ribbon edge[0] for '", gpu.name,
                             "' emitter=", ri, " pos=(", spineWorld.x, ",", spineWorld.y,
                             ",", spineWorld.z, ") hA=", heightAbove, " hB=", heightBelow,
                             " vis=", visibility, " eps=", em.edgesPerSecond,
                             " edgeLife=", em.edgeLifetime, " bone=", em.bone);
                }

                // Cap trail length
                if (edges.size() > 128) edges.pop_front();
            }
        } else {
            accum = 0.0f;
        }
    }
}

// ---------------------------------------------------------------------------
// Ribbon rendering
// ---------------------------------------------------------------------------
void M2Renderer::renderM2Ribbons(VkCommandBuffer cmd, VkDescriptorSet perFrameSet) {
    if (!ribbonPipeline_ || !ribbonAdditivePipeline_ || !ribbonVB_ || !ribbonVBMapped_) return;
    // Diagnostic: WOWEE_M2_NO_RIBBONS=1 drops every M2 ribbon trail draw.
    static const bool kNoRibbons = envFlagEnabled("WOWEE_M2_NO_RIBBONS");
    if (kNoRibbons) return;

    // Build camera right vector for billboard orientation
    // For ribbons we orient the quad strip along the spine with screen-space up.
    // Simple approach: use world-space Z=up for the ribbon cross direction.
    const glm::vec3 upWorld(0.0f, 0.0f, 1.0f);

    float* dst     = static_cast<float*>(ribbonVBMapped_);
    size_t written = 0;

    ribbonDraws_.clear();
    auto& draws = ribbonDraws_;

    for (const auto& inst : instances) {
        if (!inst.cachedModel) continue;
        const auto& gpu = *inst.cachedModel;
        if (gpu.isInstancePortal) continue;
        if (gpu.ribbonEmitters.empty()) continue;

        for (size_t ri = 0; ri < gpu.ribbonEmitters.size(); ri++) {
            if (ri >= inst.ribbonEdges.size()) continue;
            const auto& edges = inst.ribbonEdges[ri];
            if (edges.size() < 2) continue;

            const auto& em = gpu.ribbonEmitters[ri];

            // Select blend pipeline based on material blend mode
            bool additive = false;
            if (em.materialIndex < gpu.batches.size()) {
                additive = (gpu.batches[em.materialIndex].blendMode >= 3);
            }
            VkPipeline pipe = additive ? ribbonAdditivePipeline_ : ribbonPipeline_;

            // Descriptor set for texture
            VkDescriptorSet texSet = (ri < gpu.ribbonTexSets.size())
                                     ? gpu.ribbonTexSets[ri] : VK_NULL_HANDLE;
            if (!texSet) {
                if (gpu.isSpellEffect) {
                    static bool ribbonTexWarn = false;
                    if (!ribbonTexWarn) {
                        LOG_WARNING("SpellEffect: ribbon[", ri, "] for '", gpu.name,
                                    "' has null texSet - descriptor pool may be exhausted");
                        ribbonTexWarn = true;
                    }
                }
                continue;
            }

            uint32_t firstVert = static_cast<uint32_t>(written);

            // Emit triangle strip: 2 verts per edge (top + bottom)
            for (size_t ei = 0; ei < edges.size(); ei++) {
                if (written + 2 > MAX_RIBBON_VERTS) break;
                const auto& e = edges[ei];
                float t = (em.edgeLifetime > 0.0f)
                          ? 1.0f - (e.age / em.edgeLifetime) : 1.0f;
                float a = e.alpha * t;
                float u = static_cast<float>(ei) / static_cast<float>(edges.size() - 1);

                // Top vertex (above spine along upWorld)
                glm::vec3 top = e.worldPos + upWorld * e.heightAbove;
                dst[written * 9 + 0] = top.x;
                dst[written * 9 + 1] = top.y;
                dst[written * 9 + 2] = top.z;
                dst[written * 9 + 3] = e.color.r;
                dst[written * 9 + 4] = e.color.g;
                dst[written * 9 + 5] = e.color.b;
                dst[written * 9 + 6] = a;
                dst[written * 9 + 7] = u;
                dst[written * 9 + 8] = 0.0f; // v = top
                written++;

                // Bottom vertex (below spine)
                glm::vec3 bot = e.worldPos - upWorld * e.heightBelow;
                dst[written * 9 + 0] = bot.x;
                dst[written * 9 + 1] = bot.y;
                dst[written * 9 + 2] = bot.z;
                dst[written * 9 + 3] = e.color.r;
                dst[written * 9 + 4] = e.color.g;
                dst[written * 9 + 5] = e.color.b;
                dst[written * 9 + 6] = a;
                dst[written * 9 + 7] = u;
                dst[written * 9 + 8] = 1.0f; // v = bottom
                written++;
            }

            uint32_t vertCount = static_cast<uint32_t>(written) - firstVert;
            if (vertCount >= 4) {
                draws.push_back({.texSet = texSet, .pipeline = pipe, .firstVertex = firstVert, .vertexCount = vertCount});
            } else {
                // Rollback if too few verts
                written = firstVert;
            }
        }
    }

    // Periodic diagnostic: spell ribbon draw count
    {
        static uint32_t ribbonDiagFrame_ = 0;
        if (++ribbonDiagFrame_ % 300 == 1) {
            size_t spellRibbonDraws = 0;
            size_t spellRibbonVerts = 0;
            for (const auto& inst : instances) {
                if (!inst.cachedModel || !inst.cachedModel->isSpellEffect) continue;
                for (const auto& ribbonEdge : inst.ribbonEdges) {
                    if (ribbonEdge.size() >= 2) {
                        spellRibbonDraws++;
                        spellRibbonVerts += ribbonEdge.size() * 2;
                    }
                }
            }
            if (spellRibbonDraws > 0 || !draws.empty()) {
                LOG_INFO("SpellEffect: ", spellRibbonDraws, " spell ribbon strips (",
                         spellRibbonVerts, " verts), total draws=", draws.size(),
                         " written=", written);
            }
        }
    }

    if (draws.empty() || written == 0) return;

    VkExtent2D ext = vkCtx_->getSwapchainExtent();
    VkViewport vp{};
    vp.x = 0; vp.y = 0;
    vp.width  = static_cast<float>(ext.width);
    vp.height = static_cast<float>(ext.height);
    vp.minDepth = 0.0f; vp.maxDepth = 1.0f;
    VkRect2D sc{};
    sc.offset = {.x = 0, .y = 0};
    sc.extent = ext;
    vkCmdSetViewport(cmd, 0, 1, &vp);
    vkCmdSetScissor(cmd, 0, 1, &sc);

    VkPipeline lastPipe = VK_NULL_HANDLE;
    for (const auto& dc : draws) {
        if (dc.pipeline != lastPipe) {
            vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, dc.pipeline);
            vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
                                    ribbonPipelineLayout_, 0, 1, &perFrameSet, 0, nullptr);
            lastPipe = dc.pipeline;
        }
        vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
                                ribbonPipelineLayout_, 1, 1, &dc.texSet, 0, nullptr);
        VkDeviceSize offset = 0;
        vkCmdBindVertexBuffers(cmd, 0, 1, &ribbonVB_, &offset);
        vkCmdDraw(cmd, dc.vertexCount, 1, dc.firstVertex, 0);
    }
}

void M2Renderer::renderM2Particles(VkCommandBuffer cmd, VkDescriptorSet perFrameSet) {
    if (!particlePipeline_ || !m2ParticleVB_) return;
    // Diagnostic: WOWEE_M2_NO_PARTICLES=1 drops every M2 particle draw, which
    // tells a particle artifact apart from a skinned-geometry one.
    static const bool kNoParticles = envFlagEnabled("WOWEE_M2_NO_PARTICLES");
    if (kNoParticles) return;

    // How many pixels one world unit of sprite covers at one unit of distance.
    //
    // The shader divides this by the particle's distance, so it has to carry
    // the projection: a sprite of half-extent s at distance d covers
    // s * height * (1/tan(fovY/2)) / d pixels. This was a constant 500, which
    // is right at no resolution and no field of view - at 1280x720 and sixty
    // degrees the figure is about 1250 - and being two and a half times too
    // small is why flame fixtures need the floors below at all. See
    // pointSizePixelsPerUnit for the fallback.
    const float pointSizeFactor = pointSizePixelsPerUnit();
    // The floors below were tuned against the old constant and are written in
    // world units, so converting the factor without converting them would
    // change what they mean. They are really a statement about pixels - a
    // candle flame must not land sub-pixel - so scale them to keep the pixel
    // size they were tuned to.
    const float floorScale = 500.0f / pointSizeFactor;

    // Collect all particles from all instances, grouped by texture+blend
    // Reuse persistent map - clear each group's vertex data but keep bucket structure.
    for (auto& [k, g] : particleGroups_) {
        g.vertexData.clear();
        g.preAllocSet = VK_NULL_HANDLE;
    }
    auto& groups = particleGroups_;

    size_t totalParticles = 0;

    for (auto& inst : instances) {
        if (inst.particles.empty()) continue;
        if (!inst.cachedModel) continue;
        const auto& gpu = *inst.cachedModel;
        if (gpu.isInstancePortal) continue;


        // Cache the last emitter's per-emitter state so adjacent particles
        // sharing an emitter (the common case - particles from one source
        // cluster together) skip the texture/key/map-lookup work entirely.
        int lastEmitterIdx = -1;
        VkTexture* cachedTex = nullptr;
        uint16_t cachedTilesX = 1, cachedTilesY = 1;
        uint32_t cachedTotalTiles = 1;
        uint16_t cachedBlendType = 0;
        const pipeline::M2ParticleEmitter* cachedEm = nullptr;
        ParticleGroup* cachedGroup = nullptr;
        // How a tiled texture's cell is chosen, per emitter: the authored
        // cell track over the particle's life, offset by the particle's own
        // random start when RandFlipbookStart is set; or, with no track, the
        // particle's random cell when ChooseRandomTexture is set; else cell 0.
        float cachedTilesFloat = 1.0f;
        bool cachedHasCellTrack = false;
        bool cachedRandStart = false;
        bool cachedRandomCell = false;

        for (const auto& p : inst.particles) {
            if (p.emitterIndex < 0 || p.emitterIndex >= static_cast<int>(gpu.particleEmitters.size())) continue;

            if (p.emitterIndex != lastEmitterIdx) {
                lastEmitterIdx = p.emitterIndex;
                cachedEm = &gpu.particleEmitters[p.emitterIndex];

                cachedTex = whiteTexture_.get();
                if (p.emitterIndex < static_cast<int>(gpu.particleTextures.size())) {
                    cachedTex = gpu.particleTextures[p.emitterIndex];
                }
                cachedTilesX = std::max<uint16_t>(cachedEm->textureCols, 1);
                cachedTilesY = std::max<uint16_t>(cachedEm->textureRows, 1);
                cachedTotalTiles = static_cast<uint32_t>(cachedTilesX) *
                                   static_cast<uint32_t>(cachedTilesY);
                cachedBlendType = cachedEm->blendingType;
                ParticleGroupKey key{.texture = cachedTex, .blendType = static_cast<uint8_t>(cachedBlendType), .tilesX = cachedTilesX, .tilesY = cachedTilesY};
                cachedGroup = &groups[key];
                cachedGroup->texture = cachedTex;
                cachedGroup->blendType = cachedBlendType;
                cachedGroup->tilesX = cachedTilesX;
                cachedGroup->tilesY = cachedTilesY;
                if (cachedGroup->preAllocSet == VK_NULL_HANDLE &&
                    p.emitterIndex < static_cast<int>(gpu.particleTexSets.size())) {
                    cachedGroup->preAllocSet = gpu.particleTexSets[p.emitterIndex];
                }

                cachedTilesFloat = static_cast<float>(cachedTotalTiles);
                cachedHasCellTrack = cachedTotalTiles > 1 && !cachedEm->headCellTrack.floatValues.empty();
                cachedRandStart = (cachedEm->flags & kParticleFlagRandFlipbookStart) != 0;
                cachedRandomCell = cachedTotalTiles > 1 &&
                                   (cachedEm->flags & kParticleFlagChooseRandomTexture) != 0;
            }

            const auto& em = *cachedEm;
            float lifeRatio = p.life / std::max(p.maxLife, 0.001f);
            glm::vec3 color = interpFBlockVec3(em.particleColor, lifeRatio);
            float alpha = std::min(interpFBlockFloat(em.particleAlpha, lifeRatio), 1.0f);
            float rawScale = interpFBlockFloat(em.particleScale, lifeRatio);

            // The world's dampeners, and not a scene's. See setSceneMode: an
            // authored set piece is not a field of doodads to be tamed, and a
            // twentieth of the alpha is the difference between a frost wyrm
            // wreathed in light and a bare skeleton.
            if (!sceneMode_ && !gpu.isSpellEffect && !gpu.isFireflyEffect &&
                !gpu.isLanternLike && !gpu.isTorch && !gpu.isBrazierOrFire &&
                !gpu.isKoboldFlame) {
                color = glm::mix(color, glm::vec3(1.0f), 0.7f);
                if (rawScale > 2.0f) alpha *= 0.02f;
                if (cachedBlendType == 3 || cachedBlendType == 4) alpha *= 0.05f;
            }
            // Flame fixtures: the authored curves can leave a particle with
            // effectively no colour or alpha for most of its life. CHANDELIER01
            // ramps scale from zero over a six second life and its candles spend
            // nearly all of that time contributing nothing - and because these
            // draw additively, a near-black particle adds literally nothing to
            // the frame. Floor colour and alpha so a lit fixture always shows
            // flame. Floors only lift the dim end, leaving torches and candles
            // that already read correctly untouched.
            if (gpu.isLanternLike || gpu.isTorch ||
                gpu.isBrazierOrFire || gpu.isKoboldFlame) {
                color = glm::max(color, glm::vec3(0.50f, 0.26f, 0.09f));
                alpha = std::max(alpha, 0.30f);
            }

            float scale = rawScale;
            if (gpu.isSpellEffect) {
                scale = std::max(rawScale * 1.5f, 0.15f * floorScale);
            } else if (!gpu.isFireflyEffect) {
                // The world's cap, and not a scene's - see setSceneMode: a
                // set piece's emitters are authored at the size the picture
                // wants, and the login screen's frost burst runs past it.
                if (!sceneMode_) scale = std::min(rawScale, 1.5f);
                // Candle flames are authored at a fraction of a unit, which lands
                // sub-pixel at any normal viewing distance - the fixture glows
                // with no visible flame. Small effect-heavy models dodge this by
                // being classified as spell effects (three or more emitters and
                // few vertices) and picking up that path's floor, but a
                // chandelier is a real fixture at 370 vertices and misses the
                // cut, so its five candles rendered as nothing. Give flame
                // fixtures the same floor without making them spell effects,
                // which would also change how they blend.
                if (gpu.isLanternLike || gpu.isTorch ||
                    gpu.isBrazierOrFire || gpu.isKoboldFlame) {
                    scale = std::max(scale, 0.15f * floorScale);
                }
            }

            auto& vd = cachedGroup->vertexData;
            vd.push_back(p.position.x);
            vd.push_back(p.position.y);
            vd.push_back(p.position.z);
            vd.push_back(color.r);
            vd.push_back(color.g);
            vd.push_back(color.b);
            vd.push_back(alpha);
            vd.push_back(scale * pointSizeFactor);
            float tileIndex = 0.0f;
            if (cachedHasCellTrack) {
                tileIndex = std::floor(interpFBlockFloat(em.headCellTrack, lifeRatio));
                if (cachedRandStart) tileIndex += p.tileIndex;
                tileIndex = std::fmod(tileIndex, cachedTilesFloat);
                if (tileIndex < 0.0f) tileIndex += cachedTilesFloat;
            } else if (cachedRandomCell) {
                tileIndex = p.tileIndex;
            }
            vd.push_back(tileIndex);
            totalParticles++;
        }
    }

    // Periodic diagnostic: spell effect particle count
    {
        static uint32_t spellParticleDiagFrame_ = 0;
        if (++spellParticleDiagFrame_ % 300 == 1) {
            size_t spellPtc = 0;
            for (const auto& inst : instances) {
                if (inst.cachedModel && inst.cachedModel->isSpellEffect)
                    spellPtc += inst.particles.size();
            }
            if (spellPtc > 0) {
                LOG_INFO("SpellEffect: rendering ", spellPtc, " spell particles (",
                         totalParticles, " total)");
            }
        }
    }

    if (totalParticles == 0) return;

    // Bind per-frame set (set 0) for particle pipeline
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
                            particlePipelineLayout_, 0, 1, &perFrameSet, 0, nullptr);

    VkDeviceSize vbOffset = 0;
    vkCmdBindVertexBuffers(cmd, 0, 1, &m2ParticleVB_, &vbOffset);

    VkPipeline currentPipeline = VK_NULL_HANDLE;

    for (auto& [key, group] : groups) {
        if (group.vertexData.empty()) continue;

        uint8_t blendType = group.blendType;
        VkPipeline desiredPipeline = (blendType == 3 || blendType == 4)
            ? particleAdditivePipeline_ : particlePipeline_;
        if (desiredPipeline != currentPipeline) {
            vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, desiredPipeline);
            currentPipeline = desiredPipeline;
        }

        // Use pre-allocated stable descriptor set; fall back to per-frame alloc only if unavailable
        VkDescriptorSet texSet = group.preAllocSet;
        if (texSet == VK_NULL_HANDLE) {
            // Fallback: allocate per-frame (pool exhaustion risk - should not happen in practice)
            VkDescriptorSetAllocateInfo ai{.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};
            ai.descriptorPool = materialDescPool_;
            ai.descriptorSetCount = 1;
            ai.pSetLayouts = &particleTexLayout_;
            if (vkAllocateDescriptorSets(vkCtx_->getDevice(), &ai, &texSet) == VK_SUCCESS) {
                VkTexture* tex = (group.texture && group.texture->isValid())
                    ? group.texture : whiteTexture_.get();
                if (!tex || !tex->isValid()) continue;
                VkDescriptorImageInfo imgInfo = tex->descriptorInfo();
                VkWriteDescriptorSet write{.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
                write.dstSet = texSet;
                write.dstBinding = 0;
                write.descriptorCount = 1;
                write.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
                write.pImageInfo = &imgInfo;
                vkUpdateDescriptorSets(vkCtx_->getDevice(), 1, &write, 0, nullptr);
            }
        }
        if (texSet != VK_NULL_HANDLE) {
            vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
                                    particlePipelineLayout_, 1, 1, &texSet, 0, nullptr);
        }

        // Push constants: tileCount + alphaKey + edgeMask. The round-off is
        // the world's; an authored scene's sprites keep their texture's shape.
        struct { float tileX, tileY; int alphaKey; int edgeMask; } pc = {
            .tileX = static_cast<float>(group.tilesX), .tileY = static_cast<float>(group.tilesY),
            .alphaKey = (blendType == 1) ? 1 : 0,
            .edgeMask = sceneMode_ ? 0 : 1
        };
        vkCmdPushConstants(cmd, particlePipelineLayout_, VK_SHADER_STAGE_FRAGMENT_BIT, 0,
                           sizeof(pc), &pc);

        // Upload and draw in chunks
        size_t count = group.vertexData.size() / 9;
        size_t offset = 0;
        while (offset < count) {
            size_t batch = std::min(count - offset, MAX_M2_PARTICLES);
            memcpy(m2ParticleVBMapped_, &group.vertexData[offset * 9], batch * 9 * sizeof(float));
            vkCmdDraw(cmd, static_cast<uint32_t>(batch), 1, 0, 0);
            offset += batch;
        }
    }
}

void M2Renderer::renderSmokeParticles(VkCommandBuffer cmd, VkDescriptorSet perFrameSet) {
    if (smokeParticles.empty() || !smokePipeline_ || !smokeVB_) return;

    // Build vertex data: pos(3) + lifeRatio(1) + size(1) + isSpark(1) per particle
    size_t count = std::min(smokeParticles.size(), static_cast<size_t>(MAX_SMOKE_PARTICLES));
    float* dst = static_cast<float*>(smokeVBMapped_);
    for (size_t i = 0; i < count; i++) {
        const auto& p = smokeParticles[i];
        *dst++ = p.position.x;
        *dst++ = p.position.y;
        *dst++ = p.position.z;
        *dst++ = p.life / p.maxLife;
        *dst++ = p.size;
        *dst++ = p.isSpark;
    }

    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, smokePipeline_);
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
                            smokePipelineLayout_, 0, 1, &perFrameSet, 0, nullptr);

    // Push constant: screenHeight
    float screenHeight = static_cast<float>(vkCtx_->getSwapchainExtent().height);
    vkCmdPushConstants(cmd, smokePipelineLayout_, VK_SHADER_STAGE_VERTEX_BIT, 0,
                       sizeof(float), &screenHeight);

    VkDeviceSize offset = 0;
    vkCmdBindVertexBuffers(cmd, 0, 1, &smokeVB_, &offset);
    vkCmdDraw(cmd, static_cast<uint32_t>(count), 1, 0, 0);
}

} // namespace rendering
} // namespace wowee
