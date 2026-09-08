#!/usr/bin/env python3
"""Bounded fixtures for the source inventory and configured-test boundary."""
import json
import unittest

from capability_inventory import cvar_inventory, widget_inventory, ctest_inventory


class CvarInventoryTests(unittest.TestCase):
    def test_bindings_defaults_alias_branches_prefixes_and_reads(self):
        source = '''
// {"fake", "fake_setting"}
constexpr ClientCVarBinding kClientCVars[] = {
    {"density", "detail", 100.0 / 64.0},
    {"visible", "show"}
};
static void pushCvarDefault(lua_State* L, const std::string& n) {
    // if (n == "fake") { lua_pushstring(L, "9"); return; }
    if (n == "visible" || n == "alias") { lua_pushstring(L, "1"); return; }
    if (n == "dynamic") { lua_pushstring(L, windowSize()); return; }
    if (n.rfind("sound_enable", 0) == 0) { lua_pushstring(L, "1"); return; }
    { lua_pushstring(L, "0"); return; }
}
auto setting = storedCVarValue("visible", "1");
'''
        rows = cvar_inventory({"fixture.cpp": source})
        self.assertEqual([r["name"] for r in rows["client_bindings"]], ["density", "visible"])
        self.assertEqual(rows["client_bindings"][0]["scale_expression"], "100.0 / 64.0")
        defaults = {r["name"]: r["literal_default"] for r in rows["named_default_candidates"]}
        self.assertEqual(defaults, {"visible": "1", "alias": "1", "dynamic": None})
        self.assertEqual([r["literal_default"] for r in rows["default_dispatch_rules"]], ["1", "0"])
        self.assertEqual(rows["stored_value_reads"][0]["name"], "visible")
        self.assertEqual(rows["client_bindings"][0]["evidence"], "fixture.cpp:4")

    def test_comments_and_unrelated_tables_do_not_create_bindings(self):
        result = cvar_inventory({"other.cpp": 'const char* url = "https://example.invalid";\nconst auto other = {{"fake", "other"}};\n/* storedCVarValue("ignored", "1") */'})
        self.assertEqual(result["client_bindings"], [])
        self.assertEqual(result["stored_value_reads"], [])


class WidgetInventoryTests(unittest.TestCase):
    def test_registration_and_noop_candidates_keep_distinct_dispositions(self):
        source = '''{"GetText", lua_EditBox_GetText},
set("SetText", setter);
"function mt:Dynamic() return 1 end\\n"
"Placeholder=1,GetText=1,\\n"
// {"CommentOnly", lua_CommentOnly}
'''
        rows = {r["name"]: r for r in widget_inventory(source, {"GetText", "SetText", "Dynamic", "Placeholder", "Generated"}, {"Placeholder"})}
        self.assertEqual(rows["Placeholder"]["disposition"], "no-op candidate")
        self.assertEqual(rows["GetText"]["disposition"], "provided candidate")
        self.assertEqual(rows["GetText"]["routes"][0]["implementation_symbol"], "lua_EditBox_GetText")
        self.assertEqual(rows["Generated"]["routes"], [])
        self.assertTrue(all(r["execution_result"] == "not-run" for r in rows.values()))


class CtestInventoryTests(unittest.TestCase):
    def manifest(self, tests):
        return json.dumps({"kind": "ctestInfo", "version": {"major": 1, "minor": 0}, "tests": tests}).encode()

    def test_configuration_never_becomes_a_pass(self):
        raw = self.manifest([{"name": "z", "command": ["test.exe"], "status": "passed",
                              "properties": [{"name": "LABELS", "value": ["headless"]},
                                             {"name": "DISABLED", "value": True}]}, {"name": "a"}])
        result = ctest_inventory(raw)
        self.assertEqual(result["manifest_status"], "configured")
        self.assertEqual([r["name"] for r in result["tests"]], ["a", "z"])
        self.assertTrue(all(r["execution_result"] == "not-run" for r in result["tests"]))
        self.assertTrue(result["tests"][1]["disabled"])
        self.assertEqual(result, ctest_inventory(raw))
        self.assertNotEqual(result["sha256"], ctest_inventory(self.manifest([]))["sha256"])

    def test_missing_manifest_is_not_empty_test_success(self):
        self.assertEqual(ctest_inventory(None)["manifest_status"], "not supplied")
        self.assertEqual(ctest_inventory(None)["execution_result"], "not-run")

    def test_malformed_and_duplicate_manifests_fail(self):
        for raw in (b'{}', b'{"kind":"ctestInfo","version":[]}', self.manifest([{}]),
                    self.manifest([{"name": "a"}, {"name": "a"}]),
                    self.manifest([{"name": "a", "properties": [{"name": []}]}])):
            with self.subTest(raw=raw), self.assertRaises(ValueError):
                ctest_inventory(raw)


if __name__ == "__main__":
    unittest.main()
