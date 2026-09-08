#pragma once

namespace wowee::addons {
// The production Lua animation bootstrap, shared with deterministic VM tests.
// Keep one implementation: LuaEngine executes this exact literal.
inline constexpr const char kAnimationGroupLua[] =
        "local mt = __WoweeFrameMT\n"
        "__WoweePlayingAnimations = {}\n"
        "local playing = __WoweePlayingAnimations\n"

        "local animMeta = {}\n"
        "animMeta.__index = animMeta\n"
        "function animMeta:SetDuration(d) self.duration = d or 0 end\n"
        "function animMeta:GetDuration() return self.duration or 0 end\n"
        "function animMeta:SetChange(c) self.change = c end\n"
        "function animMeta:GetChange() return self.change end\n"
        "function animMeta:SetFromAlpha(a) self.fromAlpha = a end\n"
        "function animMeta:SetToAlpha(a) self.toAlpha = a end\n"
        "function animMeta:SetOffset(x, y) self.offsetX, self.offsetY = x, y end\n"
        "function animMeta:GetOffset() return self.offsetX or 0, self.offsetY or 0 end\n"
        // An animation carries scripts of its own, and OnFinished on the
        // *animation* is how FrameXML hides a faded-out frame:
        // alertframes.xml puts `self:GetRegionParent():Hide()` there. Only the
        // group's OnFinished was ever called, so an achievement banner faded to
        // nothing and stayed on screen forever.
        "function animMeta:SetScript(k, f) self[k] = f end\n"
        "function animMeta:GetScript(k) return self[k] end\n"
        "function animMeta:GetRegionParent() return self.group and self.group.parent end\n"
        "function animMeta:SetOrder(o) self.order = o or 1 end\n"
        "function animMeta:GetOrder() return self.order or 1 end\n"
        "function animMeta:SetStartDelay(d) self.startDelay = d or 0 end\n"
        "function animMeta:GetStartDelay() return self.startDelay or 0 end\n"
        "function animMeta:SetEndDelay(d) self.endDelay = d or 0 end\n"
        "function animMeta:SetSmoothing(s) self.smoothing = s end\n"
        "function animMeta:SetScale(x, y) self.scaleX, self.scaleY = x, y end\n"
        "function animMeta:SetDegrees(d) self.degrees = d end\n"
        "function animMeta:GetProgress() return self.progress or 0 end\n"
        // The same progress with the animation's own easing applied, which is
        // what anything driving a value off an animation actually wants.
        // The calendar reads it directly -
        // flashTexture:SetAlpha(CalendarViewEventFlashTimer:GetSmoothProgress())
        // on an <Animation smoothing="OUT"> - and a missing *method* is not a
        // nil to be checked but a hard error, so the whole event view went
        // down on the line that makes a highlight pulse.
        "function animMeta:GetSmoothProgress()\n"
        "    local t = self.progress or 0\n"
        "    if t < 0 then t = 0 elseif t > 1 then t = 1 end\n"
        "    local s = self.smoothing\n"
        "    if s == 'IN' then return t * t end\n"
        "    if s == 'OUT' then return t * (2 - t) end\n"
        "    if s == 'IN_OUT' then\n"
        "        if t < 0.5 then return 2 * t * t end\n"
        "        local u = 1 - t\n"
        "        return 1 - 2 * u * u\n"
        "    end\n"
        "    if s == 'OUT_IN' then\n"
        "        if t < 0.5 then local u = t * 2 return u * (2 - u) * 0.5 end\n"
        "        local u = (t - 0.5) * 2\n"
        "        return 0.5 + u * u * 0.5\n"
        "    end\n"
        // No smoothing named, or one this does not model: the linear progress
        // is the honest answer and reads as a steady fade rather than nothing.
        "    return t\n"
        "end\n"
        "function animMeta:GetElapsed() return self.elapsed or 0 end\n"
        "function animMeta:SetParent(p) self.parent = p end\n"
        "function animMeta:GetRegionParent() return self.group and self.group.parent end\n"
        "function animMeta:SetTarget(t) self.target = t end\n"
        "function animMeta:IsDelaying() return (self.elapsed or 0) < (self.startDelay or 0) end\n"
        "function animMeta:IsPlaying() return self.group and self.group:IsPlaying() end\n"
        // An animation answers Play, Pause, Stop and Finish as well as its
        // group does, and acts on the group when it does. Leaving these off
        // was worse than having no animations at all: an undefined
        // TutorialFrameCallOutPulser was a harmless fallback object that
        // swallowed :Stop(), and a real table without the method is a hard
        // error that took the whole file down with it.
        "function animMeta:Play()   if self.group then self.group:Play()   end end\n"
        "function animMeta:Stop()   if self.group then self.group:Stop()   end end\n"
        "function animMeta:Pause()  if self.group then self.group:Pause()  end end\n"
        "function animMeta:Resume() if self.group then self.group:Resume() end end\n"
        "function animMeta:Finish() if self.group then self.group:Finish() end end\n"
        "function animMeta:GetSmoothing() return self.smoothing end\n"
        "function animMeta:GetOrder() return self.order or 1 end\n"
        "function animMeta:SetScript(k, f) self[k] = f end\n"
        "function animMeta:GetScript(k) return self[k] end\n"

