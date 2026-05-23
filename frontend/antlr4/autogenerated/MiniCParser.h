
// Generated from MiniC.g4 by ANTLR 4.12.0

#pragma once


#include "antlr4-runtime.h"




class  MiniCParser : public antlr4::Parser {
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

  enum {
    RuleCompileUnit = 0, RuleDecl = 1, RuleFuncDef = 2, RuleFuncType = 3, 
    RuleFormalParamList = 4, RuleFormalParam = 5, RuleFormalParamDims = 6, 
    RuleBlock = 7, RuleBlockItemList = 8, RuleBlockItem = 9, RuleConstDecl = 10, 
    RuleVarDecl = 11, RuleConstDeclNoSemi = 12, RuleVarDeclNoSemi = 13, 
    RuleConstDef = 14, RuleBasicType = 15, RuleVarDef = 16, RuleArrayDefDims = 17, 
    RuleInitVal = 18, RuleStatement = 19, RuleMatchedStatement = 20, RuleUnmatchedStatement = 21, 
    RuleForInit = 22, RuleForStep = 23, RuleExpr = 24, RuleCond = 25, RuleLOrExp = 26, 
    RuleLAndExp = 27, RuleEqExp = 28, RuleEqOp = 29, RuleRelExp = 30, RuleRelOp = 31, 
    RuleAddExp = 32, RuleAddOp = 33, RuleMulExp = 34, RuleMulOp = 35, RuleUnaryExp = 36, 
    RuleUnaryOp = 37, RulePrimaryExp = 38, RuleRealParam = 39, RuleRealParamList = 40, 
    RuleLVal = 41
  };

  explicit MiniCParser(antlr4::TokenStream *input);

  MiniCParser(antlr4::TokenStream *input, const antlr4::atn::ParserATNSimulatorOptions &options);

  ~MiniCParser() override;

  std::string getGrammarFileName() const override;

  const antlr4::atn::ATN& getATN() const override;

  const std::vector<std::string>& getRuleNames() const override;

  const antlr4::dfa::Vocabulary& getVocabulary() const override;

  antlr4::atn::SerializedATNView getSerializedATN() const override;


  class CompileUnitContext;
  class DeclContext;
  class FuncDefContext;
  class FuncTypeContext;
  class FormalParamListContext;
  class FormalParamContext;
  class FormalParamDimsContext;
  class BlockContext;
  class BlockItemListContext;
  class BlockItemContext;
  class ConstDeclContext;
  class VarDeclContext;
  class ConstDeclNoSemiContext;
  class VarDeclNoSemiContext;
  class ConstDefContext;
  class BasicTypeContext;
  class VarDefContext;
  class ArrayDefDimsContext;
  class InitValContext;
  class StatementContext;
  class MatchedStatementContext;
  class UnmatchedStatementContext;
  class ForInitContext;
  class ForStepContext;
  class ExprContext;
  class CondContext;
  class LOrExpContext;
  class LAndExpContext;
  class EqExpContext;
  class EqOpContext;
  class RelExpContext;
  class RelOpContext;
  class AddExpContext;
  class AddOpContext;
  class MulExpContext;
  class MulOpContext;
  class UnaryExpContext;
  class UnaryOpContext;
  class PrimaryExpContext;
  class RealParamContext;
  class RealParamListContext;
  class LValContext; 

  class  CompileUnitContext : public antlr4::ParserRuleContext {
  public:
    CompileUnitContext(antlr4::ParserRuleContext *parent, size_t invokingState);
    virtual size_t getRuleIndex() const override;
    antlr4::tree::TerminalNode *EOF();
    std::vector<FuncDefContext *> funcDef();
    FuncDefContext* funcDef(size_t i);
    std::vector<DeclContext *> decl();
    DeclContext* decl(size_t i);


    virtual std::any accept(antlr4::tree::ParseTreeVisitor *visitor) override;
   
  };

  CompileUnitContext* compileUnit();

  class  DeclContext : public antlr4::ParserRuleContext {
  public:
    DeclContext(antlr4::ParserRuleContext *parent, size_t invokingState);
    virtual size_t getRuleIndex() const override;
    ConstDeclContext *constDecl();
    VarDeclContext *varDecl();


    virtual std::any accept(antlr4::tree::ParseTreeVisitor *visitor) override;
   
  };

