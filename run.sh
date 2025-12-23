cd build
./PulseReflect --clang /usr/lib/clang/21 \
               --cpp /usr/include/c++/15.2.1 \
               -f ../../Pulse/src/engine/ecs/components/misc/transform.hpp \
               --dir ../../Pulse/src/engine/ecs/components/misc/ \
               --dir ../../Pulse/src/engine/ecs/components/rendering/ \
               --dir ../../Pulse/src/engine/ecs/components/physics/ \
               --dir ../../Pulse/src/engine/ecs/components/audio/ \
               -I "../../Pulse/src;../../Pulse/submodules/;../../Pulse/submodules/json/single_include" \
               -I ../../Pulse/submodules/jolt \
               -I ../../Pulse/submodules/glm \
               -I ../../Pulse/submodules/freetype/include \
               -I ../../../../Downloads/fmodstudioapi20309linux/api/core/inc

# Note : Replace "../Pulse/" by the path to your Pulse installation