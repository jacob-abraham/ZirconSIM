#include "cpu.h"
#include "isa/inst.h"
#include "isa/instruction_match.h"
#include "isa/rf.h"
#include "syscall/syscall.h"
#include <iomanip>
#include <iostream>
#include <sstream>
#include <string>

#include "event/event.h"
#include <fstream>

namespace cpu {

// use raw(addr) so we don't log mem access
uint32_t HartState::getInstWord() const {
    auto ptr = memimg.raw(pc);
    return *((uint32_t*)ptr);
}

HartState::HartState(mem::MemoryImage& m) : memimg(m), executing(true) {}

Hart::Hart(mem::MemoryImage& m) : hs(m) {}

bool Hart::shouldHalt() {
    // halt if no longer executing
    if(!hs.executing) return true;
    //if pc is beyond the bounds of memory , return true
    if(hs.memimg.raw(hs.pc) == nullptr) return true;
    // if the instruction just executed was a jmp to itself, halt
    uint32_t inst = hs.getInstWord();
    auto op = isa::inst::decodeInstruction(inst);
    auto jmp_target =
        instruction::signext64<20>(instruction::getJTypeImm(inst)) + hs.pc;
    return (
        op == isa::inst::Opcode::rv32i_jal && jmp_target == hs.pc.previous());
}

uint64_t Hart::alloc(size_t n) {
    auto ptr = hs.memory_locations["heap_end"];
    hs.memimg.allocate(ptr, n);
    hs.memory_locations["heap_end"] += n;
    return ptr;
}
void Hart::copyToHart(void* src, uint64_t dst, size_t n) {
    for(size_t i = 0; i < n; i++) {
        hs.memimg.byte(dst + i) = ((char*)src)[i];
    }
}

enum class AUXVecType : uint64_t {
    AT_NULL = 0,    /* End of vector */
    AT_IGNORE = 1,  /* Entry should be ignored */
    AT_EXECFD = 2,  /* File descriptor of program */
    AT_PHDR = 3,    /* Program headers for program */
    AT_PHENT = 4,   /* Size of program header entry */
    AT_PHNUM = 5,   /* Number of program headers */
    AT_PAGESZ = 6,  /* System page size */
    AT_BASE = 7,    /* Base address of interpreter */
    AT_FLAGS = 8,   /* Flags */
    AT_ENTRY = 9,   /* Entry point of program */
    AT_NOTELF = 10, /* Program is not ELF */
    AT_UID = 11,    /* Real uid */
    AT_EUID = 12,   /* Effective uid */
    AT_GID = 13,    /* Real gid */
    AT_EGID = 14,   /* Effective gid */
    AT_CLKTCK = 17, /* Frequency of times() */

    /* Some more special a_type values describing the hardware.  */
    AT_PLATFORM = 15, /* String identifying platform.  */
    AT_HWCAP = 16,    /* Machine-dependent hints about
                                 processor capabilities.  */

    /* This entry gives some information about the FPU initialization
       performed by the kernel.  */
    AT_FPUCW = 18, /* Used FPU control word.  */

    /* Cache block sizes.  */
    AT_DCACHEBSIZE = 19, /* Data cache block size.  */
    AT_ICACHEBSIZE = 20, /* Instruction cache block size.  */
    AT_UCACHEBSIZE = 21, /* Unified cache block size.  */

    /* A special ignored value for PPC, used by the kernel to control the
       interpretation of the AUXV. Must be > 16.  */
    AT_IGNOREPPC = 22, /* Entry should be ignored.  */

    AT_SECURE = 23, /* Boolean, was exec setuid-like?  */

    AT_BASE_PLATFORM = 24, /* String identifying real platforms.*/

    AT_RANDOM = 25, /* Address of 16 random bytes.  */

    AT_HWCAP2 = 26, /* More machine-dependent hints about
                               processor capabilities.  */

    AT_EXECFN = 31, /* Filename of executable.  */

    /* Pointer to the global system page used for system calls and other
       nice things.  */
    AT_SYSINFO = 32,
    AT_SYSINFO_EHDR = 33,

    /* Shapes of the caches.  Bits 0-3 contains associativity; bits 4-7 contains
       log2 of line size; mask those to get cache size.  */
    AT_L1I_CACHESHAPE = 34,
    AT_L1D_CACHESHAPE = 35,
    AT_L2_CACHESHAPE = 36,
    AT_L3_CACHESHAPE = 37,

