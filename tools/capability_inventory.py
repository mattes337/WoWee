"""Static evidence extractors; none of these execute gameplay or test commands."""
import hashlib
import json
import re

from cvar_default_agreement import FALLBACK, NAME, DIRECT


_CPP_TOKEN = re.compile(r'"(?:\\.|[^"\\])*"|\'(?:\\.|[^\'\\])*\'|//[^\n]*|/\*.*?\*/', re.S)


def uncomment(source):
    """Blank ordinary C++ comments while preserving offsets and string literals."""
    return _CPP_TOKEN.sub(lambda m: re.sub(r"[^\n]", " ", m[0]) if m[0].startswith("/") else m[0], source)


def block(source, declaration):
    """Return an ordinary C++ declaration's braced contents and source offset."""
    match = re.search(declaration, source)
    if not match:
        return "", 0
    start = source.index("{", match.start()) + 1
    masked = _CPP_TOKEN.sub(lambda m: re.sub(r"[^\n]", " ", m[0]), source)
    depth = 1
    for index in range(start, len(masked)):
        depth += (masked[index] == "{") - (masked[index] == "}")
        if depth == 0:
            return source[start:index], start
    raise ValueError("Unterminated source block in capability inventory")


def evidence(source, path, offset):
    return f"{path}:{source.count(chr(10), 0, offset) + 1}"


def cvar_inventory(sources):
    """Capture literal CVar bindings, defaults and reads, preserving uncertainty."""
    bindings, defaults, reads, dispatch = [], [], [], []
    for path, raw in sorted(sources.items()):
        source = uncomment(raw)
        table, start = block(source, r"\bkClientCVars\s*\[\s*\]\s*=\s*\{")
        for match in re.finditer(r'\{\s*"([\w]+)"\s*,\s*"([\w]+)"\s*(?:,\s*([^}]+))?\}', table):
            bindings.append({"name": match[1], "setting": match[2],
                             "scale_expression": match[3].strip() if match[3] else "1.0",
                             "evidence": evidence(source, path, start + match.start()),
                             "result": "static binding; runtime behavior unverified"})
        body, start = block(source, r"\bpushCvarDefault\s*\([^)]*\)\s*\{")
        literal_matches = list(FALLBACK.finditer(body))
        for match in NAME.finditer(body):
            literal = next((m[2] for m in literal_matches if m.start() <= match.start() < m.end()), None)
            defaults.append({"name": match[1].lower(), "literal_default": literal,
                             "result": "literal default candidate" if literal is not None else "dynamic or unmatched default branch",
                             "evidence": evidence(source, path, start + match.start())})
        for match in re.finditer(r'n\.rfind\("([^"]+)",\s*0\)\s*==\s*0\)\s*\{\s*lua_pushstring\(L,\s*"([^"]*)"\)', body):
            dispatch.append({"match_kind": "prefix", "prefix": match[1], "literal_default": match[2],
                             "evidence": evidence(source, path, start + match.start())})
        tail = re.search(r'^\s*\{?\s*lua_pushstring\(L,\s*"([^"]*)"\);\s*(?:return;\s*)?\}?\s*\Z', body, re.M)
        if tail:
            dispatch.append({"match_kind": "unmatched-name fallback candidate", "literal_default": tail[1],
                             "evidence": evidence(source, path, start + tail.start())})
        for match in DIRECT.finditer(source):
            reads.append({"name": match[1], "literal_fallback": match[2],
                          "evidence": evidence(source, path, match.start())})
    return {"client_bindings": bindings, "named_default_candidates": defaults,
            "default_dispatch_rules": dispatch, "stored_value_reads": reads,
            "limitations": "Ordinary C++ literal/branch heuristics only. Expressions are not evaluated; dispatch precedence, settings defaults, side effects and runtime values require review."}


def widget_inventory(source, candidates, noops, path="src/addons/lua_engine.cpp"):
    """Attach registration evidence to the shared provider's candidate names."""
    source = uncomment(source)
    routes = {}
    patterns = {
        "C++ binding table": r'\{\s*(?:\.\w+\s*=\s*)?"(\w+)"\s*,\s*(?:\.\w+\s*=\s*)?(lua_\w+)',
        "region registration": r'\bset\("(\w+)"\s*,',
        "Lua method shim": r'function\s+\w+\s*:\s*(\w+)\s*\(',
        "no-op allowlist candidate": r'\b([A-Za-z]\w*)=1',
    }
    for route, pattern in patterns.items():
        for match in re.finditer(pattern, source):
            item = {"route": route, "evidence": evidence(source, path, match.start())}
            if route == "C++ binding table":
                item["implementation_symbol"] = match[2]
            routes.setdefault(match[1], []).append(item)
    return [{"name": name, "disposition": "no-op candidate" if name in noops else "provided candidate",
             "routes": routes.get(name, []), "execution_result": "not-run",
             "limitations": "Shared provider heuristics include dynamic/allowlist names and can overlap globals; registration does not establish method behavior."}
            for name in sorted(candidates | noops)]


def ctest_inventory(raw):
    """Read CTest --show-only=json-v1; a manifest contains no execution results."""
    if raw is None:
        return {"manifest_status": "not supplied", "tests": [], "execution_result": "not-run"}
    data = json.loads(raw)
    if (not isinstance(data, dict) or data.get("kind") != "ctestInfo"
            or not isinstance(data.get("version"), dict) or data["version"].get("major") != 1):
        raise ValueError("Expected CTest --show-only=json-v1 manifest")
    tests = data.get("tests")
    if not isinstance(tests, list):
        raise ValueError("CTest manifest tests must be a list")
    rows = []
    for test in tests:
        if not isinstance(test, dict) or not isinstance(test.get("name"), str):
            raise ValueError("CTest manifest test must have a name")
        properties = test.get("properties", [])
        if not isinstance(properties, list) or any(not isinstance(p, dict) or not isinstance(p.get("name"), str) for p in properties):
            raise ValueError("Invalid CTest properties")
        props = {p.get("name"): p.get("value") for p in properties}
        command = test.get("command", [])
        if not isinstance(command, list) or any(not isinstance(arg, str) for arg in command):
            raise ValueError("Invalid CTest command")
        rows.append({"name": test["name"], "command": command,
                     "labels": props.get("LABELS", []), "disabled": props.get("DISABLED", False),
                     "execution_result": "not-run"})
    if len({row["name"] for row in rows}) != len(rows):
        raise ValueError("Duplicate CTest test names")
    return {"manifest_status": "configured", "sha256": hashlib.sha256(raw).hexdigest(),
            "tests": sorted(rows, key=lambda row: row["name"]), "execution_result": "not-run",
            "limitations": "This inventory never infers pass from registration, labels, command presence or caller-supplied status fields. Attach a separately verified run report for pass/fail evidence."}