  DeclContext* decl();

  class  FuncDefContext : public antlr4::ParserRuleContext {
  public:
    FuncDefContext(antlr4::ParserRuleContext *parent, size_t invokingState);
    virtual size_t getRuleIndex() const override;
    FuncTypeContext *funcType();
    antlr4::tree::TerminalNode *T_ID();
    antlr4::tree::TerminalNode *T_L_PAREN();
    antlr4::tree::TerminalNode *T_R_PAREN();
    BlockContext *block();
    FormalParamListContext *formalParamList();


    virtual std::any accept(antlr4::tree::ParseTreeVisitor *visitor) override;
   
  };

  FuncDefContext* funcDef();

  class  FuncTypeContext : public antlr4::ParserRuleContext {
  public:
    FuncTypeContext(antlr4::ParserRuleContext *parent, size_t invokingState);
    virtual size_t getRuleIndex() const override;
    antlr4::tree::TerminalNode *T_INT();
    antlr4::tree::TerminalNode *T_FLOAT();
    antlr4::tree::TerminalNode *T_VOID();


    virtual std::any accept(antlr4::tree::ParseTreeVisitor *visitor) override;
   
  };

  FuncTypeContext* funcType();

  class  FormalParamListContext : public antlr4::ParserRuleContext {
  public:
    FormalParamListContext(antlr4::ParserRuleContext *parent, size_t invokingState);
    virtual size_t getRuleIndex() const override;
    std::vector<FormalParamContext *> formalParam();
    FormalParamContext* formalParam(size_t i);
    std::vector<antlr4::tree::TerminalNode *> T_COMMA();
    antlr4::tree::TerminalNode* T_COMMA(size_t i);


    virtual std::any accept(antlr4::tree::ParseTreeVisitor *visitor) override;
   
  };

  FormalParamListContext* formalParamList();

  class  FormalParamContext : public antlr4::ParserRuleContext {
  public:
    FormalParamContext(antlr4::ParserRuleContext *parent, size_t invokingState);
    virtual size_t getRuleIndex() const override;
    BasicTypeContext *basicType();
    antlr4::tree::TerminalNode *T_ID();
    FormalParamDimsContext *formalParamDims();


    virtual std::any accept(antlr4::tree::ParseTreeVisitor *visitor) override;
   
  };

  FormalParamContext* formalParam();

  class  FormalParamDimsContext : public antlr4::ParserRuleContext {
  public:
    FormalParamDimsContext(antlr4::ParserRuleContext *parent, size_t invokingState);
    virtual size_t getRuleIndex() const override;
    std::vector<antlr4::tree::TerminalNode *> T_L_BRACK();
    antlr4::tree::TerminalNode* T_L_BRACK(size_t i);
    std::vector<antlr4::tree::TerminalNode *> T_R_BRACK();
    antlr4::tree::TerminalNode* T_R_BRACK(size_t i);
    std::vector<ExprContext *> expr();
    ExprContext* expr(size_t i);


    virtual std::any accept(antlr4::tree::ParseTreeVisitor *visitor) override;
   
  };

  FormalParamDimsContext* formalParamDims();

  class  BlockContext : public antlr4::ParserRuleContext {
  public:
    BlockContext(antlr4::ParserRuleContext *parent, size_t invokingState);
    virtual size_t getRuleIndex() const override;
    antlr4::tree::TerminalNode *T_L_BRACE();
    antlr4::tree::TerminalNode *T_R_BRACE();
    BlockItemListContext *blockItemList();


    virtual std::any accept(antlr4::tree::ParseTreeVisitor *visitor) override;
   
  };

  BlockContext* block();

  class  BlockItemListContext : public antlr4::ParserRuleContext {
  public:
    BlockItemListContext(antlr4::ParserRuleContext *parent, size_t invokingState);
    virtual size_t getRuleIndex() const override;
    std::vector<BlockItemContext *> blockItem();
    BlockItemContext* blockItem(size_t i);


    virtual std::any accept(antlr4::tree::ParseTreeVisitor *visitor) override;
   
  };

  BlockItemListContext* blockItemList();

  class  BlockItemContext : public antlr4::ParserRuleContext {
  public:
    BlockItemContext(antlr4::ParserRuleContext *parent, size_t invokingState);
    virtual size_t getRuleIndex() const override;
    StatementContext *statement();
    DeclContext *decl();


