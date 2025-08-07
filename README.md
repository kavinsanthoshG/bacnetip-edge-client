# bacnet-edge-clientapp

A C-based BACnet edge client application.

## Build Instructions

1. Install [Conan](https://conan.io/) and [CMake](https://cmake.org/).
2. Install dependencies:
   ```sh
   conan install . --output-folder=build --build=missing
   ```
3. Build the project:
   ```sh
   cmake -B build -S .
   cmake --build build
   ```
4. Run the application from the `build` directory.

## Project Structure
- `src/` - C source files
- `include/` - Header files
- `tests/` - Unit tests