    /* Shapes of the caches, with more room to describe them.
       *GEOMETRY are comprised of cache line size in bytes in the bottom 16 bits
       and the cache associativity in the next 16 bits.  */
    AT_L1I_CACHESIZE = 40,
    AT_L1I_CACHEGEOMETRY = 41,
    AT_L1D_CACHESIZE = 42,
    AT_L1D_CACHEGEOMETRY = 43,
    AT_L2_CACHESIZE = 44,
    AT_L2_CACHEGEOMETRY = 45,
    AT_L3_CACHESIZE = 46,
    AT_L3_CACHEGEOMETRY = 47,

    AT_MINSIGSTKSZ = 51, /* Stack needed for signal delivery  */
};

void Hart::init_heap() {
    // initial heap size is 0
    // uint64_t SPACING = 0x10000;
    hs.memory_locations["heap_start"] = 0x100000000;

    // hs.memory_locations["stack_end"] + SPACING;
    uint64_t heap_size = 0;
    hs.memory_locations["heap_end"] =
        hs.memory_locations["heap_start"] + heap_size;
    hs.memimg.allocate(hs.memory_locations["heap_start"], heap_size);
}

intptr_t alignup(intptr_t ptr, unsigned n) {
    unsigned bitmask = (1 << n) - 1;
    ptr = (ptr + bitmask) & ~bitmask;
    return ptr;
}

void Hart::init_stack(
    std::vector<std::string> argv,
    common::ordered_map<std::string, std::string> envp) {
    // allocate a stack region at 0x7fffffff00000000-0x7fffffff00010000
    hs.memory_locations["stack_start"] = 0x7fffffff00000000;
    uint64_t stack_size = 0x10000;
    hs.memory_locations["stack_end"] =
        hs.memory_locations["stack_start"] + stack_size;
    hs.memimg.allocate(hs.memory_locations["stack_start"], stack_size);
    auto sp = hs.memory_locations["stack_end"];

    // auxvec
    common::ordered_map<AUXVecType, uint64_t> auxvec;
    auxvec.insert_or_assign(AUXVecType::AT_PAGESZ, 4096);
    auto rand_addr = alloc(16);
    for(auto i = 0; i < 16; i++) {
        hs.memimg.byte(rand_addr + i) = uint8_t(rand());
    }
    auxvec.insert_or_assign(AUXVecType::AT_RANDOM, rand_addr);
    auxvec.insert_or_assign(AUXVecType::AT_NULL, 0);

    auto argc = argv.size();
    auto envpc = envp.size();
    auto auxvecc = auxvec.size() * 2; // each pair is encoded as two 64 bit ints

    // setup size in number of bytes
    // len(auxvec) + len(envp) + NULL + len(argv) + NULL + space for argc
    auto stack_setup_size = (auxvecc + envpc + 1 + argc + 1 + 1) * 8;

    // align to 2**7=128
    auto stack_setup_size_aligned = alignup(stack_setup_size, 7);
    // sp is where main enters, MUST BE ALIGNED
    sp = sp - stack_setup_size_aligned;

    auto stack_idx = 0;

    // write argc first
    hs.memimg.word(sp + 8 * (stack_idx++)) = argc;
    // write argv
    for(auto s : argv) {
        auto slen = s.size();
        auto addr = alloc(slen + 1); // alloc space for null byte
        copyToHart((void*)s.c_str(), addr, slen);
        hs.memimg.doubleword(sp + 8 * (stack_idx++)) = addr;
    }
    // write NULL
    hs.memimg.doubleword(sp + 8 * (stack_idx++)) = 0;

    // write ENV
    for(auto [key, value] : envp) {
        auto s = key + "=" + value;
        auto slen = s.size();
        auto addr = alloc(slen + 1); // alloc space for null byte
        copyToHart((void*)s.c_str(), addr, slen);
        hs.memimg.doubleword(sp + 8 * (stack_idx++)) = addr;
    }
    // write NULL
    hs.memimg.doubleword(sp + 8 * (stack_idx++)) = 0;

    // write auxvec
    for(auto [key, value] : auxvec) {
        hs.memimg.doubleword(sp + 8 * (stack_idx++)) = uint64_t(key);
        hs.memimg.doubleword(sp + 8 * (stack_idx++)) = value;
    }

    // set gpr for sp
    hs.rf.GPR[2] = sp;
}

void Hart::init(
    std::vector<std::string> argv,
    common::ordered_map<std::string, std::string> envp) {
    init_heap();
    init_stack(argv, envp);
}

void Hart::execute(uint64_t start_address) {
    hs.pc = start_address;
    while(1) {
        try {
            event_before_execute(hs);
            auto inst = hs.getInstWord();
            isa::inst::executeInstruction(inst, hs);
            event_after_execute(hs);

            if(shouldHalt()) break;
        } catch(const std::exception& e) {
            std::cerr << "Exception Occurred: " << e.what() << std::endl;
            break;
        }
    }
}

} // namespace cpu
