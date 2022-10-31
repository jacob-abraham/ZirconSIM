
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

#include "event/event.h"

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
            T v = read();
            rc->event_read(rc->classname, reg_idx, v);
            return v;
        }
        RegisterProxy& operator=(T v) {
            T old_value = read();
            write(v);
            rc->event_write(rc->classname, reg_idx, v, old_value);
            return *this;
        }
    };

    // Subsystem: reg_$CLASS
    // Description: Fires when a register is read
    // Parameters: (register class, register index, value read)
    event::Event<std::string, uint64_t, uint64_t> event_read;
    // Subsystem: reg_$CLASS
    // Description: Fires when a register is written
    // Parameters: (register class, register index, value written, old value)
    event::Event<std::string, uint64_t, uint64_t, uint64_t> event_write;

  public:
    RegisterClass(
        std::string classname,
        std::string regprefix,
        std::array<Register<SIZE>, NUM> registers = {})
        : classname(classname), regprefix(regprefix), registers(registers) {}
    RegisterClass() = default;

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

    void addReadListener(decltype(event_read)::callback_type func) {
        event_read.addListener(func);
    }
    void addWriteListener(decltype(event_write)::callback_type func) {
        event_write.addListener(func);
    }
};

#endif
