#include "parser.h"

#include "common/debug.h"
#include "event/event.h"

#include <utility>

#include "expr_parser.h"

namespace ishell {
namespace parser {

Token Parser::expect(TokenType tt) {
    auto t = lexer.getToken();
    if(t.token_type == tt) {
        return t;
    }
    throw ParseException(
        "Expected '" + std::string(tt) + "' got '" + std::string(t.token_type) +
        "'");
}
Parser::CommandPtr Parser::parse(std::string input) {
    common::debug::log(common::debug::DebugType::PARSER, "parse()\n");
    lexer = Lexer(input);
    auto c = parse_command();
    expect(TokenType::END_OF_FILE);
    return c;
}
std::vector<Parser::CommandPtr> Parser::parseAll(std::string input) {
    common::debug::log(common::debug::DebugType::PARSER, "parseAll()\n");
    lexer = Lexer(input);
    std::vector<CommandPtr> c_list;
    while(lexer.peek().token_type != TokenType::END_OF_FILE) {
        auto c = parse_command();
        c_list.push_back(c);
    }
    expect(TokenType::END_OF_FILE);
    return c_list;
}

Parser::CommandPtr Parser::parse_command() {
    common::debug::log(common::debug::DebugType::PARSER, "parse_command()\n");
    auto actions = parse_action_list(TokenType::SEMICOLON);
    auto condition = parse_if_statement();
    auto event_list = parse_on_statement();

    actions = make_action_group(actions);

    return std::make_shared<command::CommandBase>(
        actions,
        condition,
        event_list);
}
// }
command::ExprPtr Parser::parse_if_statement() {
    common::debug::log(
        common::debug::DebugType::PARSER,
        "parse_if_statement()\n");
    if(lexer.peek().token_type == TokenType::IF) {
        expect(TokenType::IF);
        auto cond_expr = parse_expr();
        return cond_expr;
    }
    return nullptr;
}
std::vector<event::EventType> Parser::parse_on_statement() {
    common::debug::log(
        common::debug::DebugType::PARSER,
        "parse_on_statement()\n");
    if(lexer.peek().token_type == TokenType::ON) {
        expect(TokenType::ON);
        return parse_event_list();
    }
    return {};
}

std::vector<event::EventType> Parser::parse_event_list() {
    common::debug::log(
        common::debug::DebugType::PARSER,
        "parse_event_list()\n");
    auto event = parse_event();
    if(lexer.peek().token_type == TokenType::SEMICOLON) {
        expect(TokenType::SEMICOLON);
        auto event_list = parse_event_list();
        event_list.insert(event_list.begin(), event);
        return event_list;
    } else return {event};
}
event::EventType Parser::parse_event() {
    common::debug::log(common::debug::DebugType::PARSER, "parse_event()\n");
    auto sub_lexeme = expect(TokenType::SUBSYSTEM).lexeme;
    expect(TokenType::COLON);
    auto ev_lexeme = expect(TokenType::EVENT).lexeme;
    auto sub_ev = event::getEventType(sub_lexeme + "_" + ev_lexeme);
    if(sub_ev == event::EventType::NONE)
        throw ParseException("Unknown event: " + sub_lexeme + ":" + ev_lexeme);
    return sub_ev;
}

Parser::ActionPtrList Parser::parse_action_list(TokenType SEP) {
    common::debug::log(
        common::debug::DebugType::PARSER,
        "parse_action_list()\n");
    auto action = parse_action();
    if(lexer.peek().token_type == SEP) {
        expect(SEP);
        auto action_list = parse_action_list(SEP);
        action_list.insert(action_list.begin(), action);
        return action_list;
    } else return {action};
}

action::ActionPtr Parser::parse_action() {
    common::debug::log(common::debug::DebugType::PARSER, "parse_action()\n");
    if(lexer.peek().token_type == TokenType::STOP) {
        expect(TokenType::STOP);
        return std::make_shared<action::Stop>();
    } else if(lexer.peek().token_type == TokenType::PAUSE) {
        expect(TokenType::PAUSE);
        return std::make_shared<action::Pause>();
    } else if(lexer.peek().token_type == TokenType::RESUME) {
        expect(TokenType::RESUME);
        return std::make_shared<action::Resume>();
    } else if(lexer.peek().token_type == TokenType::DISASM) {
        expect(TokenType::DISASM);
        auto expr = parse_expr();
        return std::make_shared<action::Disasm>(expr);
    } else if(lexer.peek().token_type == TokenType::DUMP) {
        expect(TokenType::DUMP);
        auto dump_args = parse_dump_arg_list();
        return std::make_shared<action::Dump>(
            dump_args.begin(),
            dump_args.end());
    } else if(lexer.peek().token_type == TokenType::WATCH) {
        expect(TokenType::WATCH);
        auto [watch_expr, watch_args] = parse_watch_args();
        return std::make_shared<action::Watch>(
            watch_expr,
            watch_args.begin(),
            watch_args.end());
    } else throw ParseException("Unknown action: " + lexer.peek().getString());
}

action::Dump::DumpArg Parser::parse_dump_arg() {
    common::debug::log(common::debug::DebugType::PARSER, "parse_dump_arg()\n");
    if(lexer.peek().token_type == TokenType::STRING) {
        auto s_tok = expect(TokenType::STRING);
        return s_tok.lexeme;
    } else {
        auto e = parse_expr();
        return e;
    }
}
std::vector<action::Dump::DumpArg> Parser::parse_dump_arg_list() {
    common::debug::log(
        common::debug::DebugType::PARSER,
        "parse_dump_arg_list()\n");
    auto arg = parse_dump_arg();
    if(lexer.peek().token_type == TokenType::COMMA) {
        expect(TokenType::COMMA);
        auto arg_list = parse_dump_arg_list();
        arg_list.insert(arg_list.begin(), arg);
        return arg_list;
    } else return {arg};
}

std::pair<command::ExprPtr, Parser::ActionPtrList> Parser::parse_watch_args() {
    common::debug::log(
        common::debug::DebugType::PARSER,
        "parse_watch_args()\n");
    auto e = parse_lvalue_expr();
    auto actions = parse_action_list(TokenType::COMMA);
    return {e, actions};
}

command::ExprPtr Parser::parse_lvalue_expr() {
    common::debug::log(
        common::debug::DebugType::PARSER,
        "parse_lvalue_expr()\n");
    auto e = parse_expr();
    if(!e->isLValue())
        throw ParseException("'" + e->getString() + "' is not an lvalue");
    return e;
}

command::ExprPtr Parser::parse_expr() {
    common::debug::log(common::debug::DebugType::PARSER, "parse_expr()\n");
    std::vector<Token> input;
    while(ExprParser::isExprToken(lexer.peek())) {
        input.push_back(lexer.getToken());
    }
    ExprParser ep(input);
    auto expr = ep.parse();
    return expr;
}

Parser::ActionPtrList Parser::make_action_group(const ActionPtrList& actions) {
    // only make an action group if there is more than 1 action
    if(actions.size() > 1) {
        auto action_group = std::make_shared<action::ActionGroup>(actions);
        return {action_group};
    } else return actions;
}

} // namespace parser
} // namespace ishell
