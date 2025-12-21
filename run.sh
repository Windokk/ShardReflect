cd build
./PulseReflect --clang /usr/lib/clang/21 \
               --cpp /usr/include/c++/15.2.1 \
               -f ../../Pulse/src/engine/ecs/components/misc/transform.hpp \
               --dir ../../Pulse/src/engine/ecs/components/misc/ \
               -I ../../Pulse/src \
               -I ../../Pulse/submodules/json/single_include \
               -I ../../Pulse/submodules/jolt \
               -I ../../Pulse/submodules/glm \
               -I ../../Pulse/submodules/freetype/include

# Note : Replace "../Pulse/"... by the path to your Pulse installation