# Zepto Syntax Definitions

This directory contains **syntax highlighting files** used by the Zepto text editor.

Each file is named according to the programming language extension it supports, e.g.:

- `c.config`
- `py.config`
- `sh.config`

These files define which **keywords** should be highlighted and what **color** to use.

---

## 📄 File Format

Each line of the config file defines a **keyword** and an **ANSI color code**, separated by whitespace.

### Example (`python.config`):
```

def 34
return 32
import 36
class 35

```

### Meaning:
- `def` will be shown in **blue** (`34`)
- `return` in **green** (`32`)
- `import` in **cyan** (`36`)
- `class` in **magenta** (`35`)

**Note**: Lines starting with `#` or empty lines are ignored.

---

## 🎨 ANSI Color Codes

You can use standard 8-color ANSI codes like:

| Color        | Code |
|--------------|------|
| Black        | 30   |
| Red          | 31   |
| Green        | 32   |
| Yellow       | 33   |
| Blue         | 34   |
| Magenta      | 35   |
| Cyan         | 36   |
| White        | 37   |

Or extended formats like `38;5;COLOR` (e.g. `38;5;208` for orange).

---

## ➕ Adding a New Language

To define a new language:

1. Create a file named `yourlang.config` in this folder.
2. Add lines of the form:

```

keyword COLORCODE

```

3. When you open a file in Zepto with the extension `.yourlang`, it will automatically load `yourlang.config`.

### Example: `json.config`
```

true 32
false 31
null 35

```

---

## 📁 Installation Path

Zepto looks for syntax files in:

```

\~/.config/zepto/language/

```

You can create or edit files directly there.

---

## 💡 Tips

- Keep keyword entries lowercase, matching what appears in source files.
- You can reuse config files across similar languages (e.g. `c.config` for `.h` files).
- Only **whole word matches followed by space or newline** are highlighted.

---

## 🛠️ Custom Colors

For finer control, use ANSI escape sequences like:

```

for 38;5;208     # orange
let 1;36         # bold cyan

```

These are wrapped automatically as `\033[<code>m` by Zepto.

---

Happy hacking!
