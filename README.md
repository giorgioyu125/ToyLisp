# ToyLisp

A simple C implemention into a single file of less than 1000 loc of a lisp interpreter
that supports both REPL and file execution.

## Building the Project

To build the project, you need `meson` and a C compiler (like GCC or Clang) installed.

1.  **Setup the build directory:**
    This command configures the project and prepares the build environment.
    ```bash
    meson setup build
    ```

2.  **Compile the project:**
    This command compiles the source code.
    ```bash
    cd build
    ninja -C .
    ```

3.  **Run the interpreter:**
    The final executable will be located in the `build` directory.
    ```bash
    ./toylisp
    ```
