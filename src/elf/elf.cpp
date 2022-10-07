#include "elf.h"

ssize_t elf::parseFileHeader(std::ifstream& ifs, FileHeader& fh) {
    // char buffer[24];
    ifs.read((char*)&fh, sizeof(fh));
    if(ifs.fail()) return -1;
    if(!validateFileHeader(fh)) return -1;
    return sizeof(fh);
}
bool elf::validateFileHeader(FileHeader& fh) {
    // check magic header
    char magic[] = {0x7F, 'E', 'L', 'F'};
    if(std::memcmp(fh.e_ident.EI_MAG, magic, 4) != 0) {
        return false;
    }
    // check we are 64 bit elf
    if(fh.e_ident.EI_CLASS != 2) {
        return false;
    }
    // check we are little endian
    if(fh.e_ident.EI_DATA != 1) {
        return false;
    }
    // check version
    if(fh.e_ident.EI_VERSION != 1) {
        return false;
    }
    // // check ABI, only allow SystemV
    // if(fh.e_ident.EI_OSABI != 0x00) {
    //     return false;
    // }
    // check ELF is RISC-V
    if(fh.e_machine != 0xF3) {
        return false;
    }
    // check version
    if(fh.e_version != 1) {
        return false;
    }
    // check version
    if(fh.e_version != 1) {
        return false;
    }

    // if there are program headers, must be of proper size
    if(fh.e_phoff != 0 && fh.e_phentsize != sizeof(ProgramHeader)) return false;
    // if no program headers, phnum must be 0
    if(fh.e_phoff == 0 && fh.e_phnum != 0) return false;

    // if there are section headers, must be of proper size
    if(fh.e_shoff != 0 && fh.e_shentsize != sizeof(SectionHeader)) return false;
    // if no section headers, shnum must be 0
    if(fh.e_shoff == 0 && fh.e_shnum != 0) return false;

    return true;
}

ssize_t elf::parseProgramHeader(std::ifstream& ifs, ProgramHeader& ph) {
    ifs.read((char*)&ph, sizeof(ph));
    if(ifs.fail()) return -1;
    if(!validateProgramHeader(ph)) return -1;
    return sizeof(ph);
}
bool elf::validateProgramHeader(ProgramHeader&) { return true; }

ssize_t elf::parseSectionHeader(std::ifstream& ifs, SectionHeader& sh) {
    ifs.read((char*)&sh, sizeof(sh));
    if(ifs.fail()) return -1;
    if(!validateSectionHeader(sh)) return -1;
    return sizeof(sh);
}
bool elf::validateSectionHeader(SectionHeader&) { return true; }

bool elf::File::parse() {
    ifs.seekg(0);
    if(parseFileHeader(ifs, this->fh) == -1) return false;
    // read the program header.
    if(this->fh.e_phoff) {
        ifs.seekg(this->fh.e_phoff);
        for(size_t i = 0; i < this->fh.e_phnum; i++) {
            ProgramHeader ph;
            if(parseProgramHeader(ifs, ph) == -1) return false;
            this->phs.push_back(ph);
        }
    }
    if(this->fh.e_shoff) {
        ifs.seekg(this->fh.e_shoff);
        for(size_t i = 0; i < this->fh.e_shnum; i++) {
            SectionHeader sh;
            if(parseSectionHeader(ifs, sh) == -1) return false;
            this->shs.push_back(sh);
        }
    }
    return true;
}

std::string elf::File::getSectionHeaderString(uint64_t offset) {
    auto shstrtab = shs[fh.e_shstrndx];
    ifs.seekg(shstrtab.sh_offset + offset);
    char buffer[64];
    ifs.read(buffer, 64);
    return std::string(buffer);
}

uint64_t elf::File::getStartAddress() { return fh.e_entry; }

void elf::File::buildMemoryImage(mem::MemoryImage& m) {
    // for each loadable segment
    for(auto ph : phs) {
        if(ph.p_type == 1 /*PT_LOAD*/ && ph.p_memsz) {
            m.allocate(ph.p_vaddr, ph.p_memsz);
            // find all sections that exist in this segment and load them into
            // memory
            for(auto sh : shs) {
                if(sh.sh_type == 0 /*SHT_NULL*/) continue;
                // if entire section fits in segment
                // OR section offset is in segment and type is NOBITS
                if((sh.sh_offset >= ph.p_offset &&
                    sh.sh_offset + sh.sh_size <= ph.p_offset + ph.p_filesz) ||
                   (sh.sh_type == 0x8 /*SHT_NOBITS*/ &&
                    sh.sh_offset >= ph.p_offset &&
                    sh.sh_offset <= ph.p_offset + ph.p_filesz)) {
                    // if NOBITS, set to 0
                    if(sh.sh_type == 0x8 /*SHT_NOBITS*/) {
                        std::memset(m.get(sh.sh_addr), 0, sh.sh_size);
                    }
                    // otherwise copy section to memory
                    else {
                        ifs.seekg(sh.sh_offset);
                        ifs.read((char*)m.get(sh.sh_addr), sh.sh_size);
                    }
                }
            }
        }
    }
}
