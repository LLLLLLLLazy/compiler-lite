
// Generated from MiniC.g4 by ANTLR 4.12.0

#pragma once


#include "antlr4-runtime.h"




class  MiniCLexer : public antlr4::Lexer {
public:
  enum {
    T_L_PAREN = 1, T_R_PAREN = 2, T_L_BRACK = 3, T_R_BRACK = 4, T_SEMICOLON = 5, 
    T_L_BRACE = 6, T_R_BRACE = 7, T_COMMA = 8, T_ASSIGN = 9, T_EQ = 10, 
    T_NE = 11, T_LE = 12, T_GE = 13, T_LT = 14, T_GT = 15, T_ADD = 16, T_SUB = 17, 
    T_MUL = 18, T_DIV = 19, T_MOD = 20, T_NOT = 21, T_LAND = 22, T_LOR = 23, 
    T_INC = 24, T_DEC = 25, T_IF = 26, T_ELSE = 27, T_WHILE = 28, T_FOR = 29, 
    T_BREAK = 30, T_CONTINUE = 31, T_RETURN = 32, T_CONST = 33, T_STATIC = 34, 
    T_INT = 35, T_FLOAT = 36, T_VOID = 37, T_ID = 38, T_STRING_LITERAL = 39, 
    T_FLOAT_LITERAL = 40, T_DIGIT = 41, LINE_COMMENT = 42, BLOCK_COMMENT = 43, 
    WS = 44
  };

  explicit MiniCLexer(antlr4::CharStream *input);

  ~MiniCLexer() override;


  std::string getGrammarFileName() const override;

  const std::vector<std::string>& getRuleNames() const override;

  const std::vector<std::string>& getChannelNames() const override;

  const std::vector<std::string>& getModeNames() const override;

  const antlr4::dfa::Vocabulary& getVocabulary() const override;

  antlr4::atn::SerializedATNView getSerializedATN() const override;

  const antlr4::atn::ATN& getATN() const override;

  // By default the static state used to implement the lexer is lazily initialized during the first
  // call to the constructor. You can call this function if you wish to initialize the static state
  // ahead of time.
  static void initialize();

private:

  // Individual action functions triggered by action() above.

  // Individual semantic predicate functions triggered by sempred() above.

};

