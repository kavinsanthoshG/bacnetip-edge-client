rm -rf build
conan install –build=missing -of build
cmake -S . -B build -DCMAKE_TOOLCHAIN_FILE=build/conan_toolchain.cmake -DCMAKE_BUILD_TYPE=Release
make
./bacnet-edge-clientapp
