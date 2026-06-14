import sys
import re

def transpile(source_code):
    # This is a transpiler for the Ryzom AI custom scripting language -> Lua
    lua_code = []
    
    # Simple lexical replacements
    for line in source_code.split('\n'):
        # Method calls: C++ Lua bindings use . not : so we leave obj.method(arg) alone
        # No replacement needed here for Ryzom C++ bindings.
        
        # Sigil: @context -> context.
        line = line.replace('@', 'context.')
        
        # Sigil: $stringVar -> stringVar
        line = re.sub(r'\$([a-zA-Z0-9_]+)', r'\1', line)
        
        # if (cond) -> if cond then
        line = re.sub(r'if\s*\((.*?)\)', r'if \1 then', line)
        
        # while (cond) -> while cond do
        line = re.sub(r'while\s*\((.*?)\)', r'while \1 do', line)
        
        # { -> empty (handled by scoping) or nothing in simple cases
        # } -> end
        if line.strip() == '{':
            continue
        if line.strip() == '}':
            line = line.replace('}', 'end')
            
        # switch (cond) -> -- switch (cond) (placeholder since Lua lacks switch)
        line = re.sub(r'switch\s*\((.*?)\)', r'-- switch \1 (needs manual translation)', line)
        
        # case X: -> if cond == X then
        line = re.sub(r'case\s+(.*?):', r'elseif cond == \1 then', line)
        
        lua_code.append(line)
        
    return '\n'.join(lua_code)

if __name__ == '__main__':
    if len(sys.argv) < 2:
        print("Usage: transpile_ai.py <input.ais>")
        sys.exit(1)
        
    with open(sys.argv[1], 'r') as f:
        source = f.read()
        
    print(transpile(source))
