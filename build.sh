cmake -S . -B build -G "Unix Makefiles"
cd build
cmake --build .
cd ..
./run.sh