#pragma once

#include "llvm/ADT/StringRef.h"
#include "llvm/Support/Error.h"

namespace c_docc {

llvm::Error checkDocs(llvm::StringRef source, llvm::StringRef build, llvm::StringRef docs, bool json);
llvm::Error reviewDocs(llvm::StringRef source, llvm::StringRef build, llvm::StringRef docs,
                       llvm::StringRef reason);
llvm::Error approvePage(llvm::StringRef source, llvm::StringRef build, llvm::StringRef docs,
                        llvm::StringRef page, llvm::StringRef reason);
llvm::Error approveDocs(llvm::StringRef source, llvm::StringRef build, llvm::StringRef docs,
                        llvm::StringRef page, llvm::StringRef section, llvm::StringRef reason);

} // namespace c_docc
