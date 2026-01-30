# Copilot Instructions for GDB (Intel Fork)

## Project Overview
This is the Intel fork of GDB (GNU Debugger) with extensions for Intel GPU debugging support, including SIMD, SYCL, and Intel Xe architecture features.

## Key Directories
- `gdb/` - Main GDB source code
- `gdbserver/` - GDB server implementation
- `gdbsupport/` - Shared support library (common code between gdb and gdbserver)
- `gdb/testsuite/` - Test suite (TCL/Expect with DejaGnu)
- `gdb/python/` - Python integration and GDB Python modules
- `gdb/contrib/ari/` - ARI (Automated Regression Identification) checker

## Coding Standards

### C/C++ Style
GDB follows the [GNU Coding Standards](http://www.gnu.org/prep/standards/standards.html) with strict interpretation. GDB requires C++17 (ISO/IEC 14882:2017).

#### Indentation & Formatting
- 2-space indentation with tabs where 8 spaces = 1 tab
- Braces on new lines for function definitions
- Function definition names must start at column zero
- Function declarations should NOT have name at column zero
- 80 column limit for source files
- No trailing whitespace

#### Whitespace Rules
- Space between function/macro name and opening parenthesis
- No space after open paren/bracket or before close paren/bracket
- Space after keywords (`if`, `for`, `while`, `switch`)
- Space around operators
- Space after casts: `(foo) x` not `(foo)x`
- No space for unary operators: `!x`, `~x`, `-x`, `*x`
- Pointers/references: `void *foo;` and `int &bar;` (no space before `*` or `&`)

#### Braces
- Any two or more lines (including comments) must be wrapped in braces:
```cpp
if (i)
  {
    /* Return success.  */
    return 0;
  }
```

#### Empty Loop Bodies
- Semicolon on next line:
```cpp
while (p++ != NULL)
  ;
```

#### Lambda Indentation
- Lambda body indented as if it were a for loop's body:
```cpp
for_each_thread ([] (thread_info *thread)
  {
    /* Lambda body.  */
  });
```

#### Gettext Macro
- No space before `_()` macro (exception to normal rule):
```cpp
error (_("This is an error message."));
```

#### Pointer Comparisons
- Always use explicit nullptr comparison:
```cpp
if (ptr != nullptr)  /* correct */
if (ptr)             /* incorrect */
```

- Use proper copyright headers: `Copyright (C) YEAR Free Software Foundation, Inc.`

### Forbidden Functions (ARI Rules)
GDB has strict coding rules enforced by the ARI checker (`gdb/contrib/ari/gdb_ari.sh`):

| Do NOT use | Use instead |
|------------|-------------|
| `assert()` | `gdb_assert()` or `internal_error()` |
| `abort()` | `internal_error()` |
| `sprintf()` | `xsnprintf()` or `xstrprintf()` |
| `vsprintf()` | `xstrvprintf()` |
| `asprintf()` | `xstrprintf()` |
| `strdup()` | `xstrdup()` |
| `basename()` | `lbasename()` |
| `bzero()` | `memset()` |
| `printf("%p")` | `paddress()` or `host_address_to_string()` |
| `printf("%ll")` | `phex()` |

### Forbidden Includes
| Do NOT include | Use instead |
|----------------|-------------|
| `<assert.h>` | `"gdb_assert.h"` |
| `<wait.h>` or `<sys/wait.h>` | `"gdb_wait.h"` |
| `<regex.h>` | `"gdb_regex.h"` |
| `<vfork.h>` | `"gdb_vfork.h"` |
| `"defs.h"` | (included via compiler `-include` flag) |
| `"config.h"` | (included via `defs.h`) |

### Documentation Requirements
- Document every function (static functions at implementation, external functions in header)
- Document every global/static variable and type
- Document each struct field individually
- Implementation comment: `/* See foo.h.  */`
- Block comments: no `/*`-only or `*/`-only lines, no leading `*`
- Blank line between block comment and variable definition
- Blank line between function comment and function implementation

### Error Handling
- `error()` - User-facing errors that abort the current command
- `warning()` - Non-fatal issues shown to user
- `internal_error()` - Programming errors (replaces abort)
- `internal_warning()` - Internal non-fatal issues
- Error messages should NOT have trailing newlines
- Use `_()` macro for internationalization: `error (_("message"))`

### Exception Handling
Use C++ exceptions with GDB-specific types:
```cpp
try
  {
    // code that may throw
  }
catch (const gdb_exception_quit &ex)
  {
    throw;  // Always re-throw quit exceptions
  }
catch (const gdb_exception_error &ex)
  {
    // Handle error
  }
catch (const gdb_exception &ex)
  {
    // Handle any GDB exception
  }
```

### Memory Management
- Use `gdb::unique_xmalloc_ptr<T>` for C-style malloc'd memory
- Use `std::unique_ptr<T>` for C++ objects
- Use `std::make_unique<T>()` for creating unique_ptrs
- Avoid raw `new`/`delete` - prefer RAII patterns
- Use `xmalloc()`, `xrealloc()`, `xfree()` for C allocations

### Assertions
- `gdb_assert(expr)` - Runtime invariant checks
- `gdb_assert_not_reached("message")` - Unreachable code paths

### Output Functions
- `gdb_printf()` - Formatted output (replaces printf)
- `gdb_puts()` - Simple string output
- `gdb_stdout` / `gdb_stderr` - Output streams
- Use `ATTRIBUTE_PRINTF(n, m)` on printf-like function declarations

### Python Code
- Follow PEP 8 style guidelines
- Use 4-space indentation
- Python scripts for GDB are in `gdb/python/lib/gdb/`

### TCL/Expect Test Code
- Tests use DejaGnu framework
- Test files are in `gdb/testsuite/gdb.*/`
- Use `gdb_test`, `gdb_test_multiple`, `gdb_breakpoint` procs
- Use `with_test_prefix` to organize test output
- Clean up after tests (remove breakpoints, files, etc.)

## Intel-Specific Extensions
- Intel GPU debugging: `gdb/intelgt*` files
- SIMD lane debugging support
- SYCL debugging: `gdb/testsuite/gdb.sycl/`
- Intel Xe architecture target descriptors
- ROCm debugging support: `gdb/solib-rocm.c`, `gdb/testsuite/gdb.rocm/`

## Testing Guidelines
- Always add tests for new features in `gdb/testsuite/`
- Run tests: `make check TESTS="gdb.base/your-test.exp"`
- Run ARI checker: `gdb/contrib/ari/gdb_ari.sh <files>`
- Use `with_test_prefix` to organize test output
- Handle platform-specific behavior with `istarget`

## Generated Files (Not Governed by GNU Style)
The following files are auto-generated and should NOT be manually edited. They are marked with `THIS FILE IS GENERATED` and `buffer-read-only: t`:

### Core Generated Files
- `gdb/gdbarch-gen.c` - Generated from `gdb/gdbarch.py`
- `gdb/gdbarch-gen.h` - Generated from `gdb/gdbarch.py`
- `gdb/target-delegates-gen.c` - Generated from `gdb/make-target-delegates.py`

### Parser Files (Generated from .y files)
- `gdb/ada-exp.c` - Generated from `ada-exp.y`
- `gdb/ada-lex.c` - Generated from `ada-lex.l`
- `gdb/c-exp.c` - Generated from `c-exp.y`
- `gdb/cp-name-parser.c` - Generated from `cp-name-parser.y`
- `gdb/d-exp.c`, `gdb/f-exp.c`, `gdb/go-exp.c`, `gdb/m2-exp.c`, `gdb/p-exp.c`

### Target Description Files
- `gdb/features/*.c` - Generated from XML files via `feature_to_c.sh`
- `gdb/regformats/*.dat` - Generated register format files

### Configuration Files
- `config.h`, `config.h.in` - Generated by autoconf/autoheader
- `Makefile` - Generated from `Makefile.in` by configure

## Commit Message Format
- First line: concise summary (< 72 chars)
- Component prefix: `gdb:`, `gdbserver:`, `testsuite:`, `gdb/python:`
- Blank line followed by detailed description
- Reference issue numbers when applicable
- Explain the "why" not just the "what"

