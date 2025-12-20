cd build
./PulseReflect --clang /usr/lib/clang/21 \
               --cpp /usr/include/c++/15.2.1 \
               -f /home/windokk/Documents/GitHub/Pulse/src/engine/ecs/components/misc/transform.hpp \
               --dir /home/windokk/Documents/GitHub/Pulse/src/engine/ecs/components/misc/ \
               -I /home/windokk/Documents/GitHub/Pulse/src \
               -I /home/windokk/Documents/GitHub/Pulse/submodules/json/single_include \
               -I /home/windokk/Documents/GitHub/Pulse/submodules/jolt \
               -I /home/windokk/Documents/GitHub/Pulse/submodules/glm \
               -I /home/windokk/Documents/GitHub/Pulse/submodules/freetype/include

# Note : Replace /home/windokk/Documents/GitHub/Pulse... by the path to your Pulse installation