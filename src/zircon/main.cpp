

#include "color/color.h"
#include "common/argparse.hpp"
#include "common/format.h"
#include "controller/parser/parser.h"
#include "cpu/cpu.h"
#include "cpu/isa/inst.h"
#include "elf/elf.h"
#include "event/event.h"
#include "mem/memory-image.h"
#include "trace/stats.h"
#include <algorithm>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <sys/stat.h>
#include <unordered_map>

std::ostream* getFileStreamIfTrue(
    bool cond,
    std::optional<std::string> fname,
    std::ostream& alternative) {
    static std::unordered_map<std::string, std::ostream*> file_buffer;
    auto inst_log = &alternative;
    if(cond && fname) {
        // compare existing stats, if file already exists in buffer use that
        // ostream
        struct stat fname_info;
        if(stat(fname->c_str(), &fname_info) != 0) return nullptr;
        auto it = std::find_if(
            file_buffer.begin(),
            file_buffer.end(),
            [fname_info](auto file_entry) {
                struct stat file_entry_info;
                if(stat(file_entry.first.c_str(), &file_entry_info) == 0) {
                    return fname_info.st_dev == file_entry_info.st_dev &&
                           fname_info.st_ino == file_entry_info.st_ino;
                }
                return false;
            });
        if(it != file_buffer.end()) {
            // return old file handle
            inst_log = it->second;
        } else {
            // open new file handle
            auto handle = (new std::ofstream(*fname));
            file_buffer[*fname] = handle;
            inst_log = handle;
        }
    }
    return inst_log;
}

auto stringsplit(std::string s, std::string delim = ",") {
    std::vector<std::string> tokens;
    size_t pos = 0;
    std::string token;
    while((pos = s.find(delim)) != std::string::npos) {
        token = s.substr(0, pos);
        tokens.push_back(token);
        s.erase(0, pos + delim.length());
    }
    tokens.push_back(s);
    return tokens;
}

