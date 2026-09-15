#include <revolution/OS.h>
#include <cstdio>
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <dbghelp.h>
namespace {
LONG CALLBACK logFault(EXCEPTION_POINTERS* fault) {
    if (fault->ExceptionRecord->ExceptionCode!=EXCEPTION_ACCESS_VIOLATION) return EXCEPTION_CONTINUE_SEARCH;
    auto process=GetCurrentProcess();
    SymInitialize(process,nullptr,TRUE);
    std::fprintf(stderr,"Access violation at %p\n",fault->ExceptionRecord->ExceptionAddress);
    CONTEXT context=*fault->ContextRecord;
    STACKFRAME64 frame{};
    frame.AddrPC={context.Rip,0,AddrModeFlat};
    frame.AddrStack={context.Rsp,0,AddrModeFlat};
    frame.AddrFrame={context.Rbp,0,AddrModeFlat};
    std::fprintf(stderr,"  rcx=%llx rdx=%llx r8=%llx r9=%llx rsp=%llx\n",context.Rcx,context.Rdx,context.R8,context.R9,context.Rsp);
    for (unsigned i=0;i<32 && frame.AddrPC.Offset;++i) {
        alignas(SYMBOL_INFO) char storage[sizeof(SYMBOL_INFO)+MAX_SYM_NAME]{};
        auto* symbol=reinterpret_cast<SYMBOL_INFO*>(storage);
        symbol->SizeOfStruct=sizeof(SYMBOL_INFO); symbol->MaxNameLen=MAX_SYM_NAME;
        DWORD64 displacement=0;
        if (SymFromAddr(process,frame.AddrPC.Offset,&displacement,symbol))
            std::fprintf(stderr,"  %s + 0x%llx\n",symbol->Name,displacement);
        else std::fprintf(stderr,"  %llx\n",frame.AddrPC.Offset);
        if (!StackWalk64(IMAGE_FILE_MACHINE_AMD64,process,GetCurrentThread(),&frame,&context,nullptr,SymFunctionTableAccess64,SymGetModuleBase64,nullptr)) break;
    }
    std::fflush(stderr);
    return EXCEPTION_CONTINUE_SEARCH;
}
}
#endif

extern "C" void OSRegisterVersion(const char* version) {
#ifdef _WIN32
    static const auto handler=AddVectoredExceptionHandler(0,logFault);
#endif
    std::fprintf(stderr,"[nw4r] %s\n",version);
}