    virtual std::any accept(antlr4::tree::ParseTreeVisitor *visitor) override;
   
  };

  BlockItemContext* blockItem();

  class  ConstDeclContext : public antlr4::ParserRuleContext {
  public:
    ConstDeclContext(antlr4::ParserRuleContext *parent, size_t invokingState);
    virtual size_t getRuleIndex() const override;
    ConstDeclNoSemiContext *constDeclNoSemi();
    antlr4::tree::TerminalNode *T_SEMICOLON();


    virtual std::any accept(antlr4::tree::ParseTreeVisitor *visitor) override;
   
  };

  ConstDeclContext* constDecl();

  class  VarDeclContext : public antlr4::ParserRuleContext {
  public:
    VarDeclContext(antlr4::ParserRuleContext *parent, size_t invokingState);
    virtual size_t getRuleIndex() const override;
    VarDeclNoSemiContext *varDeclNoSemi();
    antlr4::tree::TerminalNode *T_SEMICOLON();


    virtual std::any accept(antlr4::tree::ParseTreeVisitor *visitor) override;
   
  };

  VarDeclContext* varDecl();

  class  ConstDeclNoSemiContext : public antlr4::ParserRuleContext {
  public:
    ConstDeclNoSemiContext(antlr4::ParserRuleContext *parent, size_t invokingState);
    virtual size_t getRuleIndex() const override;
    antlr4::tree::TerminalNode *T_CONST();
    BasicTypeContext *basicType();
    std::vector<ConstDefContext *> constDef();
    ConstDefContext* constDef(size_t i);
    antlr4::tree::TerminalNode *T_STATIC();
    std::vector<antlr4::tree::TerminalNode *> T_COMMA();
    antlr4::tree::TerminalNode* T_COMMA(size_t i);


    virtual std::any accept(antlr4::tree::ParseTreeVisitor *visitor) override;
   
  };

  ConstDeclNoSemiContext* constDeclNoSemi();

  class  VarDeclNoSemiContext : public antlr4::ParserRuleContext {
  public:
    VarDeclNoSemiContext(antlr4::ParserRuleContext *parent, size_t invokingState);
    virtual size_t getRuleIndex() const override;
    BasicTypeContext *basicType();
    std::vector<VarDefContext *> varDef();
    VarDefContext* varDef(size_t i);
    antlr4::tree::TerminalNode *T_STATIC();
    std::vector<antlr4::tree::TerminalNode *> T_COMMA();
    antlr4::tree::TerminalNode* T_COMMA(size_t i);


    virtual std::any accept(antlr4::tree::ParseTreeVisitor *visitor) override;
   
  };

  VarDeclNoSemiContext* varDeclNoSemi();

  class  ConstDefContext : public antlr4::ParserRuleContext {
  public:
    ConstDefContext(antlr4::ParserRuleContext *parent, size_t invokingState);
    virtual size_t getRuleIndex() const override;
    antlr4::tree::TerminalNode *T_ID();
    antlr4::tree::TerminalNode *T_ASSIGN();
    InitValContext *initVal();
    ArrayDefDimsContext *arrayDefDims();


    virtual std::any accept(antlr4::tree::ParseTreeVisitor *visitor) override;
   
  };

  ConstDefContext* constDef();

  class  BasicTypeContext : public antlr4::ParserRuleContext {
  public:
    BasicTypeContext(antlr4::ParserRuleContext *parent, size_t invokingState);
    virtual size_t getRuleIndex() const override;
    antlr4::tree::TerminalNode *T_INT();
    antlr4::tree::TerminalNode *T_FLOAT();


    virtual std::any accept(antlr4::tree::ParseTreeVisitor *visitor) override;
   
  };

  BasicTypeContext* basicType();

  class  VarDefContext : public antlr4::ParserRuleContext {
  public:
    VarDefContext(antlr4::ParserRuleContext *parent, size_t invokingState);
    virtual size_t getRuleIndex() const override;
    antlr4::tree::TerminalNode *T_ID();
    ArrayDefDimsContext *arrayDefDims();
    antlr4::tree::TerminalNode *T_ASSIGN();
    InitValContext *initVal();