int main(int argc, const char** argv) {

    argparse::ArgumentParser program_args(
        {},
        {},
        argparse::default_arguments::help);

    program_args.add_argument("file").help("elf64 file execute read from");

    program_args.add_argument("-c", "--color")
        .default_value(false)
        .implicit_value(true)
        .help("colorize output");

    program_args.add_argument("-I", "--inst")
        .default_value(false)
        .implicit_value(true)
        .help("trace instructions");
    program_args.add_argument("--inst-log")
        .metavar("LOGFILE")
        .help("instructions log file");

    program_args.add_argument("-R", "--reg")
        .default_value(false)
        .implicit_value(true)
        .help("trace register accesses");
    program_args.add_argument("--reg-log")
        .metavar("LOGFILE")
        .help("register accesses log file");

    program_args.add_argument("-M", "--mem")
        .default_value(false)
        .implicit_value(true)
        .help("trace memory accesses");
    program_args.add_argument("--mem-log")
        .metavar("LOGFILE")
        .help("memory accesses log file");

    program_args.add_argument("-S", "--stats")
        .default_value(false)
        .implicit_value(true)
        .help("dump runtime statistics");

    program_args.add_argument("-control")
        .nargs(2, 3)
        .append()
        .metavar("CONTROL")
        .help("a control sequence to apply");

    // everything after -- gets shoved into the remaining args
    std::vector<std::string> remaining_args;
    int newargc = argc;
    for(int i = 0; i < argc; i++) {
        if(argv[i][0] == '-' && argv[i][1] == '-' && argv[i][2] == '\0') {
            newargc = i;
            for(i = i + 1; i < argc; i++) {
                remaining_args.push_back(argv[i]);
            }
            break;
        }
    }

    try {
        program_args.parse_args(newargc, argv);
    } catch(const std::runtime_error& err) {
        std::cerr << err.what() << std::endl;
        std::cerr << program_args;
        return 1;
    }

    std::string filename = program_args.get<std::string>("file");
    std::ifstream is(filename, std::ios::binary);
    if(!is) {
        std::cerr << "Failed to open '" << filename << "'" << std::endl;
        return 1;
    }

    auto inst_log = getFileStreamIfTrue(
        program_args.get<bool>("--inst") && program_args.present("--inst-log"),
        program_args.present<std::string>("--inst-log"),
        std::cout);
    if(!inst_log) {
        std::cerr << "Failed to open '"
                  << *program_args.present<std::string>("--inst-log") << "'"
                  << std::endl;
        return 1;
    }

    auto mem_log = getFileStreamIfTrue(
        program_args.get<bool>("--mem") && program_args.present("--mem-log"),
        program_args.present<std::string>("--mem-log"),
        std::cout);
    if(!mem_log) {
        std::cerr << "Failed to open '"
                  << *program_args.present<std::string>("--mem-log") << "'"
                  << std::endl;
        return 1;
    }

    auto reg_log = getFileStreamIfTrue(
        program_args.get<bool>("--reg") && program_args.present("--reg-log"),
        program_args.present<std::string>("--reg-log"),
        std::cout);
    if(!reg_log) {
        std::cerr << "Failed to open '"
                  << *program_args.present<std::string>("--reg-log") << "'"
                  << std::endl;
        return 1;
    }

    elf::File f(std::move(is));
    mem::MemoryImage memimg(0x800000);
    f.buildMemoryImage(memimg);
    auto start = f.getStartAddress();
    bool useColor = program_args.get<bool>("--color");
    cpu::Hart hart(memimg);

    auto colorAddr = [useColor]() {
        return useColor ? color::getColor(
                              {color::ColorCode::LIGHT_CYAN,
                               color::ColorCode::FAINT})
                        : "";
    };
    auto colorHex = [useColor]() {
        return useColor ? color::getColor(
                              {color::ColorCode::CYAN, color::ColorCode::FAINT})
                        : "";
    };
    auto colorNew = [useColor]() {
        return useColor
                   ? color::getColor(
                         {color::ColorCode::GREEN, color::ColorCode::FAINT})
                   : "";
    };
    auto colorOld = [useColor]() {
        return useColor ? color::getColor(
                              {color::ColorCode::RED, color::ColorCode::FAINT})
                        : "";
    };
    auto colorReset = [useColor]() {
        return useColor ? color::getReset() : "";
    };

    if(program_args.get<bool>("--inst")) {
        hart.addBeforeExecuteListener(
            [inst_log, useColor, colorReset, colorHex, colorAddr](
                cpu::HartState& hs) {
                auto inst = hs.getInstWord();
                *inst_log << "PC[" << colorAddr() << common::Format::doubleword
                          << hs.pc << colorReset() << "] = " << colorHex()
                          << common::Format::word << inst << colorReset()
                          << "; "
                          << isa::inst::disassemble(inst, hs.pc, useColor)
                          << std::endl;
            });
    }

    if(program_args.get<bool>("--reg")) {
        hart.addRegisterReadListener([reg_log, colorReset, colorAddr, colorNew](
                                         std::string classname,
                                         uint64_t idx,
                                         uint64_t value) {
            *reg_log << "RD " << classname << "[" << colorAddr()
                     << common::Format::dec << idx << colorReset()
                     << "] = " << colorNew() << common::Format::doubleword
                     << (uint64_t)value << colorReset() << std::endl;
        });
        hart.addRegisterWriteListener(
            [reg_log, colorReset, colorAddr, colorNew, colorOld](
                std::string classname,
                uint64_t idx,
                uint64_t value,
                uint64_t oldvalue) {
                *reg_log << "WR " << classname << "[" << colorAddr()
                         << common::Format::dec << idx << colorReset()
                         << "] = " << colorNew() << common::Format::doubleword
                         << (uint64_t)value << colorReset()
                         << "; OLD VALUE = " << colorOld()
                         << common::Format::doubleword << oldvalue
                         << colorReset() << std::endl;
            });
    }
    if(program_args.get<bool>("--mem")) {
        memimg.addAllocationListener(
            [mem_log, colorReset, colorAddr](uint64_t addr, uint64_t size) {
                *mem_log << "ALLOCATE[" << colorAddr()
                         << common::Format::doubleword << addr << colorReset()
                         << " - " << colorAddr() << common::Format::doubleword
                         << addr + size << colorReset() << "]" << std::endl;
            });

        memimg.addReadListener([mem_log, colorReset, colorAddr, colorNew](
                                   uint64_t addr,
                                   uint64_t value,
                                   size_t size) {
            *mem_log << "RD MEM[" << colorAddr() << common::Format::doubleword
                     << addr << colorReset() << "] = " << colorNew()
                     << common::Format::hexnum(size) << (uint64_t)value
                     << colorReset() << std::endl;
        });
        memimg.addWriteListener(
            [mem_log, colorReset, colorAddr, colorNew, colorOld](
                uint64_t addr,
                uint64_t value,
                uint64_t oldvalue,
                size_t size) {
                *mem_log << "WR MEM[" << colorAddr()
                         << common::Format::doubleword << addr << colorReset()
                         << "] = " << colorNew() << common::Format::hexnum(size)
                         << (uint64_t)value << colorReset()
                         << "; OLD VALUE = " << colorOld()
                         << common::Format::hexnum(size) << oldvalue
                         << colorReset() << std::endl;
            });
    }
    Stats stats;
    if(program_args.get<bool>("--stats")) {
        hart.addBeforeExecuteListener(
            [&stats](cpu::HartState& hs) { stats.count(hs); });
    }

    // add commandline args
    auto control_args = program_args.get<std::vector<std::string>>("-control");
    if(!control_args.empty()) {
        auto parser = controller::parser::Parser(control_args);
        auto parsed_commands = parser.parse();
        // i have to cast anyways to get this, might be better to remove the
        // constructor part and just makie it a function call with no
        // virtuallyness
        for(auto a : parsed_commands.allActions()) {
            if(a->isa<controller::action::DumpPC>()) {
                a->cast<controller::action::DumpPC>()->hs = &hart.hs;
            }
            if(a->isa<controller::action::DumpRegisterClass>()) {
                a->cast<controller::action::DumpRegisterClass>()->hs = &hart.hs;
            }
        }
        for(auto c : parsed_commands.allConditions()) {
            if(c->isa<controller::condition::PCEquals>()) {
                c->cast<controller::condition::PCEquals>()->hs = &hart.hs;
            }
        }
        for(auto c : parsed_commands.commands) {
            switch(c->getEventType()) {
                case event::EventType::HART_AFTER_EXECUTE:
                    hart.addAfterExecuteListener(
                        [c](cpu::HartState& hs) { c->doit(&std::cout); });
                    break;
                case event::EventType::HART_BEFORE_EXECUTE:
                    hart.addBeforeExecuteListener(
                        [c](cpu::HartState& hs) { c->doit(&std::cout); });
                    break;
                case event::EventType::MEM_READ:
                    hart.hs.memimg.addReadListener(
                        [c](uint64_t, uint64_t, size_t) {
                            c->doit(&std::cout);
                        });
                    break;
                case event::EventType::MEM_WRITE:
                    hart.hs.memimg.addWriteListener(
                        [c](uint64_t, uint64_t, uint64_t, size_t) {
                            c->doit(&std::cout);
                        });
                    break;
                case event::EventType::MEM_ALLOCATION:
                    hart.hs.memimg.addAllocationListener(
                        [c](uint64_t, uint64_t) { c->doit(&std::cout); });
                    break;
                case event::EventType::REG_READ:
                    hart.hs.rf.addReadListener(
                        [c](std::string, uint64_t, uint64_t) {
                            c->doit(&std::cout);
                        });
                    break;
                case event::EventType::REG_WRITE:
                    hart.hs.rf.addWriteListener(
                        [c](std::string, uint64_t, uint64_t, uint64_t) {
                            c->doit(&std::cout);
                        });
                    break;
                default: std::cerr << "No Event Handler Defined\n";
            }
        }
        // for(auto c : parsed) {

        // }
    }
    // size_t control_idx = 0;

    // while(control_idx < control_args.size()) {
    //     std::string event_str = control_args[control_idx];
    //     control_idx++;
    //     auto event = event::parseEventName(event_str);
    //     switch(event) {
    //         case event::EventType::INST_AFTER_EXECUTE: {
    //             // check if we have 2 more args or 1 more arg
    //             std::string actions = control_idx < control_args.size()
    //                                       ? control_args[control_idx]
    //                                       : "";
    //             std::string cond = control_idx + 1 < control_args.size()
    //                                    ? control_args[control_idx + 1]
    //                                    : "";

    //             if(actions != "" && cond != "") {
    //                 control_idx += 2;
    //                 std::cerr << "unimp\n";
    //             } else if(actions != "" && cond == "") {
    //                 // one arg, actions only
    //                 control_idx++;
    //                 auto action_tokens = stringsplit(actions, ",");
    //                 std::vector<event::ActionType> action_types;
    //                 std::transform(
    //                     action_tokens.cbegin(),
    //                     action_tokens.cend(),
    //                     std::back_inserter(action_types),
    //                     [](auto t) { return event::parseActionName(t); });
    //                 hart.addAfterExecuteListener(
    //                     [action_types](cpu::HartState& hs) {
    //                         for(auto a : action_types) {
    //                             switch(a) {
    //                                 case event::ActionType::DUMP_REGS:
    //                                     hs.rf.GPR.dump(std::cout);
    //                                     std::cout << "\n";
    //                                     break;
    //                                 default: break;
    //                             }
    //                         }
    //                     });

    //             } else {
    //                 std::cerr << "ERRRRRR\n";
    //             }

    //             break;
    //         }
    //         default: break;
    //     }
    // }

    hart.init();
    hart.execute(start);

    if(program_args.get<bool>("--stats")) {
        std::cout << stats.dump() << std::endl;
    }

    return 0;
}
