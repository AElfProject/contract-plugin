# contract-plugin
Protoc plugins for code generation of contracts defined in grpc format

## Setup & Build

This project requires cmake & git submodules to build locally.

```
git clone git@github.com:AElfProject/contract-plugin.git
cd contract-plugin
git submodule init
git submodule update

cmake .
make
```
Protoc plugins for code generation of contracts defined in grpc format.

## Local Builds

This build process is to generate a contract_plugin binary for non-HOST machines (e.g Host=OSX Target=Linux).

### Prepare local source files
Before mounting & running the docker-image we need to ensure all local-files are ready for compilation.
```
git submodule init
git submodule update
```

### Linux/arm64

This can be compiled/generated using docker locally (was tested on a OSX M2 Macbook i.e ARM chipset) by following the below steps:

#### Build docker-image
```
docker build -t linux_arm64_basic_compiler --file docker/linux/arm64/Dockerfile .
```

#### Run docker-container with mounted-volume
```
docker run --name arm_compiler_container -v .:/home -it linux_arm64_basic_compiler /bin/sh -c "cmake -DOS_ARCH_TARGET=linux_arm64 . && make"
```

#### Locate the binary
```
cd bin/linux_arm64
file contract_csharp_plugin //to confirm the binary ARCH
```
### Linux/amd64 (x86)

Likewise for x86 binaries you can use a similar docker method but with the amd64 dockerfile.

#### Build docker-image
```
docker build -t linux_x86_basic_compiler --file docker/linux/amd64/Dockerfile .
```

#### Run docker-container
```
docker run --name x86_compiler_container -v .:/home -it linux_x86_basic_compiler /bin/sh -c "cmake -DOS_ARCH_TARGET=linux_amd64 . && make"
```

#### Locate the binary
```
cd bin/linux_amd64
file contract_csharp_plugin //to confirm the binary ARCH
```