    virtual std::any accept(antlr4::tree::ParseTreeVisitor *visitor) override;
   
  };

  VarDefContext* varDef();

  class  ArrayDefDimsContext : public antlr4::ParserRuleContext {
  public:
    ArrayDefDimsContext(antlr4::ParserRuleContext *parent, size_t invokingState);
    virtual size_t getRuleIndex() const override;
    std::vector<antlr4::tree::TerminalNode *> T_L_BRACK();
    antlr4::tree::TerminalNode* T_L_BRACK(size_t i);
    std::vector<ExprContext *> expr();
    ExprContext* expr(size_t i);
    std::vector<antlr4::tree::TerminalNode *> T_R_BRACK();
    antlr4::tree::TerminalNode* T_R_BRACK(size_t i);


    virtual std::any accept(antlr4::tree::ParseTreeVisitor *visitor) override;
   
  };

  ArrayDefDimsContext* arrayDefDims();

  class  InitValContext : public antlr4::ParserRuleContext {
  public:
    InitValContext(antlr4::ParserRuleContext *parent, size_t invokingState);
    virtual size_t getRuleIndex() const override;
    ExprContext *expr();
    antlr4::tree::TerminalNode *T_L_BRACE();
    antlr4::tree::TerminalNode *T_R_BRACE();
    std::vector<InitValContext *> initVal();
    InitValContext* initVal(size_t i);
    std::vector<antlr4::tree::TerminalNode *> T_COMMA();
    antlr4::tree::TerminalNode* T_COMMA(size_t i);


    virtual std::any accept(antlr4::tree::ParseTreeVisitor *visitor) override;
   
  };

  InitValContext* initVal();

  class  StatementContext : public antlr4::ParserRuleContext {
  public:
    StatementContext(antlr4::ParserRuleContext *parent, size_t invokingState);
   
    StatementContext() = default;
    void copyFrom(StatementContext *context);
    using antlr4::ParserRuleContext::copyFrom;

    virtual size_t getRuleIndex() const override;

   
  };

  class  UnmatchedStatementWrapperContext : public StatementContext {
  public:
    UnmatchedStatementWrapperContext(StatementContext *ctx);

    UnmatchedStatementContext *unmatchedStatement();

    virtual std::any accept(antlr4::tree::ParseTreeVisitor *visitor) override;
  };

  class  MatchedStatementWrapperContext : public StatementContext {
  public:
    MatchedStatementWrapperContext(StatementContext *ctx);

    MatchedStatementContext *matchedStatement();

    virtual std::any accept(antlr4::tree::ParseTreeVisitor *visitor) override;
  };

  StatementContext* statement();

  class  MatchedStatementContext : public antlr4::ParserRuleContext {
  public:
    MatchedStatementContext(antlr4::ParserRuleContext *parent, size_t invokingState);
   
    MatchedStatementContext() = default;
    void copyFrom(MatchedStatementContext *context);
    using antlr4::ParserRuleContext::copyFrom;

    virtual size_t getRuleIndex() const override;

   
  };

  class  BlockStatementContext : public MatchedStatementContext {
  public:
    BlockStatementContext(MatchedStatementContext *ctx);

    BlockContext *block();

    virtual std::any accept(antlr4::tree::ParseTreeVisitor *visitor) override;
  };

  class  IfElseMatchedStatementContext : public MatchedStatementContext {
  public:
    IfElseMatchedStatementContext(MatchedStatementContext *ctx);

    antlr4::tree::TerminalNode *T_IF();
    antlr4::tree::TerminalNode *T_L_PAREN();
    CondContext *cond();
    antlr4::tree::TerminalNode *T_R_PAREN();
    std::vector<MatchedStatementContext *> matchedStatement();
    MatchedStatementContext* matchedStatement(size_t i);
    antlr4::tree::TerminalNode *T_ELSE();

    virtual std::any accept(antlr4::tree::ParseTreeVisitor *visitor) override;
  };

  class  WhileMatchedStatementContext : public MatchedStatementContext {
  public:
    WhileMatchedStatementContext(MatchedStatementContext *ctx);

    antlr4::tree::TerminalNode *T_WHILE();
    antlr4::tree::TerminalNode *T_L_PAREN();
    CondContext *cond();
    antlr4::tree::TerminalNode *T_R_PAREN();
    MatchedStatementContext *matchedStatement();

