
#ifndef ZIRCON_CPU_REGISTER_H_
#define ZIRCON_CPU_REGISTER_H_

#include <array>
#include <bitset>
#include <cassert>
#include <cstdint>
#include <iomanip>
#include <ostream>
#include <sstream>
#include <string>

#include "trace/trace.h"

template <size_t SIZE> class Register {
  private:
    std::bitset<SIZE> value;
    std::string name;
    bool rd_only;

  public:
    Register(std::string name = "", bool rd_only = false)
        : value(0), name(name), rd_only(rd_only) {}

    Register(
        std::bitset<SIZE> bits,
        std::string name = "",
        bool rd_only = false)
        : value(bits), name(name), rd_only(rd_only) {}

    Register(uint64_t value, std::string name = "", bool rd_only = false)
        : value(value), name(name), rd_only(rd_only) {}

    void set(const uint64_t& value) {
        if(!rd_only) this->value = value;
    }
    void set(const std::bitset<SIZE>& value) {
        if(!rd_only) this->value = value;
    }
    Register<SIZE>& operator=(const uint64_t& value) {
        this->set(value);
        return *this;
    }

    std::bitset<SIZE> getBits() { return value; }
    uint64_t get() const {
        assert(SIZE <= 64);
        return value.to_ullong();
    }
    operator uint64_t() const { return get(); }

    const std::string& getName() { return name; }
    void dump(std::ostream& o) {
        o << getName() << " 0x" << std::setfill('0') << std::setw(16)
          << std::right << std::hex << value;
    }
};

template <size_t NUM, size_t SIZE> class RegisterClass {
  private:
    std::string classname;
    std::string regprefix;
    std::array<Register<SIZE>, NUM> registers;
    Trace trace_reg;

    struct RegisterProxy {
        using T = uint64_t;

      private:
        RegisterClass<NUM, SIZE>* rc;
        unsigned reg_idx;
        friend RegisterClass;

      public:
        T read() { return rc->registers[reg_idx].get(); }
        void write(T v) { rc->registers[reg_idx].set(v); }
        RegisterProxy(RegisterClass* rc, unsigned reg_idx)
            : rc(rc), reg_idx(reg_idx) {}
        operator T() {
            rc->trace_reg << "RD " << rc->classname << "[" << Trace::dec
                          << reg_idx << "]" << Trace::flush;
            T v = read();
            rc->trace_reg << " = " << Trace::doubleword << v << std::endl;
            return v;
        }
        RegisterProxy& operator=(T v) {
            rc->trace_reg << "WR " << rc->classname << "[" << Trace::dec
                          << reg_idx << "]" << Trace::flush;
            T old_value = read();
            write(v);
            rc->trace_reg << " = " << Trace::doubleword << v
                          << "; OLD VALUE = " << Trace::doubleword << old_value
                          << std::endl;
            return *this;
        }
    };

  public:
    RegisterClass(
        std::string classname,
        std::string regprefix,
        std::array<Register<SIZE>, NUM> registers = {},
        TraceMode tm = TraceMode::NONE)
        : classname(classname), regprefix(regprefix), registers(registers),
          trace_reg("REGISTER TRACE: " + classname) {
        trace_reg.setState(tm & TraceMode::REGISTER);
    }
    RegisterClass() = default;
    void setTraceMode(TraceMode tm) {
        trace_reg.setState(tm & TraceMode::REGISTER);
    }

    RegisterProxy reg(unsigned idx) {
        assert(idx < NUM);
        return RegisterProxy(this, idx);
    }
    RegisterProxy operator[](unsigned idx) { return reg(idx); }

    const std::string& getName() { return classname; }
    void dump(std::ostream& o) {
        o << getName();
        auto half = NUM / 2;
        for(size_t i = 0; i < half; i++) {
            o << "\n";
            o << std::setfill(' ') << std::setw(32);
            reg(i).dump(o);
            o << std::setfill(' ') << std::setw(32);
            reg(half + i).dump(o);
        }
    }
};

#endif