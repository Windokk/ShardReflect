cmake -S . -B build -G "MinGW Makefiles"
cd build
cmake --build . -j8
cd ..
start run.bat