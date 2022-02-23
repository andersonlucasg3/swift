//===--- CodeCompletion.h - Routines for code completion --------*- C++ -*-===//
//
// This source file is part of the Swift.org open source project
//
// Copyright (c) 2014 - 2017 Apple Inc. and the Swift project authors
// Licensed under Apache License v2.0 with Runtime Library Exception
//
// See https://swift.org/LICENSE.txt for license information
// See https://swift.org/CONTRIBUTORS.txt for the list of Swift project authors
//
//===----------------------------------------------------------------------===//

#ifndef SWIFT_IDE_CODECOMPLETION_H
#define SWIFT_IDE_CODECOMPLETION_H

#include "CodeCompletionResultType.h"
#include "swift/AST/Identifier.h"
#include "swift/Basic/Debug.h"
#include "swift/Basic/LLVM.h"
#include "swift/Basic/OptionSet.h"
#include "swift/Basic/StringExtras.h"
#include "swift/Frontend/Frontend.h"
#include "swift/IDE/CodeCompletionResult.h"
#include "swift/IDE/CodeCompletionResultSink.h"
#include "swift/IDE/CodeCompletionString.h"
#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/StringMap.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/Support/Allocator.h"
#include "llvm/Support/TrailingObjects.h"
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace swift {
class CodeCompletionCallbacksFactory;
class Decl;
class DeclContext;
class FrontendOptions;
class ModuleDecl;
class SourceFile;

namespace ide {

class CodeCompletionCache;
class CodeCompletionContext;
class CodeCompletionResultBuilder;
struct CodeCompletionResultSink;
struct RequestedCachedModule;

/// A routine to remove code completion tokens from code completion
/// tests.
///
/// \code
/// code-completion-token:
///     '#^' identifier '^#'
/// \endcode
///
/// \param Input test source code.
/// \param TokenName names the token whose position should be returned in
/// \p CompletionOffset.
/// \param CompletionOffset set to ~0U on error, or to a 0-based byte offset on
/// success.
///
/// \returns test source code without any code completion tokens.
std::string removeCodeCompletionTokens(StringRef Input,
                                       StringRef TokenName,
                                       unsigned *CompletionOffset);

template <typename T>
ArrayRef<T> copyArray(llvm::BumpPtrAllocator &Allocator,
                            ArrayRef<T> Arr) {
  T *Buffer = Allocator.Allocate<T>(Arr.size());
  std::copy(Arr.begin(), Arr.end(), Buffer);
  return llvm::makeArrayRef(Buffer, Arr.size());
}

/// A utility for calculating the import depth of a given module. Direct imports
/// have depth 1, imports of those modules have depth 2, etc.
///
/// Special modules such as Playground auxiliary sources are considered depth
/// 0.
class ImportDepth {
  llvm::StringMap<uint8_t> depths;

public:
  ImportDepth() = default;
  ImportDepth(ASTContext &context, const FrontendOptions &frontendOptions);

  Optional<uint8_t> lookup(StringRef module) {
    auto I = depths.find(module);
    if (I == depths.end())
      return None;
    return I->getValue();
  }
};

class CodeCompletionContext {
  friend class CodeCompletionResultBuilder;

  /// A set of current completion results.
  CodeCompletionResultSink CurrentResults;

public:
  CodeCompletionCache &Cache;
  CompletionKind CodeCompletionKind = CompletionKind::None;

  enum class TypeContextKind {
    /// There is no known contextual type. All types are equally good.
    None,

    /// There is a contextual type from a single-expression closure/function
    /// body. The context is a hint, and enables unresolved member completion,
    /// but should not hide any results.
    SingleExpressionBody,

    /// There are known contextual types, or there aren't but a nonvoid type is expected.
    Required,
  };

  TypeContextKind typeContextKind = TypeContextKind::None;

  /// Whether there may be members that can use implicit member syntax,
  /// e.g. `x = .foo`.
  bool MayUseImplicitMemberExpr = false;

  /// Flag to indicate that the completion is happening reusing ASTContext
  /// from the previous completion.
  /// NOTE: Do not use this to change the behavior. This is only for debugging.
  bool ReusingASTContext = false;

  CodeCompletionContext(CodeCompletionCache &Cache)
      : Cache(Cache) {}

  void setAnnotateResult(bool flag) { CurrentResults.annotateResult = flag; }
  bool getAnnotateResult() const { return CurrentResults.annotateResult; }

  void setIncludeObjectLiterals(bool flag) {
    CurrentResults.includeObjectLiterals = flag;
  }
  bool includeObjectLiterals() const {
    return CurrentResults.includeObjectLiterals;
  }

  void setAddInitsToTopLevel(bool flag) {
    CurrentResults.addInitsToTopLevel = flag;
  }
  bool getAddInitsToTopLevel() const {
    return CurrentResults.addInitsToTopLevel;
  }

  void setCallPatternHeuristics(bool flag) {
    CurrentResults.enableCallPatternHeuristics = flag;
  }
  bool getCallPatternHeuristics() const {
    return CurrentResults.enableCallPatternHeuristics;
  }

  void setAddCallWithNoDefaultArgs(bool flag) {
    CurrentResults.addCallWithNoDefaultArgs = flag;
  }
  bool addCallWithNoDefaultArgs() const {
    return CurrentResults.addCallWithNoDefaultArgs;
  }

  /// Allocate a string owned by the code completion context.
  StringRef copyString(StringRef Str);

  /// Sort code completion results in an implementation-defined order
  /// in place.
  static std::vector<CodeCompletionResult *>
  sortCompletionResults(ArrayRef<CodeCompletionResult *> Results);

  CodeCompletionResultSink &getResultSink() {
    return CurrentResults;
  }
};

struct SwiftCompletionInfo {
  swift::ASTContext *swiftASTContext = nullptr;
  const swift::CompilerInvocation *invocation = nullptr;
  CodeCompletionContext *completionContext = nullptr;
};

/// An abstract base class for consumers of code completion results.
/// \see \c SimpleCachingCodeCompletionConsumer.
class CodeCompletionConsumer {
public:
  virtual ~CodeCompletionConsumer() {}
  virtual void
  handleResultsAndModules(CodeCompletionContext &context,
                          ArrayRef<RequestedCachedModule> requestedModules,
                          DeclContext *DC) = 0;
};

/// A simplified code completion consumer interface that clients can use to get
/// CodeCompletionResults with automatic caching of top-level completions from
/// imported modules.
struct SimpleCachingCodeCompletionConsumer : public CodeCompletionConsumer {

  // Implement the CodeCompletionConsumer interface.
  void handleResultsAndModules(CodeCompletionContext &context,
                               ArrayRef<RequestedCachedModule> requestedModules,
                               DeclContext *DCForModules) override;

  /// Clients should override this method to receive \p Results.
  virtual void handleResults(CodeCompletionContext &context) = 0;
};

/// Create a factory for code completion callbacks.
CodeCompletionCallbacksFactory *
makeCodeCompletionCallbacksFactory(CodeCompletionContext &CompletionContext,
                                   CodeCompletionConsumer &Consumer);

/// Lookup the top-level code completions from \p module and store them in
/// \p targetSink.
///
/// Results are looked up as if in \p currDeclContext, which may be null.
void lookupCodeCompletionResultsFromModule(CodeCompletionResultSink &targetSink,
                                           const ModuleDecl *module,
                                           ArrayRef<std::string> accessPath,
                                           bool needLeadingDot,
                                           const SourceFile *SF);

} // end namespace ide
} // end namespace swift

#endif // SWIFT_IDE_CODECOMPLETION_H
