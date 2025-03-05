# How to create a pass?

## Write Pass

## Register Pass

All registration should be implemented in RegisterPasses.cpp.

* Include the header file of your pass: 
```c
#include "MyPass.h"
```

* According to the type of pass (ModulePass, FunctionPass etc.), add into suitable register callback, note that 'Name' tells the parameter of 'opt --passes' command: 
```cpp
PB.registerPipelineParsingCallback(
                [](StringRef Name, llvm::ModulePassManager &PM,
                   ArrayRef<llvm::PassBuilder::PipelineElement>) {
                  if (Name == "mypass") {
                    PM.addPass(MyPass());
                    return true;
                  }
                  return false;
                });
```

## Build Pass Plugin

* Add cpp file of the pass into CMakeLists.txt:
```
add_llvm_pass_plugin(MyPass
    lib/RegisterPasses.cpp
    lib/MyPass.cpp
)
```

## Best Practice

* `module.print(errs(), nullptr)` prints changed IR before LLVM verifier, which is helpful while LLVM crashes.

* `verifyModule(module, &errs()))` return false if verifier fails.
