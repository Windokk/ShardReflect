cmake -S . -B build -G "Unix Makefiles"
cd build
cmake --build . -j8
cd ..
./run.sh