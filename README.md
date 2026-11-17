# Nerx (Nerxelity)

A simple programming language with easy-to-read keywords. A single `.nrx`
file can run directly, or be turned into a standalone `.exe`.

```
be name = ask "What is your name? "
print "Hello, " name "!"

func square(n int) int
    return n * n

for i in range(5)
    print i " x " i " = " square(i)
```

The compiler, `Nerx`, is written in nerx itself. It produces native Windows
executables directly: no virtual machine, no extra toolchain, nothing else to install.

## Install

Run the installer `nerx-setup.exe`. It installs `Nerx` and, if you tick the
option, adds it to your PATH so you can use it from any folder.

## Usage

```
nerx run program.nrx                     run it (compiled to a temporary exe, then deleted)
nerx build program.nrx                   build program.exe
nerx build program.nrx -o game.exe
Nerx program.nrx                         same as build
```

- The resulting `.exe` is standalone: the `.nrx` file does not need to be shipped with it.
- Type errors are caught before the program runs.
- Number-crunching loops run within a few percent of an optimizing native compiler.
  Recursive functions are still slower (about 4x on a naive Fibonacci).
  Numbers and sources are in the Speed section of the website.

## The language

- Typed variables (`int`, `double`, `float`, `bool`, `string`, `list`, `map`)
  and `lock` for values that must not change.
- `if`, `loop`, `for ... in`, `options`, `try` / `catch`, functions, and `type`
  (structs with methods).
- Other files with `im`, libraries with `lib` (windows, 3D games, internet,
  keyboard, sound, files, json, random numbers, console).
- `ffi` to call functions in Windows DLLs directly, including structs
  (buffers) and callbacks.
- Error messages in Indonesian or English (`errlang = id` / `en`).

The full list is in [SYNTAX.en.txt](SYNTAX.en.txt) (Indonesian: [SYNTAX.txt](SYNTAX.txt)),
and a complete example is in [contoh_fitur.nrx](contoh/contoh_fitur.nrx).

## Build from source

`Nerx` builds itself from the seed compiler `src\nrxc.exe`:

```
cd src
nrxc.exe nrxc.nrx nerx_new.exe
```

The full test suite (output comparison against the previous implementation in
`history\`, plus a check that the compiler reproduces itself byte for byte):
`powershell -NoProfile -File tes\semua.ps1`. The installer is built with
`installer\buat.bat` (requires Inno Setup 6). Earlier versions of the project
are kept in [history\](history/README.md).

## Status and known limitations

Nerx is an early version. Expect bugs, and please report them.

- Windows 10/11, 64-bit only. No Linux or macOS yet.
- Recursive function calls are slower than loops (about 4x slower than an
  optimizing native compiler on a naive Fibonacci).
- Whole numbers beyond about 2^62 become floating point numbers instead of
  wrapping around.
- `min` and `max` with a mix of whole and decimal numbers can return a
  different type than you might expect.
- The 3D library has no partial transparency (alpha) yet.
- No debugger and no package manager yet.
- The installer is not code-signed, so Windows may show an "unknown
  publisher" warning. Some antivirus programs may also flag executables
  made by the compiler by mistake (a false positive).

## How it is made

Designed and directed by Nerxelity; implemented with AI assistance (Claude).
Every change is checked by the test suite in `tes/` (run `tes\semua.ps1`),
which compares the output of 84 programs and 105 error cases, and checks that
the compiler reproduces itself byte for byte.

## License

MIT, see [LICENSE](LICENSE). The earlier implementation in `history\` uses
[raylib](https://github.com/raysan5/raylib) (zlib license, see `history\lib\raylib`).