        "local groupMeta = {}\n"
        "groupMeta.__index = groupMeta\n"
        "function groupMeta:CreateAnimation(kind, name)\n"
        "    local a = setmetatable({kind = kind or 'Alpha', group = self,\n"
        "                            duration = 0, order = 1, startDelay = 0}, animMeta)\n"
        "    table.insert(self.animations, a)\n"
        "    if name then _G[name] = a end\n"
        "    return a\n"
        "end\n"
        "function groupMeta:GetAnimations() return unpack(self.animations) end\n"
        "function groupMeta:SetLooping(m) self.looping = m end\n"
        "function groupMeta:GetLooping() return self.looping or 'NONE' end\n"
        "function groupMeta:IsPlaying() return self.isPlaying == true end\n"
        "function groupMeta:IsDone() return self.isPlaying ~= true end\n"
        "function groupMeta:SetScript(k, f) self[k] = f end\n"
        "function groupMeta:GetScript(k) return self[k] end\n"
        "function groupMeta:HookScript(k, f)\n"
        "    local prev = self[k]\n"
        "    self[k] = function(...) if prev then prev(...) end f(...) end\n"
        "end\n"
        "function groupMeta:SetParent(p) self.parent = p end\n"
        "function groupMeta:GetParent() return self.parent end\n"
        // Same-order animations are parallel; each successive order adds a
        // stage. Sparse order numbers still name consecutive stages.
        "local function schedule(g)\n"
        "    local spans = {}\n"
        "    for _, a in ipairs(g.animations) do\n"
        "        local order = a.order or 1\n"
        "        local t = (a.startDelay or 0) + (a.duration or 0)\n"
        "        if t > (spans[order] or 0) then spans[order] = t end\n"
        "    end\n"
        "    local orders = {}\n"
        "    for order in pairs(spans) do table.insert(orders, order) end\n"
        "    table.sort(orders)\n"
        "    local starts = {}\n"
        "    local total = 0\n"
        "    for _, order in ipairs(orders) do\n"
        "        starts[order] = total\n"
        "        total = total + spans[order]\n"
        "    end\n"
        "    return starts, total\n"
        "end\n"
        "function groupMeta:GetDuration()\n"
        "    local _, total = schedule(self)\n"
        "    return total\n"
        "end\n"
        // The frame's alpha at the moment Play is called is what an Alpha
        // animation's change is relative to. Captured here rather than at
        // creation, because a group replayed later starts from wherever the
        // frame is then.
        "function groupMeta:Play()\n"
        "    self.isPlaying = true\n"
        "    self.reversed = false\n"
        "    self.elapsed = 0\n"
        "    self.baseAlpha = self.parent and self.parent:GetAlpha() or 1\n"
        "    for _, a in ipairs(self.animations) do a.elapsed = 0 a.progress = 0 a.finished = nil end\n"
        "    playing[self] = true\n"
        "    if self.OnPlay then self:OnPlay() end\n"
        "end\n"
        "function groupMeta:Stop()\n"
        "    self.paused = nil\n"
        "    self.isPlaying = false\n"
        "    playing[self] = nil\n"
        // Put back what the animations moved, or a stopped group leaves the
        // frame transparent or displaced with nothing to restore it.
        "    if self.parent then\n"
        "        if self.baseAlpha then self.parent:SetAlpha(self.baseAlpha) end\n"
        "        __WoweeSetAnimOffset(self.parent, 0, 0)\n"
        "    end\n"
        "    if self.OnStop then self:OnStop() end\n"
        "end\n"
        "function groupMeta:Finish()\n"
        "    self.isPlaying = false\n"
        "    playing[self] = nil\n"
        "    if self.OnFinished then self:OnFinished() end\n"
        "end\n"
        "function groupMeta:Pause() self.paused = true end\n"
        "function groupMeta:Resume() self.paused = nil end\n"

        // Stop every group on this frame at once. A frame method rather than a
        // group one, and the only animation call FrameXML makes without
        // holding the group: blizzard_glyphui.lua does sparkle:StopAnimating()
        // whenever a glyph slot empties, which is every time the glyph tab is
        // opened on a character with a free socket. Missing, that raised and
        // took GlyphFrame_UpdateGlyphSlot with it.
        "function __WoweeStopAnimating(self)\n"
        "    if self.__animGroups then\n"
        "        for _, g in ipairs(self.__animGroups) do g:Stop() end\n"
        "    end\n"
        "end\n"
        "mt.StopAnimating = __WoweeStopAnimating\n"

