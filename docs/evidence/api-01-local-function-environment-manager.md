# API-01: `LOCAL_Function_Environment_Manager`

The supplied `FrameXML/RestrictedExecution.lua`, SHA-256
`d91023d0f8c590cf3168d42ba439c480862fa0c773981a05e69af1051b61ad77`,
proves that `LOCAL_Function_Environment_Manager` is a lexical helper rather
than a client API.

`CreateRestrictedEnvironment` is declared local at line 574. It declares its
local `manage` closure at line 604 and returns `result, manage` at line 645.
Lines 722–723 assign those two values to two locals:

```lua
local LOCAL_Function_Environment, LOCAL_Function_Environment_Manager =
    CreateRestrictedEnvironment(LOCAL_Restricted_Global_Functions);
```

The calls at lines 777 and 824 therefore resolve to the second local. An exact
name search found no other supplied source use or dynamic registration.

The static scanner previously recognized a single identifier on the left side
of an assignment but not a comma-separated local declaration. It consequently
counted both lexical calls as unresolved global candidates. Its regression now
binds a second local with the same shape, invokes it, and retains a separate
unresolved global call. The local must disappear from the report while the real
candidate remains.

No Lua API or fallback was added. The existing disposition “false positive:
local helper” is confirmed by the supplied source and scanner behavior.
