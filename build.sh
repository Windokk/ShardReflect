cmake -S . -B build -G Ninja
cd build
cmake --build .
cd ..
./run.sh