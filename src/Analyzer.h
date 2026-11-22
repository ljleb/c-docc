#pragma once

#include "Graph.h"
#include "llvm/Support/Error.h"

namespace dagdocdiff {

llvm::Expected<Snapshot> analyze(llvm::StringRef sourceRoot, llvm::StringRef buildDirectory);

} // namespace dagdocdiff
