#pragma once

#include "llvm/ADT/StringRef.h"
#include "llvm/Support/Error.h"

namespace dagdocdiff {

llvm::Error checkDocs(llvm::StringRef source, llvm::StringRef build, llvm::StringRef docs);
llvm::Error approveDocs(llvm::StringRef source, llvm::StringRef build, llvm::StringRef docs,
                        llvm::StringRef page, llvm::StringRef section, llvm::StringRef reason);

} // namespace dagdocdiff
