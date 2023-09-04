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

### Linux/arm64

This can be compiled/generated using docker locally (was tested on a OSX M2 Macbook i.e ARM chipset) by following the below steps:

#### Build docker-image
```
docker build -t linux_arm64_basic_compiler --file Dockerfile.linux_arm64
```

#### Run docker-container
```
docker run --name arm_compiler_container -d -t linux_arm64_basic_compiler
```

#### Copy binary from container's /opt/bin folder
```
docker cp arm_compiler_container:/workdir/opt/bin/contract_csharp_plugin .
```
