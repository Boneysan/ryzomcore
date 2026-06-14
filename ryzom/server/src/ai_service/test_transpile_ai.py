"""Tests for transpile_ai.py — AIS → Lua transpiler."""

import sys
import os
sys.path.insert(0, os.path.dirname(__file__))

from transpile_ai import transpile, tokenize, NAME, STRNAME, CTXNAME, NUMBER, STRING, KW, OP


# ---------------------------------------------------------------------------
# Tokenizer tests
# ---------------------------------------------------------------------------

def test_tokenize_line_comments():
    toks = tokenize("// line comment\na;")
    vals = [t.value for t in toks if t.value not in ('', ';')]
    assert vals == ['a'], vals

def test_tokenize_hash_comments():
    toks = tokenize("# hash comment\nb;")
    vals = [t.value for t in toks if t.value not in ('', ';')]
    assert vals == ['b'], vals

def test_tokenize_block_comment():
    toks = tokenize("/* block\n comment */\nc;")
    vals = [t.value for t in toks if t.value not in ('', ';')]
    assert vals == ['c'], vals

def test_tokenize_sigil_names():
    toks = tokenize("@ctx $str")
    types = [(t.type, t.value) for t in toks if t.value != '']
    assert (CTXNAME, 'ctx') in types
    assert (STRNAME, 'str') in types

def test_tokenize_two_char_ops():
    src = "&& || == != >= =>"
    ops = [t.value for t in tokenize(src) if t.type == OP]
    assert '&&' in ops
    assert '||' in ops
    assert '>=' in ops

def test_tokenize_string_literal():
    toks = tokenize('"hello world"')
    strings = [t for t in toks if t.type == STRING]
    assert strings[0].value == '"hello world"'

def test_tokenize_number():
    toks = tokenize("3.14 42")
    nums = [t.value for t in toks if t.type == NUMBER]
    assert '3.14' in nums
    assert '42' in nums

def test_tokenize_keywords():
    for kw in ('if', 'else', 'while', 'switch', 'case', 'rand', 'onchildren', 'print', 'log'):
        toks = tokenize(kw)
        assert toks[0].type == KW, f"{kw} not tokenized as keyword"


# ---------------------------------------------------------------------------
# Control flow tests
# ---------------------------------------------------------------------------

def test_if_simple():
    lua = transpile("if (x == 1) { doThing(); }")
    assert 'if x == 1 then' in lua
    assert 'doThing()' in lua
    assert 'end' in lua
    assert 'then' in lua

def test_if_else():
    lua = transpile("if (a > b) { yes(); } else { no(); }")
    assert 'then' in lua
    assert 'else' in lua
    assert 'yes()' in lua
    assert 'no()' in lua
    assert 'end' in lua

def test_nested_if():
    src = "if (a > 0) { if (b > 0) { ok(); } }"
    lua = transpile(src)
    assert lua.count('if') == 2
    assert lua.count('end') == 2

def test_while():
    lua = transpile("while (count > 0) { step(); }")
    assert 'while count > 0 do' in lua
    assert 'step()' in lua
    assert 'end' in lua

def test_nested_paren_condition():
    # The old transpiler broke on nested parens — verify we handle them
    src = "if (x > 0 && (y < 10 || z == 5)) { ok(); }"
    lua = transpile(src)
    assert 'if' in lua
    assert 'and' in lua
    assert 'or' in lua
    # Should not end the condition prematurely
    assert 'then' in lua

def test_logical_operators():
    lua = transpile("if (a && b || c) { f(); }")
    assert 'and' in lua
    assert 'or' in lua
    assert '&&' not in lua
    assert '||' not in lua

def test_neq_operator():
    lua = transpile("if (x != 0) { f(); }")
    assert '~=' in lua
    assert '!=' not in lua

def test_neq_alt_forms():
    for op in ('<>', '><'):
        lua = transpile(f"if (x {op} 0) {{ f(); }}")
        assert '~=' in lua, f"op {op!r} not translated"


# ---------------------------------------------------------------------------
# Switch / case
# ---------------------------------------------------------------------------

def test_switch_case():
    src = 'switch (mode) { case "patrol" : go(); case "fight" : attack(); }'
    lua = transpile(src)
    assert 'local _sw1' in lua
    assert '_sw1 == "patrol"' in lua
    assert 'elseif' in lua
    assert '_sw1 == "fight"' in lua
    assert 'end' in lua

def test_switch_number_key():
    src = 'switch (state) { case 1 : one(); case 2 : two(); }'
    lua = transpile(src)
    assert '== 1' in lua
    assert '== 2' in lua


# ---------------------------------------------------------------------------
# Rand block
# ---------------------------------------------------------------------------

def test_rand_multi_branch():
    src = "rand { branchA(); branchB(); branchC(); }"
    lua = transpile(src)
    assert 'math.random(3)' in lua
    assert 'branchA()' in lua
    assert 'branchB()' in lua
    assert 'branchC()' in lua
    assert 'elseif' in lua