        // A texture animates itself as readily as a frame does, so this is a
        // plain function both get rather than a frame method. The achievement
        // banner's glow and shine each declare an animIn on the *texture*, and
        // AlertFrame_AnimateIn plays the frame's and both of theirs one after
        // another - so a region without this raised on the second call and lost
        // every line after it, the fade-out that hides the banner included.
        //
        // Regions carry their methods on themselves rather than on a shared
        // metatable, so installRegionMethods copies these two off the globals.
        "function __WoweeCreateAnimationGroup(self, name)\n"
        "    local g = setmetatable({parent = self, animations = {}}, groupMeta)\n"
        "    if name then _G[name] = g end\n"
        "    self.__animGroups = self.__animGroups or {}\n"
        "    table.insert(self.__animGroups, g)\n"
        "    return g\n"
        "end\n"
        "mt.CreateAnimationGroup = __WoweeCreateAnimationGroup\n"

        // Advanced once a frame from dispatchOnUpdate.
        "function __WoweeTickAnimations(elapsed)\n"
        "    for g in pairs(playing) do\n"
        "        if g.paused then\n"
        "        else\n"
        "            g.elapsed = (g.elapsed or 0) + elapsed\n"
        "            local starts, duration = schedule(g)\n"
        "            local anyRunning = g.elapsed < duration\n"
        "            local dx, dy = 0, 0\n"
        "            local alpha = g.baseAlpha or 1\n"
        "            for _, a in ipairs(g.animations) do\n"
        "                local stageElapsed = g.elapsed - (starts[a.order or 1] or 0)\n"
        "                a.elapsed = stageElapsed > 0 and stageElapsed or 0\n"
        "                local t = a.elapsed - (a.startDelay or 0)\n"
        "                local d = a.duration or 0\n"
        "                if stageElapsed < 0 or t < 0 then\n"
        "                elseif d <= 0 then\n"
        "                    a.progress = 1\n"
        "                    if not a.finished then\n"
        "                        a.finished = true\n"
        "                        if a.OnFinished then a:OnFinished() end\n"
        "                    end\n"
        "                else\n"
        "                    local p = t / d\n"
        "                    local done = false\n"
        "                    if p >= 1 then p = 1 done = true end\n"
        "                    if g.reversed then p = 1 - p end\n"
        "                    a.progress = p\n"
        // Once per run, and before the group finishes, because the frame this
        // hides is the one the group is still animating.
        "                    if done and not a.finished then\n"
        "                        a.finished = true\n"
        "                        if a.OnFinished then a:OnFinished() end\n"
        "                    end\n"
        "                    if a.kind == 'Alpha' then\n"
        "                        if a.fromAlpha and a.toAlpha then\n"
        "                            alpha = a.fromAlpha + (a.toAlpha - a.fromAlpha) * p\n"
        "                        elseif a.change then\n"
        "                            alpha = (g.baseAlpha or 1) + a.change * p\n"
        "                        end\n"
        "                    elseif a.kind == 'Translation' then\n"
        "                        dx = dx + (a.offsetX or 0) * p\n"
        "                        dy = dy + (a.offsetY or 0) * p\n"
        "                    elseif a.kind == 'Scale' then\n"
        "                        local sx = a.scaleX\n"
        "                        if sx and g.parent then\n"
        "                            g.parent:SetScale(1 + (sx - 1) * p)\n"
        "                        end\n"
        "                    end\n"
        "                end\n"
        "            end\n"
        "            if g.parent then\n"
        "                if alpha < 0 then alpha = 0 elseif alpha > 1 then alpha = 1 end\n"
        "                g.parent:SetAlpha(alpha)\n"
        "                __WoweeSetAnimOffset(g.parent, dx, dy)\n"
        "            end\n"
        "            if not anyRunning then\n"
        "                local mode = g.looping or 'NONE'\n"
        "                if mode == 'REPEAT' or mode == 'BOUNCE' then\n"
        // BOUNCE plays back the way it came; REPEAT starts over. Either way the
        // clocks reset, or the next round finishes instantly.
        "                    if mode == 'BOUNCE' then g.reversed = not g.reversed end\n"
        "                    g.elapsed = 0\n"
        "                    for _, a in ipairs(g.animations) do a.elapsed = 0 a.finished = nil end\n"
        "                    if g.OnLoop then g:OnLoop() end\n"
        "                else\n"
        "                    g:Finish()\n"
        "                end\n"
        "            end\n"
        "        end\n"
        "    end\n"
        "end\n";

} // namespace wowee::addons
