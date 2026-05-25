cmake -S . -B build -DSDL_UNIX_CONSOLE_BUILD=ON -DCMAKE_BUILD_TYPE=Release && cmake --build build -j$(nproc)