def test_rand_single_branch():
    src = "rand { only(); }"
    lua = transpile(src)
    # Single branch — no math.random overhead
    assert 'only()' in lua
    assert 'math.random' not in lua


# ---------------------------------------------------------------------------
# onchildren
# ---------------------------------------------------------------------------

def test_onchildren():
    lua = transpile("onchildren() { doIt(); }")
    assert 'for _, _child in ipairs(children()) do' in lua
    assert 'doIt()' in lua
    assert 'end' in lua


# ---------------------------------------------------------------------------
# Assignments and variables
# ---------------------------------------------------------------------------

def test_assignment():
    lua = transpile("x = 42;")
    assert 'x = 42' in lua

def test_string_var_assignment():
    lua = transpile('$name = "Tryker";')
    assert '_str_name = "Tryker"' in lua

def test_ctx_var_assignment():
    lua = transpile("@phase = 2;")
    assert '_ctx_phase = 2' in lua

def test_compound_assign_add():
    lua = transpile("counter += 1;")
    assert 'counter = counter + ( 1 )' in lua or 'counter = counter + (1)' in lua

def test_compound_assign_mul():
    lua = transpile("damage *= 2;")
    assert 'damage = damage * ( 2 )' in lua or 'damage = damage * (2)' in lua

def test_post_increment():
    lua = transpile("counter++;")
    assert 'counter = counter + 1' in lua

def test_post_decrement():
    lua = transpile("counter--;")
    assert 'counter = counter + -1' in lua or 'counter = counter - 1' in lua

def test_pre_increment():
    lua = transpile("++counter;")
    assert 'counter' in lua
    assert '+ 1' in lua or '+1' in lua


# ---------------------------------------------------------------------------
# Function definitions and calls
# ---------------------------------------------------------------------------

def test_function_definition():
    src = "patrol() { move(); attack(); }"
    lua = transpile(src)
    assert 'local function patrol()' in lua
    assert 'move()' in lua
    assert 'attack()' in lua
    assert 'end' in lua

def test_call_no_args():
    lua = transpile("doSomething();")
    assert 'doSomething()' in lua

def test_call_with_args():
    lua = transpile("setMode(mode);")
    assert 'setMode(mode)' in lua

def test_context_call():
    lua = transpile("group.doThing();")
    assert 'group:doThing()' in lua

def test_tuple_call_with_output():
    lua = transpile("(result) getGroup();")
    assert 'result' in lua
    assert 'getGroup()' in lua
    assert '=' in lua

def test_tuple_call_discard():
    lua = transpile("() setActivity(\"normal\");")
    assert 'setActivity("normal")' in lua

def test_tuple_multi_output():
    lua = transpile("(x, y) getPosition();")
    assert 'x' in lua
    assert 'y' in lua
    assert 'getPosition()' in lua


# ---------------------------------------------------------------------------
# Print / log
# ---------------------------------------------------------------------------

def test_print():
    lua = transpile('print("hello");')
    assert 'egs.info' in lua
    assert '"hello"' in lua

def test_log():
    lua = transpile('log("state", x);')
    assert 'egs.info' in lua

def test_print_with_variable():
    lua = transpile('print("val", count);')
    assert 'count' in lua
    assert 'egs.info' in lua


# ---------------------------------------------------------------------------
# Indentation
# ---------------------------------------------------------------------------

def test_indentation_in_if():
    lua = transpile("if (x) { f(); }")
    lines = lua.split('\n')
    body_line = [l for l in lines if 'f()' in l][0]
    assert body_line.startswith('  '), f"body not indented: {body_line!r}"

def test_nested_indentation():
    lua = transpile("if (a) { if (b) { f(); } }")
    lines = lua.split('\n')
    body_line = [l for l in lines if 'f()' in l][0]
    assert body_line.startswith('    '), f"nested body not double-indented: {body_line!r}"


# ---------------------------------------------------------------------------
# Full snippet test
# ---------------------------------------------------------------------------

def test_full_snippet():
    src = """
// NPC patrol behavior
patrol() {
    if (atDest && @phase == 1) {
        () setMode("normal");
        @phase = 2;
    } else {
        () moveToPos(destX, destY, 0);
    }
}

switch (alertLevel) {
    case "low" : patrol();
    case "high" : () setMode("combat");
}
"""
    lua = transpile(src)
    # Key checks
    assert 'local function patrol' in lua
    assert 'setMode' in lua
    assert 'moveToPos' in lua
    assert '_ctx_phase' in lua
    assert 'local _sw1' in lua
    assert '"low"' in lua
    assert '"high"' in lua
    assert '&&' not in lua   # must be translated
    assert '||' not in lua   # must be translated


if __name__ == '__main__':
    import traceback
    passed = 0
    failed = 0
    for name, func in list(globals().items()):
        if name.startswith('test_') and callable(func):
            try:
                func()
                print(f"  ok  {name}")
                passed += 1
            except Exception as e:
                print(f"FAIL  {name}")
                traceback.print_exc()
                failed += 1
    print(f"\n{passed} passed, {failed} failed")
    sys.exit(1 if failed else 0)