    virtual std::any accept(antlr4::tree::ParseTreeVisitor *visitor) override;
  };

  class  AssignStatementContext : public MatchedStatementContext {
  public:
    AssignStatementContext(MatchedStatementContext *ctx);

    LValContext *lVal();
    antlr4::tree::TerminalNode *T_ASSIGN();
    ExprContext *expr();
    antlr4::tree::TerminalNode *T_SEMICOLON();

    virtual std::any accept(antlr4::tree::ParseTreeVisitor *visitor) override;
  };

  class  ForStatementContext : public MatchedStatementContext {
  public:
    ForStatementContext(MatchedStatementContext *ctx);

    antlr4::tree::TerminalNode *T_FOR();
    antlr4::tree::TerminalNode *T_L_PAREN();
    std::vector<antlr4::tree::TerminalNode *> T_SEMICOLON();
    antlr4::tree::TerminalNode* T_SEMICOLON(size_t i);
    antlr4::tree::TerminalNode *T_R_PAREN();
    MatchedStatementContext *matchedStatement();
    ForInitContext *forInit();
    CondContext *cond();
    ForStepContext *forStep();

    virtual std::any accept(antlr4::tree::ParseTreeVisitor *visitor) override;
  };

  class  BreakStatementContext : public MatchedStatementContext {
  public:
    BreakStatementContext(MatchedStatementContext *ctx);

    antlr4::tree::TerminalNode *T_BREAK();
    antlr4::tree::TerminalNode *T_SEMICOLON();

    virtual std::any accept(antlr4::tree::ParseTreeVisitor *visitor) override;
  };

  class  ContinueStatementContext : public MatchedStatementContext {
  public:
    ContinueStatementContext(MatchedStatementContext *ctx);

    antlr4::tree::TerminalNode *T_CONTINUE();
    antlr4::tree::TerminalNode *T_SEMICOLON();

    virtual std::any accept(antlr4::tree::ParseTreeVisitor *visitor) override;
  };

  class  ExpressionStatementContext : public MatchedStatementContext {
  public:
    ExpressionStatementContext(MatchedStatementContext *ctx);

    antlr4::tree::TerminalNode *T_SEMICOLON();
    ExprContext *expr();

    virtual std::any accept(antlr4::tree::ParseTreeVisitor *visitor) override;
  };

  class  ReturnStatementContext : public MatchedStatementContext {
  public:
    ReturnStatementContext(MatchedStatementContext *ctx);

    antlr4::tree::TerminalNode *T_RETURN();
    antlr4::tree::TerminalNode *T_SEMICOLON();
    ExprContext *expr();

    virtual std::any accept(antlr4::tree::ParseTreeVisitor *visitor) override;
  };

  MatchedStatementContext* matchedStatement();

  class  UnmatchedStatementContext : public antlr4::ParserRuleContext {
  public:
    UnmatchedStatementContext(antlr4::ParserRuleContext *parent, size_t invokingState);
   
    UnmatchedStatementContext() = default;
    void copyFrom(UnmatchedStatementContext *context);
    using antlr4::ParserRuleContext::copyFrom;

    virtual size_t getRuleIndex() const override;

   
  };

  class  IfElseUnmatchedStatementContext : public UnmatchedStatementContext {
  public:
    IfElseUnmatchedStatementContext(UnmatchedStatementContext *ctx);

    antlr4::tree::TerminalNode *T_IF();
    antlr4::tree::TerminalNode *T_L_PAREN();
    CondContext *cond();
    antlr4::tree::TerminalNode *T_R_PAREN();
    MatchedStatementContext *matchedStatement();
    antlr4::tree::TerminalNode *T_ELSE();
    UnmatchedStatementContext *unmatchedStatement();

    virtual std::any accept(antlr4::tree::ParseTreeVisitor *visitor) override;
  };

  class  WhileUnmatchedStatementContext : public UnmatchedStatementContext {
  public:
    WhileUnmatchedStatementContext(UnmatchedStatementContext *ctx);

    antlr4::tree::TerminalNode *T_WHILE();
    antlr4::tree::TerminalNode *T_L_PAREN();
    CondContext *cond();
    antlr4::tree::TerminalNode *T_R_PAREN();
    UnmatchedStatementContext *unmatchedStatement();

