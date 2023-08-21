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

## Testing

the compiled `contract_csharp_plugin` binary result is written to `/opt/bin/`. You will need to copy over this binary into the `aelf.Tool` https://github.com/AElfProject/aelf-developer-tools/tree/master/aelf.tools/AElf.Tools/tools/ and build/package that C# project in-order to test your changes (i.e the output C# contract-code). Do note you'll need to build OS-specific executables for windows,linux,osx (based on the OS-folders under `aelf.tools/AElf.Tools/tools/`).