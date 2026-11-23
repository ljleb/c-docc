#pragma once

#include "Graph.h"
#include "llvm/Support/Error.h"

namespace c_docc {

llvm::Expected<Snapshot> analyze(llvm::StringRef sourceRoot, llvm::StringRef buildDirectory);

} // namespace c_docc
