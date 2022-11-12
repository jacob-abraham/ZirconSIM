#ifndef ZIRCON_CONTROLLER_PARSER_PARSER_H_
#define ZIRCON_CONTROLLER_PARSER_PARSER_H_

#include "controller/command.h"
#include "cpu/isa/rf.h"
#include "lexer.h"
#include <optional>
#include <ostream>
#include <stdexcept>
#include <type_traits>

namespace controller {

namespace parser {
struct ParseException : public std::runtime_error {
    ParseException(std::string message)
        : std::runtime_error("Parse Exception: " + message) {}
};

class Parser {
  public:
    Lexer lexer;
    std::ostream* log;
    Parser(std::vector<std::string> input, std::ostream& log = std::cout)
        : lexer(input), log(&log) {}
    CommandList parse();

    /*
    controls          -> control | control controls
    control           -> event_action | event_cond_action
    event_action      -> event action_list
    event_cond_action -> event cond_list action_list
    event             -> SUBSYSTEM COLON EVENT
    action_list       -> action | action COMMA action_list
    action            -> REGISTER_CLASS | register | PC | mem | STOP
    cond_list         -> cond | cond COMMA cond_list
    cond              -> register EQUALS NUM | mem EQUALS NUM | PC EQUALS NUM
    register          -> REGISTER_CLASS LBRACK NUM RBRACK
    mem               -> MEM LBRACK NUM RBRACK
    */

  private:
    Token expect(TokenType tt);

    std::vector<std::shared_ptr<Command>> parse_controls();
    std::shared_ptr<Command> parse_control();
    std::shared_ptr<Command> parse_event_action();
    std::shared_ptr<ConditionalCommand> parse_event_cond_action();
    event::EventType parse_event();
    std::vector<std::shared_ptr<action::ActionInterface>> parse_action_list();
    std::shared_ptr<action::ActionInterface> parse_action();
    std::vector<std::shared_ptr<condition::ConditionInterface>>
    parse_cond_list();
    std::shared_ptr<condition::ConditionInterface> parse_cond();
    std::pair<isa::rf::RegisterClassType, RegisterIndex> parse_register();
    Address parse_mem();
};

} // namespace parser
} // namespace controller

#endif