    virtual std::any accept(antlr4::tree::ParseTreeVisitor *visitor) override;
  };

  class  IfWithoutElseStatementContext : public UnmatchedStatementContext {
  public:
    IfWithoutElseStatementContext(UnmatchedStatementContext *ctx);

    antlr4::tree::TerminalNode *T_IF();
    antlr4::tree::TerminalNode *T_L_PAREN();
    CondContext *cond();
    antlr4::tree::TerminalNode *T_R_PAREN();
    StatementContext *statement();

    virtual std::any accept(antlr4::tree::ParseTreeVisitor *visitor) override;
  };

  class  ForUnmatchedStatementContext : public UnmatchedStatementContext {
  public:
    ForUnmatchedStatementContext(UnmatchedStatementContext *ctx);

    antlr4::tree::TerminalNode *T_FOR();
    antlr4::tree::TerminalNode *T_L_PAREN();
    std::vector<antlr4::tree::TerminalNode *> T_SEMICOLON();
    antlr4::tree::TerminalNode* T_SEMICOLON(size_t i);
    antlr4::tree::TerminalNode *T_R_PAREN();
    UnmatchedStatementContext *unmatchedStatement();
    ForInitContext *forInit();
    CondContext *cond();
    ForStepContext *forStep();

    virtual std::any accept(antlr4::tree::ParseTreeVisitor *visitor) override;
  };

  UnmatchedStatementContext* unmatchedStatement();

  class  ForInitContext : public antlr4::ParserRuleContext {
  public:
    ForInitContext(antlr4::ParserRuleContext *parent, size_t invokingState);
    virtual size_t getRuleIndex() const override;
    ConstDeclNoSemiContext *constDeclNoSemi();
    VarDeclNoSemiContext *varDeclNoSemi();
    LValContext *lVal();
    antlr4::tree::TerminalNode *T_ASSIGN();
    ExprContext *expr();


    virtual std::any accept(antlr4::tree::ParseTreeVisitor *visitor) override;
   
  };

  ForInitContext* forInit();

  class  ForStepContext : public antlr4::ParserRuleContext {
  public:
    ForStepContext(antlr4::ParserRuleContext *parent, size_t invokingState);
    virtual size_t getRuleIndex() const override;
    LValContext *lVal();
    antlr4::tree::TerminalNode *T_ASSIGN();
    ExprContext *expr();


    virtual std::any accept(antlr4::tree::ParseTreeVisitor *visitor) override;
   
  };

  ForStepContext* forStep();

  class  ExprContext : public antlr4::ParserRuleContext {
  public:
    ExprContext(antlr4::ParserRuleContext *parent, size_t invokingState);
    virtual size_t getRuleIndex() const override;
    LOrExpContext *lOrExp();


    virtual std::any accept(antlr4::tree::ParseTreeVisitor *visitor) override;
   
  };

  ExprContext* expr();

  class  CondContext : public antlr4::ParserRuleContext {
  public:
    CondContext(antlr4::ParserRuleContext *parent, size_t invokingState);
    virtual size_t getRuleIndex() const override;
    ExprContext *expr();


    virtual std::any accept(antlr4::tree::ParseTreeVisitor *visitor) override;
   
  };

  CondContext* cond();

  class  LOrExpContext : public antlr4::ParserRuleContext {
  public:
    LOrExpContext(antlr4::ParserRuleContext *parent, size_t invokingState);
    virtual size_t getRuleIndex() const override;
    std::vector<LAndExpContext *> lAndExp();
    LAndExpContext* lAndExp(size_t i);
    std::vector<antlr4::tree::TerminalNode *> T_LOR();
    antlr4::tree::TerminalNode* T_LOR(size_t i);


    virtual std::any accept(antlr4::tree::ParseTreeVisitor *visitor) override;
   
  };

  LOrExpContext* lOrExp();

  class  LAndExpContext : public antlr4::ParserRuleContext {
  public:
    LAndExpContext(antlr4::ParserRuleContext *parent, size_t invokingState);
    virtual size_t getRuleIndex() const override;
    std::vector<EqExpContext *> eqExp();
    EqExpContext* eqExp(size_t i);
    std::vector<antlr4::tree::TerminalNode *> T_LAND();
    antlr4::tree::TerminalNode* T_LAND(size_t i);


