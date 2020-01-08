/* -*- Mode: C++; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 2 -*-
 * vim: set ts=8 sts=2 et sw=2 tw=80:
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef jit_BaselineJIT_h
#define jit_BaselineJIT_h

#include "mozilla/Maybe.h"
#include "mozilla/MemoryReporting.h"
#include "mozilla/Span.h"

#include "ds/LifoAlloc.h"
#include "jit/Bailouts.h"
#include "jit/IonCode.h"
#include "jit/shared/Assembler-shared.h"
#include "vm/EnvironmentObject.h"
#include "vm/JSContext.h"
#include "vm/Realm.h"
#include "vm/TraceLogging.h"

namespace js {
namespace jit {

class ICEntry;
class ICStub;
class ReturnAddressEntry;

// Base class for entries mapping a pc offset to a native code offset.
class BasePCToNativeEntry {
  uint32_t pcOffset_;
  uint32_t nativeOffset_;

 public:
  BasePCToNativeEntry(uint32_t pcOffset, uint32_t nativeOffset)
      : pcOffset_(pcOffset), nativeOffset_(nativeOffset) {}
  uint32_t pcOffset() const { return pcOffset_; }
  uint32_t nativeOffset() const { return nativeOffset_; }
};

// Class used during Baseline compilation to store the native code offset for
// resume offset ops.
class ResumeOffsetEntry : public BasePCToNativeEntry {
 public:
  using BasePCToNativeEntry::BasePCToNativeEntry;
};

using ResumeOffsetEntryVector =
    Vector<ResumeOffsetEntry, 16, SystemAllocPolicy>;

// Largest script that the baseline compiler will attempt to compile.
#if defined(JS_CODEGEN_ARM)
// ARM branches can only reach 32MB, and the macroassembler doesn't mitigate
// that limitation. Use a stricter limit on the acceptable script size to
// avoid crashing when branches go out of range.
static constexpr uint32_t BaselineMaxScriptLength = 1000000u;
#else
static constexpr uint32_t BaselineMaxScriptLength = 0x0fffffffu;
#endif

// Limit the locals on a given script so that stack check on baseline frames
// doesn't overflow a uint32_t value.
// (BaselineMaxScriptSlots * sizeof(Value)) must fit within a uint32_t.
//
// This also applies to the Baseline Interpreter: it ensures we don't run out
// of stack space (and throw over-recursion exceptions) for scripts with a huge
// number of locals. The C++ interpreter avoids this by having heap-allocated
// stack frames.
static constexpr uint32_t BaselineMaxScriptSlots = 0xffffu;

// An entry in the BaselineScript return address table. These entries are used
// to determine the bytecode pc for a return address into Baseline code.
//
// There must be an entry for each location where we can end up calling into
// C++ (directly or via script/trampolines) and C++ can request the current
// bytecode pc (this includes anything that may throw an exception, GC, or walk
// the stack). We currently add entries for each:
//
// * callVM
// * IC
// * DebugTrap (trampoline call)
// * JSOP_RESUME (because this is like a scripted call)
//
// Note: see also BaselineFrame::HAS_OVERRIDE_PC.
class RetAddrEntry {
  // Offset from the start of the JIT code where call instruction is.
  uint32_t returnOffset_;

  // The offset of this bytecode op within the JSScript.
  uint32_t pcOffset_ : 28;

 public:
  enum class Kind : uint32_t {
    // An IC for a JOF_IC op.
    IC,

    // A prologue IC.
    PrologueIC,

    // A callVM for an op.
    CallVM,

    // A callVM not for an op (e.g., in the prologue).
    NonOpCallVM,

    // A callVM for the warmup counter.
    WarmupCounter,

    // A callVM for the over-recursion check on function entry.
    StackCheck,

    // DebugTrapHandler (for debugger breakpoints/stepping).
    DebugTrap,

    // A callVM for Debug{Prologue,AfterYield,Epilogue}.
    DebugPrologue,
    DebugAfterYield,
    DebugEpilogue,

    Invalid
  };

 private:
  // What this entry is for.
  uint32_t kind_ : 4;

 public:
  RetAddrEntry(uint32_t pcOffset, Kind kind, CodeOffset retOffset)
      : returnOffset_(uint32_t(retOffset.offset())),
        pcOffset_(pcOffset),
        kind_(uint32_t(kind)) {
    MOZ_ASSERT(returnOffset_ == retOffset.offset(),
               "retOffset must fit in returnOffset_");

    // The pc offset must fit in at least 28 bits, since we shave off 4 for
    // the Kind enum.
    MOZ_ASSERT(pcOffset_ == pcOffset);
    JS_STATIC_ASSERT(BaselineMaxScriptLength <= (1u << 28) - 1);
    MOZ_ASSERT(pcOffset <= BaselineMaxScriptLength);

    MOZ_ASSERT(kind < Kind::Invalid);
    MOZ_ASSERT(this->kind() == kind, "kind must fit in kind_ bit field");
  }

  CodeOffset returnOffset() const { return CodeOffset(returnOffset_); }

  uint32_t pcOffset() const { return pcOffset_; }

  jsbytecode* pc(JSScript* script) const {
    return script->offsetToPC(pcOffset_);
  }

  Kind kind() const {
    MOZ_ASSERT(kind_ < uint32_t(Kind::Invalid));
    return Kind(kind_);
  }
};

struct BaselineScript final {
 private:
  // Code pointer containing the actual method.
  HeapPtr<JitCode*> method_ = nullptr;

  // Baseline Interpreter can enter Baseline Compiler code at this address. This
  // is right after the warm-up counter check in the prologue.
  uint32_t warmUpCheckPrologueOffset_;

  // The offsets for the toggledJump instructions for profiler instrumentation.
  uint32_t profilerEnterToggleOffset_;
  uint32_t profilerExitToggleOffset_;

  // The offsets and event used for Tracelogger toggling.
#ifdef JS_TRACE_LOGGING
#  ifdef DEBUG
  bool traceLoggerScriptsEnabled_ = false;
  bool traceLoggerEngineEnabled_ = false;
#  endif
  TraceLoggerEvent traceLoggerScriptEvent_ = {};
#endif

 private:
  uint32_t retAddrEntriesOffset_ = 0;
  uint32_t retAddrEntries_ = 0;

  uint32_t osrEntriesOffset_ = 0;
  uint32_t osrEntries_ = 0;

  uint32_t debugTrapEntriesOffset_ = 0;
  uint32_t debugTrapEntries_ = 0;

  // We store the native code address corresponding to each bytecode offset in
  // the script's resumeOffsets list.
  uint32_t resumeEntriesOffset_ = 0;

  // By default tracelogger is disabled. Therefore we disable the logging code
  // by default. We store the offsets we must patch to enable the logging.
  uint32_t traceLoggerToggleOffsetsOffset_ = 0;
  uint32_t numTraceLoggerToggleOffsets_ = 0;

  // Total size of the allocation including BaselineScript and trailing data.
  uint32_t allocBytes_ = 0;

 public:
  enum Flag {
    // Flag set when compiled for use with Debugger. Handles various
    // Debugger hooks and compiles toggled calls for traps.
    HAS_DEBUG_INSTRUMENTATION = 1 << 0,

    // Flag is set if this script has profiling instrumentation turned on.
    PROFILER_INSTRUMENTATION_ON = 1 << 1,
  };

  // Native code offset for OSR from Baseline Interpreter into Baseline JIT at
  // JSOP_LOOPENTRY ops.
  class OSREntry : public BasePCToNativeEntry {
   public:
    using BasePCToNativeEntry::BasePCToNativeEntry;
  };

  // Native code offset for a debug trap when the script is compiled with debug
  // instrumentation.
  class DebugTrapEntry : public BasePCToNativeEntry {
   public:
    using BasePCToNativeEntry::BasePCToNativeEntry;
  };

 private:
  uint8_t flags_ = 0;

  // An ion compilation that is ready, but isn't linked yet.
  IonBuilder* pendingBuilder_ = nullptr;

  // Use BaselineScript::New to create new instances. It will properly
  // allocate trailing objects.
  BaselineScript(uint32_t warmUpCheckPrologueOffset,
                 uint32_t profilerEnterToggleOffset,
                 uint32_t profilerExitToggleOffset)
      : warmUpCheckPrologueOffset_(warmUpCheckPrologueOffset),
        profilerEnterToggleOffset_(profilerEnterToggleOffset),
        profilerExitToggleOffset_(profilerExitToggleOffset) {}

  // Translate an offset into a concrete pointer.
  template <typename T>
  T* offsetToPointer(size_t offset) const {
    uintptr_t base = reinterpret_cast<uintptr_t>(this);
    uintptr_t elem = base + offset;
    return reinterpret_cast<T*>(elem);
  }

  mozilla::Span<RetAddrEntry> retAddrEntries() const {
    return mozilla::MakeSpan(
        offsetToPointer<RetAddrEntry>(retAddrEntriesOffset_), retAddrEntries_);
  }
  mozilla::Span<OSREntry> osrEntries() const {
    return mozilla::MakeSpan(offsetToPointer<OSREntry>(osrEntriesOffset_),
                             osrEntries_);
  }
  mozilla::Span<DebugTrapEntry> debugTrapEntries() const {
    return mozilla::MakeSpan(
        offsetToPointer<DebugTrapEntry>(debugTrapEntriesOffset_),
        debugTrapEntries_);
  }

#ifdef JS_TRACE_LOGGING
  mozilla::Span<uint32_t> traceLoggerToggleOffsets() const {
    MOZ_ASSERT(traceLoggerToggleOffsetsOffset_);
    return mozilla::MakeSpan(
        offsetToPointer<uint32_t>(traceLoggerToggleOffsetsOffset_),
        numTraceLoggerToggleOffsets_);
  }
#endif

  // Note: this doesn't return a Span<> because BaselineScript does not store
  // the number of entries.
  uint8_t** resumeEntryList() const {
    return offsetToPointer<uint8_t*>(resumeEntriesOffset_);
  }

 public:
  static BaselineScript* New(JSScript* jsscript,
                             uint32_t warmUpCheckPrologueOffset,
                             uint32_t profilerEnterToggleOffset,
                             uint32_t profilerExitToggleOffset,
                             size_t retAddrEntries, size_t osrEntries,
                             size_t debugTrapEntries, size_t resumeEntries,
                             size_t traceLoggerToggleOffsetEntries);

  static void Destroy(JSFreeOp* fop, BaselineScript* script);

  void trace(JSTracer* trc);

  static inline size_t offsetOfMethod() {
    return offsetof(BaselineScript, method_);
  }

  void addSizeOfIncludingThis(mozilla::MallocSizeOf mallocSizeOf,
                              size_t* data) const {
    *data += mallocSizeOf(this);
  }

  void setHasDebugInstrumentation() { flags_ |= HAS_DEBUG_INSTRUMENTATION; }
  bool hasDebugInstrumentation() const {
    return flags_ & HAS_DEBUG_INSTRUMENTATION;
  }

  uint8_t* warmUpCheckPrologueAddr() const {
    return method_->raw() + warmUpCheckPrologueOffset_;
  }

  JitCode* method() const { return method_; }
  void setMethod(JitCode* code) {
    MOZ_ASSERT(!method_);
    method_ = code;
  }

  bool containsCodeAddress(uint8_t* addr) const {
    return method()->raw() <= addr &&
           addr <= method()->raw() + method()->instructionsSize();
  }

  uint8_t* returnAddressForEntry(const RetAddrEntry& ent);

  const RetAddrEntry& retAddrEntryFromPCOffset(uint32_t pcOffset,
                                               RetAddrEntry::Kind kind);
  const RetAddrEntry& prologueRetAddrEntry(RetAddrEntry::Kind kind);
  const RetAddrEntry& retAddrEntryFromReturnOffset(CodeOffset returnOffset);
  const RetAddrEntry& retAddrEntryFromReturnAddress(uint8_t* returnAddr);

  uint8_t* nativeCodeForOSREntry(uint32_t pcOffset);

  void copyRetAddrEntries(const RetAddrEntry* entries);
  void copyOSREntries(const OSREntry* entries);
  void copyDebugTrapEntries(const DebugTrapEntry* entries);

  // Copy resumeOffsets list from |script| and convert the pcOffsets
  // to native addresses in the Baseline code based on |entries|.
  void computeResumeNativeOffsets(JSScript* script,
                                  const ResumeOffsetEntryVector& entries);

  // Return the bytecode offset for a given native code address. Be careful
  // when using this method: it's an approximation and not guaranteed to be the
  // correct pc.
  jsbytecode* approximatePcForNativeAddress(JSScript* script,
                                            uint8_t* nativeAddress);

  // Toggle debug traps (used for breakpoints and step mode) in the script.
  // If |pc| is nullptr, toggle traps for all ops in the script. Else, only
  // toggle traps at |pc|.
  void toggleDebugTraps(JSScript* script, jsbytecode* pc);

  void toggleProfilerInstrumentation(bool enable);
  bool isProfilerInstrumentationOn() const {
    return flags_ & PROFILER_INSTRUMENTATION_ON;
  }

#ifdef JS_TRACE_LOGGING
  void initTraceLogger(JSScript* script, const Vector<CodeOffset>& offsets);
  void toggleTraceLoggerScripts(JSScript* script, bool enable);
  void toggleTraceLoggerEngine(bool enable);

  static size_t offsetOfTraceLoggerScriptEvent() {
    return offsetof(BaselineScript, traceLoggerScriptEvent_);
  }
#endif

  static size_t offsetOfResumeEntriesOffset() {
    return offsetof(BaselineScript, resumeEntriesOffset_);
  }

  static void writeBarrierPre(Zone* zone, BaselineScript* script);

  bool hasPendingIonBuilder() const { return !!pendingBuilder_; }

  js::jit::IonBuilder* pendingIonBuilder() {
    MOZ_ASSERT(hasPendingIonBuilder());
    return pendingBuilder_;
  }
  void setPendingIonBuilder(JSRuntime* rt, JSScript* script,
                            js::jit::IonBuilder* builder);
  void removePendingIonBuilder(JSRuntime* rt, JSScript* script);

  size_t allocBytes() const { return allocBytes_; }
};
static_assert(
    sizeof(BaselineScript) % sizeof(uintptr_t) == 0,
    "The data attached to the script must be aligned for fast JIT access.");

enum class BaselineTier { Interpreter, Compiler };

template <BaselineTier Tier>
MethodStatus CanEnterBaselineMethod(JSContext* cx, RunState& state);

MethodStatus CanEnterBaselineInterpreterAtBranch(JSContext* cx,
                                                 InterpreterFrame* fp);

JitExecStatus EnterBaselineInterpreterAtBranch(JSContext* cx,
                                               InterpreterFrame* fp,
                                               jsbytecode* pc);

bool CanBaselineInterpretScript(JSScript* script);

// Called by the Baseline Interpreter to compile a script for the Baseline JIT.
// |res| is set to the native code address in the BaselineScript to jump to, or
// nullptr if we were unable to compile this script.
bool BaselineCompileFromBaselineInterpreter(JSContext* cx, BaselineFrame* frame,
                                            uint8_t** res);

void FinishDiscardBaselineScript(JSFreeOp* fop, JSScript* script);

void AddSizeOfBaselineData(JSScript* script, mozilla::MallocSizeOf mallocSizeOf,
                           size_t* data);

void ToggleBaselineProfiling(JSContext* cx, bool enable);

void ToggleBaselineTraceLoggerScripts(JSRuntime* runtime, bool enable);
void ToggleBaselineTraceLoggerEngine(JSRuntime* runtime, bool enable);

struct alignas(uintptr_t) BaselineBailoutInfo {
  // Pointer into the current C stack, where overwriting will start.
  uint8_t* incomingStack = nullptr;

  // The top and bottom heapspace addresses of the reconstructed stack
  // which will be copied to the bottom.
  uint8_t* copyStackTop = nullptr;
  uint8_t* copyStackBottom = nullptr;

  // The value of the frame pointer register on resume.
  void* resumeFramePtr = nullptr;

  // The native code address to resume into.
  void* resumeAddr = nullptr;

  // If non-null, we have to type monitor the top stack value for this pc (we
  // resume right after it).
  jsbytecode* monitorPC = nullptr;

  // The bytecode pc of try block and fault block.
  jsbytecode* tryPC = nullptr;
  jsbytecode* faultPC = nullptr;

  // Number of baseline frames to push on the stack.
  uint32_t numFrames = 0;

  // Size of the innermost BaselineFrame. This is equivalent to
  // BaselineFrame::debugFrameSize_ in debug builds.
  uint32_t frameSizeOfInnerMostFrame = 0;

  // If Ion bailed out on a global script before it could perform the global
  // declaration conflicts check. In such cases the baseline script is
  // resumed at the first pc instead of the prologue, so an extra flag is
  // needed to perform the check.
  bool checkGlobalDeclarationConflicts = false;

  // The bailout kind.
  mozilla::Maybe<BailoutKind> bailoutKind = {};

  BaselineBailoutInfo() = default;
  BaselineBailoutInfo(const BaselineBailoutInfo&) = default;

  void operator=(const BaselineBailoutInfo&) = delete;
};

MOZ_MUST_USE bool BailoutIonToBaseline(
    JSContext* cx, JitActivation* activation, const JSJitFrameIter& iter,
    bool invalidate, BaselineBailoutInfo** bailoutInfo,
    const ExceptionBailoutInfo* exceptionInfo);

MethodStatus BaselineCompile(JSContext* cx, JSScript* script,
                             bool forceDebugInstrumentation = false);

static const unsigned BASELINE_MAX_ARGS_LENGTH = 20000;

// Class storing the generated Baseline Interpreter code for the runtime.
class BaselineInterpreter {
 public:
  struct CallVMOffsets {
    uint32_t debugPrologueOffset = 0;
    uint32_t debugEpilogueOffset = 0;
    uint32_t debugAfterYieldOffset = 0;
  };
  struct ICReturnOffset {
    uint32_t offset;
    JSOp op;
    ICReturnOffset(uint32_t offset, JSOp op) : offset(offset), op(op) {}
  };
  using ICReturnOffsetVector = Vector<ICReturnOffset, 0, SystemAllocPolicy>;

 private:
  // The interpreter code.
  JitCode* code_ = nullptr;

  // Offset of the code to start interpreting a bytecode op.
  uint32_t interpretOpOffset_ = 0;

  // Like interpretOpOffset_ but skips the debug trap for the current op.
  uint32_t interpretOpNoDebugTrapOffset_ = 0;

  // Early Ion bailouts will enter at this address. This is after frame
  // construction and environment initialization.
  uint32_t bailoutPrologueOffset_ = 0;

  // Offset of the GeneratorThrowOrReturn callVM. See also
  // emitGeneratorThrowOrReturnCallVM.
  uint32_t generatorThrowOrReturnCallOffset_ = 0;

  // The offsets for the toggledJump instructions for profiler instrumentation.
  uint32_t profilerEnterToggleOffset_ = 0;
  uint32_t profilerExitToggleOffset_ = 0;

  // Offset of the jump (tail call) to the debug trap handler trampoline code.
  // When the debugger is enabled, NOPs are patched to calls to this location.
  uint32_t debugTrapHandlerOffset_ = 0;

  // The offsets of toggled jumps for debugger instrumentation.
  using CodeOffsetVector = Vector<uint32_t, 0, SystemAllocPolicy>;
  CodeOffsetVector debugInstrumentationOffsets_;

  // Offsets of toggled calls to the DebugTrapHandler trampoline (for
  // breakpoints and stepping).
  CodeOffsetVector debugTrapOffsets_;

  // Offsets of toggled jumps for code coverage.
  CodeOffsetVector codeCoverageOffsets_;

  // Offsets of IC calls for IsIonInlinableOp ops, for Ion bailouts.
  ICReturnOffsetVector icReturnOffsets_;

  // Offsets of some callVMs for BaselineDebugModeOSR.
  CallVMOffsets callVMOffsets_;

  uint8_t* codeAtOffset(uint32_t offset) const {
    MOZ_ASSERT(offset > 0);
    MOZ_ASSERT(offset < code_->instructionsSize());
    return codeRaw() + offset;
  }

 public:
  BaselineInterpreter() = default;

  BaselineInterpreter(const BaselineInterpreter&) = delete;
  void operator=(const BaselineInterpreter&) = delete;

  void init(JitCode* code, uint32_t interpretOpOffset,
            uint32_t interpretOpNoDebugTrapOffset,
            uint32_t bailoutPrologueOffset,
            uint32_t generatorThrowOrReturnCallOffset,
            uint32_t profilerEnterToggleOffset,
            uint32_t profilerExitToggleOffset, uint32_t debugTrapHandlerOffset,
            CodeOffsetVector&& debugInstrumentationOffsets,
            CodeOffsetVector&& debugTrapOffsets,
            CodeOffsetVector&& codeCoverageOffsets,
            ICReturnOffsetVector&& icReturnOffsets,
            const CallVMOffsets& callVMOffsets);

  uint8_t* codeRaw() const { return code_->raw(); }

  uint8_t* retAddrForDebugPrologueCallVM() const {
    return codeAtOffset(callVMOffsets_.debugPrologueOffset);
  }
  uint8_t* retAddrForDebugEpilogueCallVM() const {
    return codeAtOffset(callVMOffsets_.debugEpilogueOffset);
  }
  uint8_t* retAddrForDebugAfterYieldCallVM() const {
    return codeAtOffset(callVMOffsets_.debugAfterYieldOffset);
  }
  uint8_t* bailoutPrologueEntryAddr() const {
    return codeAtOffset(bailoutPrologueOffset_);
  }

  uint8_t* retAddrForIC(JSOp op) const;

  TrampolinePtr interpretOpAddr() const {
    return TrampolinePtr(codeAtOffset(interpretOpOffset_));
  }
  TrampolinePtr interpretOpNoDebugTrapAddr() const {
    return TrampolinePtr(codeAtOffset(interpretOpNoDebugTrapOffset_));
  }
  TrampolinePtr generatorThrowOrReturnCallAddr() const {
    return TrampolinePtr(codeAtOffset(generatorThrowOrReturnCallOffset_));
  }

  void toggleProfilerInstrumentation(bool enable);
  void toggleDebuggerInstrumentation(bool enable);

  void toggleCodeCoverageInstrumentationUnchecked(bool enable);
  void toggleCodeCoverageInstrumentation(bool enable);
};

MOZ_MUST_USE bool GenerateBaselineInterpreter(JSContext* cx,
                                              BaselineInterpreter& interpreter);

}  // namespace jit
}  // namespace js

namespace JS {

template <>
struct DeletePolicy<js::jit::BaselineScript> {
  explicit DeletePolicy(JSRuntime* rt) : rt_(rt) {}
  void operator()(const js::jit::BaselineScript* script);

 private:
  JSRuntime* rt_;
};

}  // namespace JS

#endif /* jit_BaselineJIT_h */
