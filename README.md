# 🧬 Zepto Editor

Zepto is a **quantum-sized**, dependency-free text editor for the terminal.  
It fits in **a few kilobytes**, yet supports modern editing features like:

- **Undo/Redo**
- **Standard Copy/Paste**
- **Text Selection**
- **Syntax Highlighting**
- **Line Numbers**
- **Raw terminal mode for ultra-light input handling**

MIT licensed, simple, fast, and perfect for minimal environments or system rescue shells.

---

## 🧠 Philosophy

Zepto was designed to be as small and portable as possible while still providing features you'd expect from a modern terminal-based editor.  
No external libraries, no ncurses, no third-party build systems — just ANSI escape codes and raw TTY input.

**see the [wiki](https://github.com/vroby65/zepto/wiki) for details**

---

## 🔧 Features

- **No dependencies**: uses raw ANSI escape codes and `termios`
- **Raw terminal mode**: captures keypresses instantly, including combinations like Ctrl/Alt/F-keys
- **Undo/Redo**: based on compact history stacks
- **Clipboard**: supports `Ctrl+C` / `Ctrl+X` / `Ctrl+V`
- **Syntax highlighting**: colorizes keywords using user-defined config files
- **Compact**: built binary under 30 KB
- **Mouse support**: SGR mode supported. mouse wheel to scroll source and click to locate cursor
- **Portable**: works on any POSIX terminal

---

## 📦 Files

- `zt.c` – main source file
- `build` – small shell script to compile the editor
- `install` – shell script to install the editor and syntax config
- `languages/` – directory with syntax definitions (e.g., `c.config`, `python.config`)

---

## 🧾 Syntax Highlighting

Syntax highlighting is driven by config files placed in:
```

~/.config/zepto/languages/

```
Each file is named `<language>.config` (e.g. `c.config`) and contains lines like:
```

if 31  
else 31  
while 34  
return 32

```
Where the first part is the keyword and the second is the ANSI color code (`31 = red`, `34 = blue`, `32 = green`, etc.).  
Colors are inserted at render-time using standard escape codes like `\033[31m` for red, and `\033[0m` to reset.

---

## 🎨 Terminal Graphics

Zepto draws everything using **raw ANSI escape codes**:

- `\033[H` — move cursor to top-left
- `\033[<row>;<col>H` — move cursor to a specific position
- `\033[K` — clear to end of line
- `\033[7m` / `\033[0m` — invert video (used for selections)
- `\033[?25l` / `\033[?25h` — hide/show cursor
- `\033[41m` / `\033[107m` — set background color (red, white, etc.)

This allows Zepto to run **without ncurses**, making it ideal for static builds, embedded systems, or recovery environments.

---

## ⚙️ Compilation

To compile Zepto and produce the `zt` binary, run:

```bash
./build
```

This will generate the `zt` executable.

---

## 🧱 Installation

To install Zepto system-wide and set up syntax configs:

```bash
./install
```

This copies:

- `zt` into `/usr/local/bin`

- `languages/` into `~/.config/zt/languages/`

---

## 🖱️ Usage

```bash
zt filename.c
```

Controls:

- `Esc` — exit without saving

- `F2` or `Ctrl+2` — save

- `F10` — save and exit

- `F7` or `Ctrl+7` — search

- `Ctrl+Z` / `Ctrl+Y` — undo / redo

- `Ctrl+C` / `Ctrl+X` / `Ctrl+V` — copy / cut / paste

- Shift + Arrows — text selection

---

## 🪪 License

MIT License — see `LICENSE` file for details.

---

## 💡 Tip

Ideal for rescue shells, retro systems, and minimal distros.  
Zepto proves you don’t need megabytes of code to have modern editing pow