    virtual std::any accept(antlr4::tree::ParseTreeVisitor *visitor) override;
   
  };

  LAndExpContext* lAndExp();

  class  EqExpContext : public antlr4::ParserRuleContext {
  public:
    EqExpContext(antlr4::ParserRuleContext *parent, size_t invokingState);
    virtual size_t getRuleIndex() const override;
    std::vector<RelExpContext *> relExp();
    RelExpContext* relExp(size_t i);
    std::vector<EqOpContext *> eqOp();
    EqOpContext* eqOp(size_t i);


    virtual std::any accept(antlr4::tree::ParseTreeVisitor *visitor) override;
   
  };

  EqExpContext* eqExp();

  class  EqOpContext : public antlr4::ParserRuleContext {
  public:
    EqOpContext(antlr4::ParserRuleContext *parent, size_t invokingState);
    virtual size_t getRuleIndex() const override;
    antlr4::tree::TerminalNode *T_EQ();
    antlr4::tree::TerminalNode *T_NE();


    virtual std::any accept(antlr4::tree::ParseTreeVisitor *visitor) override;
   
  };

  EqOpContext* eqOp();

  class  RelExpContext : public antlr4::ParserRuleContext {
  public:
    RelExpContext(antlr4::ParserRuleContext *parent, size_t invokingState);
    virtual size_t getRuleIndex() const override;
    std::vector<AddExpContext *> addExp();
    AddExpContext* addExp(size_t i);
    std::vector<RelOpContext *> relOp();
    RelOpContext* relOp(size_t i);


    virtual std::any accept(antlr4::tree::ParseTreeVisitor *visitor) override;
   
  };

  RelExpContext* relExp();

  class  RelOpContext : public antlr4::ParserRuleContext {
  public:
    RelOpContext(antlr4::ParserRuleContext *parent, size_t invokingState);
    virtual size_t getRuleIndex() const override;
    antlr4::tree::TerminalNode *T_LT();
    antlr4::tree::TerminalNode *T_GT();
    antlr4::tree::TerminalNode *T_LE();
    antlr4::tree::TerminalNode *T_GE();


    virtual std::any accept(antlr4::tree::ParseTreeVisitor *visitor) override;
   
  };

  RelOpContext* relOp();

  class  AddExpContext : public antlr4::ParserRuleContext {
  public:
    AddExpContext(antlr4::ParserRuleContext *parent, size_t invokingState);
    virtual size_t getRuleIndex() const override;
    std::vector<MulExpContext *> mulExp();
    MulExpContext* mulExp(size_t i);
    std::vector<AddOpContext *> addOp();
    AddOpContext* addOp(size_t i);


    virtual std::any accept(antlr4::tree::ParseTreeVisitor *visitor) override;
   
  };

  AddExpContext* addExp();

  class  AddOpContext : public antlr4::ParserRuleContext {
  public:
    AddOpContext(antlr4::ParserRuleContext *parent, size_t invokingState);
    virtual size_t getRuleIndex() const override;
    antlr4::tree::TerminalNode *T_ADD();
    antlr4::tree::TerminalNode *T_SUB();


    virtual std::any accept(antlr4::tree::ParseTreeVisitor *visitor) override;
   
  };

  AddOpContext* addOp();

  class  MulExpContext : public antlr4::ParserRuleContext {
  public:
    MulExpContext(antlr4::ParserRuleContext *parent, size_t invokingState);
    virtual size_t getRuleIndex() const override;
    std::vector<UnaryExpContext *> unaryExp();
    UnaryExpContext* unaryExp(size_t i);
    std::vector<MulOpContext *> mulOp();
    MulOpContext* mulOp(size_t i);


    virtual std::any accept(antlr4::tree::ParseTreeVisitor *visitor) override;
   
  };

  MulExpContext* mulExp();

  class  MulOpContext : public antlr4::ParserRuleContext {
  public:
    MulOpContext(antlr4::ParserRuleContext *parent, size_t invokingState);
    virtual size_t getRuleIndex() const override;
    antlr4::tree::TerminalNode *T_MUL();
    antlr4::tree::TerminalNode *T_DIV();
    antlr4::tree::TerminalNode *T_MOD();


