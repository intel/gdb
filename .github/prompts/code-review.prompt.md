# GDB Code Review Guidelines

When reviewing GDB code changes, check for the following:

## Style & Formatting
- GNU coding style compliance (2-space indentation, braces on new lines for functions)
- Proper copyright headers: `Copyright (C) YEAR Free Software Foundation, Inc.`
- No trailing whitespace
- Space after keywords (`if`, `for`, `while`, `switch`)
- Space around operators

## Forbidden Functions (ARI Rules)
The GDB codebase has strict rules about which functions to use:

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
| `#include <assert.h>` | `#include "gdb_assert.h"` |
| `#include <wait.h>` | `#include "gdb_wait.h"` |
| `#include <regex.h>` | `#include "gdb_regex.h"` |

## Error Handling
- Use `error()` for user-facing errors that abort the current command
- Use `warning()` for non-fatal issues
- Use `internal_error()` for programming errors (replaces abort)
- Use `internal_warning()` for internal non-fatal issues
- Error messages should NOT have trailing newlines
- Error messages should use `_()` for internationalization markup

## Exception Handling
- Use `try`/`catch` with `gdb_exception` types:
  - `gdb_exception` - base exception type
  - `gdb_exception_error` - for errors
  - `gdb_exception_quit` - for quit signals (must re-throw)
- Always re-throw `gdb_exception_quit` to allow proper quit handling

## Memory Management
- Prefer `gdb::unique_xmalloc_ptr<T>` for C-style allocations
- Use `std::unique_ptr<T>` for C++ objects
- Use `std::make_unique<T>()` for creating unique_ptrs
- Avoid raw `new`/`delete` when possible
- Use RAII patterns for resource management

## Assertions & Invariants
- Use `gdb_assert(expr)` for runtime invariant checks
- Use `gdb_assert_not_reached("message")` for unreachable code paths
- Include meaningful messages in `gdb_assert_not_reached`

## Output Functions
- Use `gdb_printf()` for formatted output
- Use `gdb_puts()` for simple strings
- Use `gdb_stdout` / `gdb_stderr` for output streams
- Use `ATTRIBUTE_PRINTF(n, m)` on printf-like function declarations

## Intel-Specific Extensions
- Intel GPU debugging code in `gdb/intelgt*` files
- SIMD lane debugging support
- SYCL debugging support
- Intel Xe architecture target descriptors
- Follow patterns in existing Intel-specific code

## Testing Requirements
- New features must have accompanying tests in `gdb/testsuite/`
- Tests use DejaGnu framework (TCL/Expect)
- Use `gdb_test`, `gdb_test_multiple`, `gdb_breakpoint` procs
- Use `with_test_prefix` to organize test output
- Clean up after tests (remove breakpoints, files, etc.)
- Run ARI checker: `gdb/contrib/ari/gdb_ari.sh`

## Documentation
- Public functions should have descriptive comments
- Complex logic should be explained inline
- Update relevant documentation for user-visible changes

## Commit Message Format
- First line: concise summary (< 72 chars)
- Component prefix when applicable: `gdb/python:`, `testsuite:`, `gdbserver:`
- Blank line followed by detailed description
- Reference issue numbers when applicable
- Explain the "why" not just the "what"
