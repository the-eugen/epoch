# Epoch Project

Epoch is a C-based retro-console emulation project.

## Folder structure
- Source code is in the src subfolder
- Build artifacts are in obj subfolder, better to ignore that when trying to find code

## Coding Style
- Language Standard: C11 (`--std=gnu11`).
- Data Types: Uses fixed-width types from `<stdint.h>` (e.g., `uint8_t`, `uint64_t`).
- Code Organization:
  - Logic is encapsulated in `struct`s.
  - Internal helper functions are marked `static`.
  - Conditional compilation is used for test-specific code (`#ifdef EP_CONFIG_TEST`).
- Error Handling: Uses custom `ep_assert` and `ep_verify` macros for assertions and verification.
- Commenting Style: Prefer plain text comments; avoid Doxygen/Javadoc-style tags (e.g., `@brief`, `@param`). Preserve existing inline and field-level comments.

## Building
The project uses a custom `Makefile`.
- Default Build: `make`
- Clean: `make clean`
- Debug build: `make CONFIG_DEBUG=1 V=1"

## Testing
The project includes an embedded test framework.
- Build with tests: `CONFIG_TEST=1 make`
- Tests: Tests are defined using `ep_test(...)` macros within source files.
- Test Generation: Some tests are generated via Python scripts (`tools/gen_6502_tests.py`) which output `src/6502_tests.inc`.
- When adding code don't try to build the project unless asked to build and validate
