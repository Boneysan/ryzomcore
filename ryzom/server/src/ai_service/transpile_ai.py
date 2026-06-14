#!/usr/bin/env python3
"""
Ryzom AIS → Lua transpiler for the EGS Lua runtime (Phase 6.1).

Source language: the Ryzom AI Service scripting language, as defined in
script_parser.lex / script_parser.yacc.  Key constructs:

  // and # line comments
  Variables:  myVar (float), $strVar (string), @ctxVar (context)
  Literals:   42, 3.14, "hello"
  Operators:  && || == != <> >< != >= => <= =< > < + - * / += -= *= /= ++ --
  Control:    if (cond) { } else { }
              while (cond) { }
              switch (expr) { case "v" : stmt ... }
              rand { stmt ... }          -- random branch selection
              onchildren() { }           -- iterate child entities
  Functions:  name() { }                -- define a callable block
              name();                   -- call a defined block
              (out) nativeFn(arg);      -- native call with output tuple
              () nativeFn(arg);         -- native call, discard output
              ctx.name();               -- call on a named context
  Print/Log:  print("msg", var, $s);   → egs.info(...)
              log("msg", var, $s);      → egs.info(...)

Target: Lua 5.2, suitable for egs_lua.cpp runtime.
Context / string variables use _ctx_NAME / _str_NAME conventions to avoid
the @ and $ sigils that Lua identifiers cannot contain.
"""

import sys
import re
from typing import List, Optional, Tuple

# ---------------------------------------------------------------------------
# Tokeniser
# ---------------------------------------------------------------------------

NAME = 'NAME'
STRNAME = 'STRNAME'   # $foo
CTXNAME = 'CTXNAME'   # @foo
NUMBER = 'NUMBER'
STRING = 'STRING'
OP = 'OP'
PUNCT = 'PUNCT'
KW = 'KW'
EOF = 'EOF'

KEYWORDS = frozenset({'if', 'else', 'while', 'switch', 'case',
                      'rand', 'onchildren', 'print', 'log'})


class Token:
    __slots__ = ('type', 'value', 'line')

    def __init__(self, type_: str, value: str, line: int) -> None:
        self.type = type_
        self.value = value
        self.line = line

    def __repr__(self) -> str:
        return f'Token({self.type}, {self.value!r}, line={self.line})'


def tokenize(src: str) -> List[Token]:
    tokens: List[Token] = []
    i = 0
    line = 1
    n = len(src)

    while i < n:
        c = src[i]

        # Whitespace
        if c in ' \t\r':
            i += 1
            continue
        if c == '\n':
            line += 1
            i += 1
            continue

        # Line comments: // or #
        if c == '/' and i + 1 < n and src[i + 1] == '/':
            while i < n and src[i] != '\n':
                i += 1
            continue
        if c == '#':
            while i < n and src[i] != '\n':
                i += 1
            continue

        # Block comment: /* ... */
        if c == '/' and i + 1 < n and src[i + 1] == '*':
            i += 2
            while i + 1 < n and not (src[i] == '*' and src[i + 1] == '/'):
                if src[i] == '\n':
                    line += 1
                i += 1
            i += 2
            continue

        # String literals
        if c == '"':
            j = i + 1
            while j < n and src[j] != '"':
                if src[j] == '\\':
                    j += 1
                j += 1
            tokens.append(Token(STRING, src[i:j + 1], line))
            i = j + 1
            continue

        # Context variable: @ident
        if c == '@':
            j = i + 1
            while j < n and (src[j].isalnum() or src[j] == '_'):
                j += 1
            tokens.append(Token(CTXNAME, src[i + 1:j], line))
            i = j
            continue

        # String variable: $ident
        if c == '$':
            j = i + 1
            while j < n and (src[j].isalnum() or src[j] == '_'):
                j += 1
            tokens.append(Token(STRNAME, src[i + 1:j], line))
            i = j
            continue

        # Identifiers and keywords
        if c.isalpha() or c == '_':
            j = i
            while j < n and (src[j].isalnum() or src[j] == '_'):
                j += 1
            word = src[i:j]
            tokens.append(Token(KW if word in KEYWORDS else NAME, word, line))
            i = j
            continue

        # Numbers (including leading sign only when unambiguous)
        if c.isdigit() or (c in '+-' and i + 1 < n and src[i + 1].isdigit()):
            j = i
            if src[j] in '+-':
                j += 1
            while j < n and src[j].isdigit():
                j += 1
            if j < n and src[j] == '.':
                j += 1
                while j < n and src[j].isdigit():
                    j += 1
            if j < n and src[j] in 'eE':
                j += 1
                if j < n and src[j] in '+-':
                    j += 1
                while j < n and src[j].isdigit():
                    j += 1
            tokens.append(Token(NUMBER, src[i:j], line))
            i = j
            continue

        # Two-character operators (check before single-char)
        two = src[i:i + 2]
        if two in ('&&', '||', '==', '!=', '>=', '=>', '<=', '=<',
                   '<>', '><', '+=', '-=', '*=', '/=', '++', '--'):
            tokens.append(Token(OP, two, line))
            i += 2
            continue

        # Single-character operators and punctuation
        if c in '=<>':
            tokens.append(Token(OP, c, line))
            i += 1
            continue
        if c in '+-':
            tokens.append(Token(OP, c, line))
            i += 1
            continue
        if c in '*/':
            tokens.append(Token(OP, c, line))
            i += 1
            continue
        if c in '.,;:(){}':
            tokens.append(Token(PUNCT, c, line))
            i += 1
            continue

        # Unknown — skip
        i += 1

    tokens.append(Token(EOF, '', line))
    return tokens


