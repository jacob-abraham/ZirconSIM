#include "types.h"


namespace types {

Address strToAddress(const std::string& s) { return std::stoull(s); }
RegisterIndex strToRegisterIndex(const std::string& s) { return std::stoull(s); }
Integer strToInteger(const std::string& s) { return std::stoull(s); }
SignedInteger strToSignedInteger(const std::string& s) { return std::stoll(s); }

}