    virtual std::any accept(antlr4::tree::ParseTreeVisitor *visitor) override;
   
  };

  MulOpContext* mulOp();

  class  UnaryExpContext : public antlr4::ParserRuleContext {
  public:
    UnaryExpContext(antlr4::ParserRuleContext *parent, size_t invokingState);
    virtual size_t getRuleIndex() const override;
    PrimaryExpContext *primaryExp();
    antlr4::tree::TerminalNode *T_ID();
    antlr4::tree::TerminalNode *T_L_PAREN();
    antlr4::tree::TerminalNode *T_R_PAREN();
    RealParamListContext *realParamList();
    UnaryOpContext *unaryOp();
    UnaryExpContext *unaryExp();
    LValContext *lVal();
    antlr4::tree::TerminalNode *T_INC();
    antlr4::tree::TerminalNode *T_DEC();


    virtual std::any accept(antlr4::tree::ParseTreeVisitor *visitor) override;
   
  };

  UnaryExpContext* unaryExp();

  class  UnaryOpContext : public antlr4::ParserRuleContext {
  public:
    UnaryOpContext(antlr4::ParserRuleContext *parent, size_t invokingState);
    virtual size_t getRuleIndex() const override;
    antlr4::tree::TerminalNode *T_ADD();
    antlr4::tree::TerminalNode *T_SUB();
    antlr4::tree::TerminalNode *T_NOT();


    virtual std::any accept(antlr4::tree::ParseTreeVisitor *visitor) override;
   
  };

  UnaryOpContext* unaryOp();

  class  PrimaryExpContext : public antlr4::ParserRuleContext {
  public:
    PrimaryExpContext(antlr4::ParserRuleContext *parent, size_t invokingState);
    virtual size_t getRuleIndex() const override;
    antlr4::tree::TerminalNode *T_L_PAREN();
    ExprContext *expr();
    antlr4::tree::TerminalNode *T_R_PAREN();
    antlr4::tree::TerminalNode *T_FLOAT_LITERAL();
    antlr4::tree::TerminalNode *T_DIGIT();
    LValContext *lVal();


    virtual std::any accept(antlr4::tree::ParseTreeVisitor *visitor) override;
   
  };

  PrimaryExpContext* primaryExp();

  class  RealParamContext : public antlr4::ParserRuleContext {
  public:
    RealParamContext(antlr4::ParserRuleContext *parent, size_t invokingState);
    virtual size_t getRuleIndex() const override;
    ExprContext *expr();
    antlr4::tree::TerminalNode *T_STRING_LITERAL();


    virtual std::any accept(antlr4::tree::ParseTreeVisitor *visitor) override;
   
  };

  RealParamContext* realParam();

  class  RealParamListContext : public antlr4::ParserRuleContext {
  public:
    RealParamListContext(antlr4::ParserRuleContext *parent, size_t invokingState);
    virtual size_t getRuleIndex() const override;
    std::vector<RealParamContext *> realParam();
    RealParamContext* realParam(size_t i);
    std::vector<antlr4::tree::TerminalNode *> T_COMMA();
    antlr4::tree::TerminalNode* T_COMMA(size_t i);


    virtual std::any accept(antlr4::tree::ParseTreeVisitor *visitor) override;
   
  };

  RealParamListContext* realParamList();

  class  LValContext : public antlr4::ParserRuleContext {
  public:
    LValContext(antlr4::ParserRuleContext *parent, size_t invokingState);
    virtual size_t getRuleIndex() const override;
    antlr4::tree::TerminalNode *T_ID();
    std::vector<antlr4::tree::TerminalNode *> T_L_BRACK();
    antlr4::tree::TerminalNode* T_L_BRACK(size_t i);
    std::vector<ExprContext *> expr();
    ExprContext* expr(size_t i);
    std::vector<antlr4::tree::TerminalNode *> T_R_BRACK();
    antlr4::tree::TerminalNode* T_R_BRACK(size_t i);


    virtual std::any accept(antlr4::tree::ParseTreeVisitor *visitor) override;
   
  };

  LValContext* lVal();


  // By default the static state used to implement the parser is lazily initialized during the first
  // call to the constructor. You can call this function if you wish to initialize the static state
  // ahead of time.
  static void initialize();

private:
};