# ---------------------------------------------------------------------------
# Transpiler (recursive descent)
# ---------------------------------------------------------------------------

class TranspileError(Exception):
    pass


class Transpiler:
    def __init__(self, tokens: List[Token]) -> None:
        self.tokens = tokens
        self.pos = 0
        self.indent = 0
        self._sw_n = 0
        self._rand_n = 0
        self._declared: set = set()

    # --- Token access -------------------------------------------------------

    def peek(self, offset: int = 0) -> Token:
        p = self.pos + offset
        return self.tokens[p] if p < len(self.tokens) else Token(EOF, '', 0)

    def consume(self, expected: Optional[str] = None) -> Token:
        tok = self.tokens[self.pos]
        if expected is not None and tok.value != expected:
            raise TranspileError(
                f"line {tok.line}: expected {expected!r}, got {tok.value!r}")
        self.pos += 1
        return tok

    def at(self, *values: str) -> bool:
        return self.peek().value in values

    def at_type(self, *types: str) -> bool:
        return self.peek().type in types

    # --- Indentation --------------------------------------------------------

    def ind(self) -> str:
        return '  ' * self.indent

    # --- Top-level ----------------------------------------------------------

    def translate(self) -> str:
        parts = []
        while not self.at_type(EOF):
            s = self.parse_statement()
            if s:
                parts.append(s)
        return '\n'.join(parts)

    # --- Statements ---------------------------------------------------------

    def parse_statement(self) -> str:
        tok = self.peek()

        if tok.type == KW:
            kw = tok.value
            if kw == 'if':
                return self.parse_if()
            if kw == 'while':
                return self.parse_while()
            if kw == 'switch':
                return self.parse_switch()
            if kw == 'rand':
                return self.parse_rand()
            if kw == 'onchildren':
                return self.parse_onchildren()
            if kw in ('print', 'log'):
                return self.parse_print_log()

        if tok.value == '{':
            return self.parse_block_bare()

        if tok.value == '(':
            return self.parse_tuple_call()

        # Pre-increment / pre-decrement
        if tok.type == OP and tok.value in ('++', '--'):
            op = self.consume().value
            ctx = self.try_context()
            name = self.consume().value
            self.opt_semi()
            lhs = self.lua_num_var(ctx, name)
            delta = '1' if op == '++' else '-1'
            return f"{self.ind()}{lhs} = {lhs} + {delta}"

        return self.parse_simple()

    def parse_if(self) -> str:
        self.consume('if')
        self.consume('(')
        cond = self.balanced_expr(')')
        self.consume(')')
        ind = self.ind()
        body = self.indented_statement()
        if self.at('else'):
            self.consume('else')
            else_body = self.indented_statement()
            return (f"{ind}if {cond} then\n{body}\n"
                    f"{ind}else\n{else_body}\n{ind}end")
        return f"{ind}if {cond} then\n{body}\n{ind}end"

    def parse_while(self) -> str:
        self.consume('while')
        self.consume('(')
        cond = self.balanced_expr(')')
        self.consume(')')
        ind = self.ind()
        body = self.indented_statement()
        return f"{ind}while {cond} do\n{body}\n{ind}end"

    def parse_switch(self) -> str:
        self.consume('switch')
        self.consume('(')
        expr = self.balanced_expr(')')
        self.consume(')')
        self.consume('{')

        self._sw_n += 1
        sv = f"_sw{self._sw_n}"
        ind = self.ind()

        cases: List[Tuple[str, str]] = []
        while not self.at('}') and not self.at_type(EOF):
            if not self.at('case'):
                # stray tokens — skip one
                self.pos += 1
                continue
            self.consume('case')
            key = self.parse_case_key()
            self.consume(':')
            self.indent += 1
            body = self.parse_statement()
            self.indent -= 1
            if body:
                cases.append((key, body))
        self.consume('}')

        if not cases:
            return f"{ind}-- switch({expr}): no cases"

        lines = [f"{ind}local {sv} = {expr}"]
        for i, (key, body) in enumerate(cases):
            kw2 = 'if' if i == 0 else 'elseif'
            lines.append(f"{ind}{kw2} {sv} == {key} then")
            lines.append(body)
        lines.append(f"{ind}end")
        return '\n'.join(lines)

    def parse_rand(self) -> str:
        self.consume('rand')
        self.consume('{')

        self._rand_n += 1
        rv = f"_rand{self._rand_n}"
        ind = self.ind()

        branches: List[str] = []
        while not self.at('}') and not self.at_type(EOF):
            self.indent += 1
            b = self.parse_statement()
            self.indent -= 1
            if b:
                branches.append(b)
        self.consume('}')

        if not branches:
            return f"{ind}-- rand: no branches"
        if len(branches) == 1:
            return branches[0]

        lines = [f"{ind}local {rv} = math.random({len(branches)})"]
        for i, branch in enumerate(branches):
            kw2 = 'if' if i == 0 else 'elseif'
            lines.append(f"{ind}{kw2} {rv} == {i + 1} then")
            lines.append(branch)
        lines.append(f"{ind}end")
        return '\n'.join(lines)

    def parse_onchildren(self) -> str:
        self.consume('onchildren')
        self.consume('(')
        self.consume(')')
        ind = self.ind()
        body = self.indented_statement()
        return f"{ind}for _, _child in ipairs(children()) do\n{body}\n{ind}end"

    def parse_print_log(self) -> str:
        self.consume()  # 'print' or 'log'
        self.consume('(')
        args: List[str] = []
        while not self.at(')') and not self.at_type(EOF):
            args.append(self.translate_atom())
            if self.at(','):
                self.consume(',')
        self.consume(')')
        self.opt_semi()
        joined = ' .. " " .. '.join(
            a if a.startswith('"') else f'tostring({a})' for a in args)
        return f"{self.ind()}egs.info({joined})"

    def parse_tuple_call(self) -> str:
        """(out, ...) nativeFn(args);  or  () nativeFn(args);"""
        self.consume('(')
        outs: List[str] = []
        while not self.at(')') and not self.at_type(EOF):
            outs.append(self.lua_lval_expr())
            if self.at(','):
                self.consume(',')
        self.consume(')')

        ctx = self.try_context()
        fname = self.consume().value
        self.consume('(')
        args = self.arg_list()
        self.consume(')')
        self.opt_semi()

        call = self.make_call(ctx, fname, args)
        ind = self.ind()
        if not outs:
            return f"{ind}{call}"
        lhs = ', '.join(outs)
        # Declare as locals if not yet declared
        new = [v for v in outs if v not in self._declared]
        for v in new:
            self._declared.add(v)
        prefix = 'local ' if new else ''
        return f"{ind}{prefix}{lhs} = {call}"

    def parse_simple(self) -> str:
        """Assignments, calls, function definitions."""
        tok = self.peek()
        ind = self.ind()

        # Function definition: NAME() { ... }
        if (tok.type == NAME
                and self.peek(1).value == '('
                and self.peek(2).value == ')'
                and self.peek(3).value == '{'):
            fname = self.consume().value
            self.consume('(')
            self.consume(')')
            self.indent += 1
            body = self.parse_block_bare()
            self.indent -= 1
            return f"{ind}local function {fname}()\n{body}\n{ind}end"

        # Optional context prefix: NAME. or @NAME.
        ctx = self.try_context()
        tok = self.peek()

        if tok.type == NAME:
            name = self.consume().value

            # Call: name()
            if self.at('('):
                self.consume('(')
                args = self.arg_list()
                self.consume(')')
                self.opt_semi()
                return f"{ind}{self.make_call(ctx, name, args)}"

            # Post-increment / Post-decrement: name++;
            if self.at('++', '--'):
                op = self.consume().value
                self.opt_semi()
                lhs = self.lua_num_var(ctx, name)
                delta = '1' if op == '++' else '-1'
                return f"{ind}{lhs} = {lhs} + {delta}"

            # Compound assignment: name += expr;
            if self.peek().type == OP and self.peek().value in ('+=', '-=', '*=', '/='):
                op_ch = self.consume().value[0]
                rhs = self.balanced_expr(';')
                self.opt_semi()
                lhs = self.lua_num_var(ctx, name)
                return f"{ind}{lhs} = {lhs} {op_ch} ({rhs})"

            # Assignment: name = expr;
            if self.at('=') and self.peek(1).value != '=':
                self.consume('=')
                rhs = self.balanced_expr(';')
                self.opt_semi()
                lhs = self.lua_num_var(ctx, name)
                decl = '' if lhs in self._declared else 'local '
                self._declared.add(lhs)
                return f"{ind}{decl}{lhs} = {rhs}"

        elif tok.type == STRNAME:
            name = self.consume().value
            if self.at('=') and self.peek(1).value != '=':
                self.consume('=')
                rhs = self.balanced_expr(';')
                self.opt_semi()
                lhs = self.lua_str_var(ctx, name)
                decl = '' if lhs in self._declared else 'local '
                self._declared.add(lhs)
                return f"{ind}{decl}{lhs} = {rhs}"

        elif tok.type == CTXNAME:
            name = self.consume().value
            if self.at('=') and self.peek(1).value != '=':
                self.consume('=')
                rhs = self.balanced_expr(';')
                self.opt_semi()
                lhs = self.lua_ctx_name(ctx, name)
                decl = '' if lhs in self._declared else 'local '
                self._declared.add(lhs)
                return f"{ind}{decl}{lhs} = {rhs}"

        # Re-sync: skip to end of statement
        while not self.at(';', '{', '}') and not self.at_type(EOF):
            self.pos += 1
        self.opt_semi()
        return ''

    # --- Helpers ------------------------------------------------------------

    def parse_block_bare(self) -> str:
        """Parse { statements } and return the block contents (indented)."""
        self.consume('{')
        parts: List[str] = []
        while not self.at('}') and not self.at_type(EOF):
            s = self.parse_statement()
            if s:
                parts.append(s)
        self.consume('}')
        return '\n'.join(parts)

    def indented_statement(self) -> str:
        """Parse the next statement with one extra indent level."""
        self.indent += 1
        if self.at('{'):
            s = self.parse_block_bare()
        else:
            s = self.parse_statement()
        self.indent -= 1
        return s

    def try_context(self) -> Optional[str]:
        """Consume NAME. or @NAME. as a context prefix, return the name."""
        if self.peek().type in (NAME, CTXNAME) and self.peek(1).value == '.':
            name = self.consume().value
            self.consume('.')
            return name
        return None

    def balanced_expr(self, stop: str) -> str:
        """Collect tokens into a translated expression until *stop* at depth 0."""
        parts: List[str] = []
        depth = 0
        while not self.at_type(EOF):
            tok = self.peek()
            if tok.value == stop and depth == 0:
                break
            if tok.value in ('(', '{'):
                depth += 1
            elif tok.value in (')', '}'):
                if depth == 0:
                    break
                depth -= 1
            parts.append(self.translate_tok(self.consume()))
        return ' '.join(parts)

    def arg_list(self) -> str:
        """Parse comma-separated expressions until ) without consuming it."""
        args: List[str] = []
        depth = 0
        cur: List[str] = []
        while not self.at_type(EOF):
            tok = self.peek()
            if tok.value == ')' and depth == 0:
                break
            if tok.value in ('(', '{'):
                depth += 1
            elif tok.value in (')', '}'):
                depth -= 1
            if tok.value == ',' and depth == 0:
                self.consume(',')
                args.append(' '.join(cur))
                cur = []
                continue
            cur.append(self.translate_tok(self.consume()))
        if cur:
            args.append(' '.join(cur))
        return ', '.join(args)

    def parse_case_key(self) -> str:
        tok = self.peek()
        if tok.type in (STRING, NUMBER):
            return self.consume().value
        if tok.type == NAME:
            return f'"{self.consume().value}"'
        return self.consume().value

    def translate_atom(self) -> str:
        tok = self.peek()
        if tok.type == STRING:
            return self.consume().value
        if tok.type == NUMBER:
            return self.consume().value
        if tok.type == NAME:
            return self.consume().value
        if tok.type == STRNAME:
            return f"_str_{self.consume().value}"
        if tok.type == CTXNAME:
            return f"_ctx_{self.consume().value}"
        if tok.value == '(':
            self.consume('(')
            inner = self.balanced_expr(')')
            self.consume(')')
            return f"({inner})"
        return self.consume().value

    def lua_lval_expr(self) -> str:
        tok = self.peek()
        if tok.type == NAME:
            return self.consume().value
        if tok.type == STRNAME:
            return f"_str_{self.consume().value}"
        if tok.type == CTXNAME:
            return f"_ctx_{self.consume().value}"
        return self.consume().value

    def lua_num_var(self, ctx: Optional[str], name: str) -> str:
        return f"{ctx}.{name}" if ctx else name

    def lua_str_var(self, ctx: Optional[str], name: str) -> str:
        return f"{ctx}._str_{name}" if ctx else f"_str_{name}"

    def lua_ctx_name(self, ctx: Optional[str], name: str) -> str:
        return f"{ctx}._ctx_{name}" if ctx else f"_ctx_{name}"

    def make_call(self, ctx: Optional[str], name: str, args: str) -> str:
        if ctx:
            return f"{ctx}:{name}({args})"
        return f"{name}({args})"

    def opt_semi(self) -> None:
        if self.at(';'):
            self.consume(';')

    def translate_tok(self, tok: Token) -> str:
        """Convert a single token to its Lua equivalent."""
        if tok.type == STRNAME:
            return f"_str_{tok.value}"
        if tok.type == CTXNAME:
            return f"_ctx_{tok.value}"
        table = {
            '&&': 'and',
            '||': 'or',
            '!=': '~=',
            '<>': '~=',
            '><': '~=',
            '=>': '>=',
            '=<': '<=',
        }
        return table.get(tok.value, tok.value)


# ---------------------------------------------------------------------------
# Public API
# ---------------------------------------------------------------------------

def transpile(source: str) -> str:
    """Transpile AIS source code to Lua. Returns the Lua string."""
    return Transpiler(tokenize(source)).translate()


if __name__ == '__main__':
    if len(sys.argv) < 2:
        print("Usage: transpile_ai.py <input.ais>", file=sys.stderr)
        sys.exit(1)
    with open(sys.argv[1]) as fh:
        print(transpile(fh.read()))
