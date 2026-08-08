# PulseReflect

A reflection tool for the Pulse Game Engine

Works for Linux and Windows (MinGW)

## Dependencies

- LLVM
- Clang

## Example : 

./run.sh or ./run.bat

## CLI options

-f : Specifies a file for which to generate reflection

--dir : Specifies a directory in which to look for files (for reflection generation)

--recursive : Should we look recursively through the directories ?

--clang : Path to the clang lib root (usually /usr/lib/clang/<version> on Linux) (mandatory)

--cpp : Path to the stdlibc++ root (usually /usr/include/c++/<version> on Linux) (mandatory)

-I : Path to the include dir of Pulse (usually <Path/To/Pulse>/src)
