
// Generated from MiniC.g4 by ANTLR 4.12.0


#include "MiniCVisitor.h"

#include "MiniCParser.h"


using namespace antlrcpp;

using namespace antlr4;

namespace {

struct MiniCParserStaticData final {
  MiniCParserStaticData(std::vector<std::string> ruleNames,
                        std::vector<std::string> literalNames,
                        std::vector<std::string> symbolicNames)
      : ruleNames(std::move(ruleNames)), literalNames(std::move(literalNames)),
        symbolicNames(std::move(symbolicNames)),
        vocabulary(this->literalNames, this->symbolicNames) {}

  MiniCParserStaticData(const MiniCParserStaticData&) = delete;
  MiniCParserStaticData(MiniCParserStaticData&&) = delete;
  MiniCParserStaticData& operator=(const MiniCParserStaticData&) = delete;
  MiniCParserStaticData& operator=(MiniCParserStaticData&&) = delete;

  std::vector<antlr4::dfa::DFA> decisionToDFA;
  antlr4::atn::PredictionContextCache sharedContextCache;
  const std::vector<std::string> ruleNames;
  const std::vector<std::string> literalNames;
  const std::vector<std::string> symbolicNames;
  const antlr4::dfa::Vocabulary vocabulary;
  antlr4::atn::SerializedATNView serializedATN;
  std::unique_ptr<antlr4::atn::ATN> atn;
};

::antlr4::internal::OnceFlag minicParserOnceFlag;
MiniCParserStaticData *minicParserStaticData = nullptr;

void minicParserInitialize() {
  assert(minicParserStaticData == nullptr);
  auto staticData = std::make_unique<MiniCParserStaticData>(
    std::vector<std::string>{
      "compileUnit", "funcDef", "block", "blockItemList", "blockItem", "varDecl", 
      "basicType", "arrayDimensions", "initList", "initItem", "varDef", 
      "statement", "expr", "addExp", "addOp", "mulOp", "mulExp", "unaryOp", 
      "unaryExp", "primaryExp", "realParamList", "lVal"
    },
    std::vector<std::string>{
      "", "'('", "')'", "';'", "'{'", "'}'", "'['", "']'", "'='", "','", 
      "'+'", "'-'", "'*'", "'/'", "'%'", "'return'", "'int'", "'void'"
    },
    std::vector<std::string>{
      "", "T_L_PAREN", "T_R_PAREN", "T_SEMICOLON", "T_L_BRACE", "T_R_BRACE", 
      "T_L_BRACKET", "T_R_BRACKET", "T_ASSIGN", "T_COMMA", "T_ADD", "T_SUB", 
      "T_MUL", "T_DIV", "T_MOD", "T_RETURN", "T_INT", "T_VOID", "T_ID", 
      "T_DIGIT", "LINE_COMMENT", "BLOCK_COMMENT", "WS"
    }
  );
  static const int32_t serializedATNSegment[] = {
  	4,1,22,207,2,0,7,0,2,1,7,1,2,2,7,2,2,3,7,3,2,4,7,4,2,5,7,5,2,6,7,6,2,
  	7,7,7,2,8,7,8,2,9,7,9,2,10,7,10,2,11,7,11,2,12,7,12,2,13,7,13,2,14,7,
  	14,2,15,7,15,2,16,7,16,2,17,7,17,2,18,7,18,2,19,7,19,2,20,7,20,2,21,7,
  	21,1,0,1,0,5,0,47,8,0,10,0,12,0,50,9,0,1,0,1,0,1,1,1,1,1,1,1,1,1,1,1,
  	1,1,2,1,2,3,2,62,8,2,1,2,1,2,1,3,4,3,67,8,3,11,3,12,3,68,1,4,1,4,3,4,
  	73,8,4,1,5,1,5,1,5,1,5,5,5,79,8,5,10,5,12,5,82,9,5,1,5,1,5,1,6,1,6,1,
  	7,1,7,1,7,1,7,4,7,92,8,7,11,7,12,7,93,1,8,1,8,1,8,1,8,5,8,100,8,8,10,
  	8,12,8,103,9,8,3,8,105,8,8,1,8,1,8,1,9,1,9,3,9,111,8,9,1,10,1,10,3,10,
  	115,8,10,1,10,1,10,1,10,3,10,120,8,10,3,10,122,8,10,1,11,1,11,1,11,1,
  	11,1,11,1,11,1,11,1,11,1,11,1,11,1,11,3,11,135,8,11,1,11,3,11,138,8,11,
  	1,12,1,12,1,13,1,13,1,13,1,13,5,13,146,8,13,10,13,12,13,149,9,13,1,14,
  	1,14,1,15,1,15,1,16,1,16,1,16,1,16,5,16,159,8,16,10,16,12,16,162,9,16,
  	1,17,1,17,1,18,5,18,167,8,18,10,18,12,18,170,9,18,1,18,1,18,1,18,1,18,
  	3,18,176,8,18,1,18,3,18,179,8,18,1,19,1,19,1,19,1,19,1,19,1,19,3,19,187,
  	8,19,1,20,1,20,1,20,5,20,192,8,20,10,20,12,20,195,9,20,1,21,1,21,1,21,
  	1,21,1,21,5,21,202,8,21,10,21,12,21,205,9,21,1,21,0,0,22,0,2,4,6,8,10,
  	12,14,16,18,20,22,24,26,28,30,32,34,36,38,40,42,0,2,1,0,10,11,1,0,12,
  	14,210,0,48,1,0,0,0,2,53,1,0,0,0,4,59,1,0,0,0,6,66,1,0,0,0,8,72,1,0,0,
  	0,10,74,1,0,0,0,12,85,1,0,0,0,14,91,1,0,0,0,16,95,1,0,0,0,18,110,1,0,
  	0,0,20,112,1,0,0,0,22,137,1,0,0,0,24,139,1,0,0,0,26,141,1,0,0,0,28,150,
  	1,0,0,0,30,152,1,0,0,0,32,154,1,0,0,0,34,163,1,0,0,0,36,178,1,0,0,0,38,
  	186,1,0,0,0,40,188,1,0,0,0,42,196,1,0,0,0,44,47,3,2,1,0,45,47,3,10,5,
  	0,46,44,1,0,0,0,46,45,1,0,0,0,47,50,1,0,0,0,48,46,1,0,0,0,48,49,1,0,0,
  	0,49,51,1,0,0,0,50,48,1,0,0,0,51,52,5,0,0,1,52,1,1,0,0,0,53,54,5,16,0,
  	0,54,55,5,18,0,0,55,56,5,1,0,0,56,57,5,2,0,0,57,58,3,4,2,0,58,3,1,0,0,
  	0,59,61,5,4,0,0,60,62,3,6,3,0,61,60,1,0,0,0,61,62,1,0,0,0,62,63,1,0,0,
  	0,63,64,5,5,0,0,64,5,1,0,0,0,65,67,3,8,4,0,66,65,1,0,0,0,67,68,1,0,0,
  	0,68,66,1,0,0,0,68,69,1,0,0,0,69,7,1,0,0,0,70,73,3,22,11,0,71,73,3,10,
  	5,0,72,70,1,0,0,0,72,71,1,0,0,0,73,9,1,0,0,0,74,75,3,12,6,0,75,80,3,20,
  	10,0,76,77,5,9,0,0,77,79,3,20,10,0,78,76,1,0,0,0,79,82,1,0,0,0,80,78,
  	1,0,0,0,80,81,1,0,0,0,81,83,1,0,0,0,82,80,1,0,0,0,83,84,5,3,0,0,84,11,
  	1,0,0,0,85,86,5,16,0,0,86,13,1,0,0,0,87,88,5,6,0,0,88,89,3,24,12,0,89,
  	90,5,7,0,0,90,92,1,0,0,0,91,87,1,0,0,0,92,93,1,0,0,0,93,91,1,0,0,0,93,
  	94,1,0,0,0,94,15,1,0,0,0,95,104,5,4,0,0,96,101,3,18,9,0,97,98,5,9,0,0,
  	98,100,3,18,9,0,99,97,1,0,0,0,100,103,1,0,0,0,101,99,1,0,0,0,101,102,
  	1,0,0,0,102,105,1,0,0,0,103,101,1,0,0,0,104,96,1,0,0,0,104,105,1,0,0,
  	0,105,106,1,0,0,0,106,107,5,5,0,0,107,17,1,0,0,0,108,111,3,16,8,0,109,
  	111,3,24,12,0,110,108,1,0,0,0,110,109,1,0,0,0,111,19,1,0,0,0,112,114,
  	5,18,0,0,113,115,3,14,7,0,114,113,1,0,0,0,114,115,1,0,0,0,115,121,1,0,
  	0,0,116,119,5,8,0,0,117,120,3,16,8,0,118,120,3,24,12,0,119,117,1,0,0,
  	0,119,118,1,0,0,0,120,122,1,0,0,0,121,116,1,0,0,0,121,122,1,0,0,0,122,
  	21,1,0,0,0,123,124,5,15,0,0,124,125,3,24,12,0,125,126,5,3,0,0,126,138,
  	1,0,0,0,127,128,3,42,21,0,128,129,5,8,0,0,129,130,3,24,12,0,130,131,5,
  	3,0,0,131,138,1,0,0,0,132,138,3,4,2,0,133,135,3,24,12,0,134,133,1,0,0,
  	0,134,135,1,0,0,0,135,136,1,0,0,0,136,138,5,3,0,0,137,123,1,0,0,0,137,
  	127,1,0,0,0,137,132,1,0,0,0,137,134,1,0,0,0,138,23,1,0,0,0,139,140,3,
  	26,13,0,140,25,1,0,0,0,141,147,3,32,16,0,142,143,3,28,14,0,143,144,3,
  	32,16,0,144,146,1,0,0,0,145,142,1,0,0,0,146,149,1,0,0,0,147,145,1,0,0,
  	0,147,148,1,0,0,0,148,27,1,0,0,0,149,147,1,0,0,0,150,151,7,0,0,0,151,
  	29,1,0,0,0,152,153,7,1,0,0,153,31,1,0,0,0,154,160,3,36,18,0,155,156,3,
  	30,15,0,156,157,3,36,18,0,157,159,1,0,0,0,158,155,1,0,0,0,159,162,1,0,
  	0,0,160,158,1,0,0,0,160,161,1,0,0,0,161,33,1,0,0,0,162,160,1,0,0,0,163,
  	164,5,11,0,0,164,35,1,0,0,0,165,167,3,34,17,0,166,165,1,0,0,0,167,170,
  	1,0,0,0,168,166,1,0,0,0,168,169,1,0,0,0,169,171,1,0,0,0,170,168,1,0,0,
  	0,171,179,3,38,19,0,172,173,5,18,0,0,173,175,5,1,0,0,174,176,3,40,20,
  	0,175,174,1,0,0,0,175,176,1,0,0,0,176,177,1,0,0,0,177,179,5,2,0,0,178,
  	168,1,0,0,0,178,172,1,0,0,0,179,37,1,0,0,0,180,181,5,1,0,0,181,182,3,
  	24,12,0,182,183,5,2,0,0,183,187,1,0,0,0,184,187,5,19,0,0,185,187,3,42,
  	21,0,186,180,1,0,0,0,186,184,1,0,0,0,186,185,1,0,0,0,187,39,1,0,0,0,188,
  	193,3,24,12,0,189,190,5,9,0,0,190,192,3,24,12,0,191,189,1,0,0,0,192,195,
  	1,0,0,0,193,191,1,0,0,0,193,194,1,0,0,0,194,41,1,0,0,0,195,193,1,0,0,
  	0,196,203,5,18,0,0,197,198,5,6,0,0,198,199,3,24,12,0,199,200,5,7,0,0,
  	200,202,1,0,0,0,201,197,1,0,0,0,202,205,1,0,0,0,203,201,1,0,0,0,203,204,
  	1,0,0,0,204,43,1,0,0,0,205,203,1,0,0,0,23,46,48,61,68,72,80,93,101,104,
  	110,114,119,121,134,137,147,160,168,175,178,186,193,203
  };
  staticData->serializedATN = antlr4::atn::SerializedATNView(serializedATNSegment, sizeof(serializedATNSegment) / sizeof(serializedATNSegment[0]));

  antlr4::atn::ATNDeserializer deserializer;
  staticData->atn = deserializer.deserialize(staticData->serializedATN);

  const size_t count = staticData->atn->getNumberOfDecisions();
  staticData->decisionToDFA.reserve(count);
  for (size_t i = 0; i < count; i++) { 
    staticData->decisionToDFA.emplace_back(staticData->atn->getDecisionState(i), i);
  }
  minicParserStaticData = staticData.release();
}

}

MiniCParser::MiniCParser(TokenStream *input) : MiniCParser(input, antlr4::atn::ParserATNSimulatorOptions()) {}

MiniCParser::MiniCParser(TokenStream *input, const antlr4::atn::ParserATNSimulatorOptions &options) : Parser(input) {
  MiniCParser::initialize();
  _interpreter = new atn::ParserATNSimulator(this, *minicParserStaticData->atn, minicParserStaticData->decisionToDFA, minicParserStaticData->sharedContextCache, options);
}

MiniCParser::~MiniCParser() {
  delete _interpreter;
}

const atn::ATN& MiniCParser::getATN() const {
  return *minicParserStaticData->atn;
}

std::string MiniCParser::getGrammarFileName() const {
  return "MiniC.g4";
}

const std::vector<std::string>& MiniCParser::getRuleNames() const {
  return minicParserStaticData->ruleNames;
}

const dfa::Vocabulary& MiniCParser::getVocabulary() const {
  return minicParserStaticData->vocabulary;
}

antlr4::atn::SerializedATNView MiniCParser::getSerializedATN() const {
  return minicParserStaticData->serializedATN;
}


//----------------- CompileUnitContext ------------------------------------------------------------------

MiniCParser::CompileUnitContext::CompileUnitContext(ParserRuleContext *parent, size_t invokingState)
  : ParserRuleContext(parent, invokingState) {
}

tree::TerminalNode* MiniCParser::CompileUnitContext::EOF() {
  return getToken(MiniCParser::EOF, 0);
}

std::vector<MiniCParser::FuncDefContext *> MiniCParser::CompileUnitContext::funcDef() {
  return getRuleContexts<MiniCParser::FuncDefContext>();
}

MiniCParser::FuncDefContext* MiniCParser::CompileUnitContext::funcDef(size_t i) {
  return getRuleContext<MiniCParser::FuncDefContext>(i);
}

std::vector<MiniCParser::VarDeclContext *> MiniCParser::CompileUnitContext::varDecl() {
  return getRuleContexts<MiniCParser::VarDeclContext>();
}

MiniCParser::VarDeclContext* MiniCParser::CompileUnitContext::varDecl(size_t i) {
  return getRuleContext<MiniCParser::VarDeclContext>(i);
}


size_t MiniCParser::CompileUnitContext::getRuleIndex() const {
  return MiniCParser::RuleCompileUnit;
}


std::any MiniCParser::CompileUnitContext::accept(tree::ParseTreeVisitor *visitor) {
  if (auto parserVisitor = dynamic_cast<MiniCVisitor*>(visitor))
    return parserVisitor->visitCompileUnit(this);
  else
    return visitor->visitChildren(this);
}

MiniCParser::CompileUnitContext* MiniCParser::compileUnit() {
  CompileUnitContext *_localctx = _tracker.createInstance<CompileUnitContext>(_ctx, getState());
  enterRule(_localctx, 0, MiniCParser::RuleCompileUnit);
  size_t _la = 0;

#if __cplusplus > 201703L
  auto onExit = finally([=, this] {
#else
  auto onExit = finally([=] {
#endif
    exitRule();
  });
  try {
    enterOuterAlt(_localctx, 1);
    setState(48);
    _errHandler->sync(this);
    _la = _input->LA(1);
    while (_la == MiniCParser::T_INT) {
      setState(46);
      _errHandler->sync(this);
      switch (getInterpreter<atn::ParserATNSimulator>()->adaptivePredict(_input, 0, _ctx)) {
      case 1: {
        setState(44);
        funcDef();
        break;
      }

      case 2: {
        setState(45);
        varDecl();
        break;
      }

      default:
        break;
      }
      setState(50);
      _errHandler->sync(this);
      _la = _input->LA(1);
    }
    setState(51);
    match(MiniCParser::EOF);
   
  }
  catch (RecognitionException &e) {
    _errHandler->reportError(this, e);
    _localctx->exception = std::current_exception();
    _errHandler->recover(this, _localctx->exception);
  }

  return _localctx;
}

//----------------- FuncDefContext ------------------------------------------------------------------

MiniCParser::FuncDefContext::FuncDefContext(ParserRuleContext *parent, size_t invokingState)
  : ParserRuleContext(parent, invokingState) {
}

tree::TerminalNode* MiniCParser::FuncDefContext::T_INT() {
  return getToken(MiniCParser::T_INT, 0);
}

tree::TerminalNode* MiniCParser::FuncDefContext::T_ID() {
  return getToken(MiniCParser::T_ID, 0);
}

tree::TerminalNode* MiniCParser::FuncDefContext::T_L_PAREN() {
  return getToken(MiniCParser::T_L_PAREN, 0);
}

tree::TerminalNode* MiniCParser::FuncDefContext::T_R_PAREN() {
  return getToken(MiniCParser::T_R_PAREN, 0);
}

MiniCParser::BlockContext* MiniCParser::FuncDefContext::block() {
  return getRuleContext<MiniCParser::BlockContext>(0);
}


size_t MiniCParser::FuncDefContext::getRuleIndex() const {
  return MiniCParser::RuleFuncDef;
}


std::any MiniCParser::FuncDefContext::accept(tree::ParseTreeVisitor *visitor) {
  if (auto parserVisitor = dynamic_cast<MiniCVisitor*>(visitor))
    return parserVisitor->visitFuncDef(this);
  else
    return visitor->visitChildren(this);
}

MiniCParser::FuncDefContext* MiniCParser::funcDef() {
  FuncDefContext *_localctx = _tracker.createInstance<FuncDefContext>(_ctx, getState());
  enterRule(_localctx, 2, MiniCParser::RuleFuncDef);

#if __cplusplus > 201703L
  auto onExit = finally([=, this] {
#else
  auto onExit = finally([=] {
#endif
    exitRule();
  });
  try {
    enterOuterAlt(_localctx, 1);
    setState(53);
    match(MiniCParser::T_INT);
    setState(54);
    match(MiniCParser::T_ID);
    setState(55);
    match(MiniCParser::T_L_PAREN);
    setState(56);
    match(MiniCParser::T_R_PAREN);
    setState(57);
    block();
   
  }
  catch (RecognitionException &e) {
    _errHandler->reportError(this, e);
    _localctx->exception = std::current_exception();
    _errHandler->recover(this, _localctx->exception);
  }

  return _localctx;
}

//----------------- BlockContext ------------------------------------------------------------------

MiniCParser::BlockContext::BlockContext(ParserRuleContext *parent, size_t invokingState)
  : ParserRuleContext(parent, invokingState) {
}

tree::TerminalNode* MiniCParser::BlockContext::T_L_BRACE() {
  return getToken(MiniCParser::T_L_BRACE, 0);
}

tree::TerminalNode* MiniCParser::BlockContext::T_R_BRACE() {
  return getToken(MiniCParser::T_R_BRACE, 0);
}

MiniCParser::BlockItemListContext* MiniCParser::BlockContext::blockItemList() {
  return getRuleContext<MiniCParser::BlockItemListContext>(0);
}


size_t MiniCParser::BlockContext::getRuleIndex() const {
  return MiniCParser::RuleBlock;
}


std::any MiniCParser::BlockContext::accept(tree::ParseTreeVisitor *visitor) {
  if (auto parserVisitor = dynamic_cast<MiniCVisitor*>(visitor))
    return parserVisitor->visitBlock(this);
  else
    return visitor->visitChildren(this);
}

MiniCParser::BlockContext* MiniCParser::block() {
  BlockContext *_localctx = _tracker.createInstance<BlockContext>(_ctx, getState());
  enterRule(_localctx, 4, MiniCParser::RuleBlock);
  size_t _la = 0;

#if __cplusplus > 201703L
  auto onExit = finally([=, this] {
#else
  auto onExit = finally([=] {
#endif
    exitRule();
  });
  try {
    enterOuterAlt(_localctx, 1);
    setState(59);
    match(MiniCParser::T_L_BRACE);
    setState(61);
    _errHandler->sync(this);

    _la = _input->LA(1);
    if ((((_la & ~ 0x3fULL) == 0) &&
      ((1ULL << _la) & 886810) != 0)) {
      setState(60);
      blockItemList();
    }
    setState(63);
    match(MiniCParser::T_R_BRACE);
   
  }
  catch (RecognitionException &e) {
    _errHandler->reportError(this, e);
    _localctx->exception = std::current_exception();
    _errHandler->recover(this, _localctx->exception);
  }

  return _localctx;
}

//----------------- BlockItemListContext ------------------------------------------------------------------

MiniCParser::BlockItemListContext::BlockItemListContext(ParserRuleContext *parent, size_t invokingState)
  : ParserRuleContext(parent, invokingState) {
}

std::vector<MiniCParser::BlockItemContext *> MiniCParser::BlockItemListContext::blockItem() {
  return getRuleContexts<MiniCParser::BlockItemContext>();
}

MiniCParser::BlockItemContext* MiniCParser::BlockItemListContext::blockItem(size_t i) {
  return getRuleContext<MiniCParser::BlockItemContext>(i);
}


size_t MiniCParser::BlockItemListContext::getRuleIndex() const {
  return MiniCParser::RuleBlockItemList;
}


std::any MiniCParser::BlockItemListContext::accept(tree::ParseTreeVisitor *visitor) {
  if (auto parserVisitor = dynamic_cast<MiniCVisitor*>(visitor))
    return parserVisitor->visitBlockItemList(this);
  else
    return visitor->visitChildren(this);
}

MiniCParser::BlockItemListContext* MiniCParser::blockItemList() {
  BlockItemListContext *_localctx = _tracker.createInstance<BlockItemListContext>(_ctx, getState());
  enterRule(_localctx, 6, MiniCParser::RuleBlockItemList);
  size_t _la = 0;

#if __cplusplus > 201703L
  auto onExit = finally([=, this] {
#else
  auto onExit = finally([=] {
#endif
    exitRule();
  });
  try {
    enterOuterAlt(_localctx, 1);
    setState(66); 
    _errHandler->sync(this);
    _la = _input->LA(1);
    do {
      setState(65);
      blockItem();
      setState(68); 
      _errHandler->sync(this);
      _la = _input->LA(1);
    } while ((((_la & ~ 0x3fULL) == 0) &&
      ((1ULL << _la) & 886810) != 0));
   
  }
  catch (RecognitionException &e) {
    _errHandler->reportError(this, e);
    _localctx->exception = std::current_exception();
    _errHandler->recover(this, _localctx->exception);
  }

  return _localctx;
}

//----------------- BlockItemContext ------------------------------------------------------------------

MiniCParser::BlockItemContext::BlockItemContext(ParserRuleContext *parent, size_t invokingState)
  : ParserRuleContext(parent, invokingState) {
}

MiniCParser::StatementContext* MiniCParser::BlockItemContext::statement() {
  return getRuleContext<MiniCParser::StatementContext>(0);
}

MiniCParser::VarDeclContext* MiniCParser::BlockItemContext::varDecl() {
  return getRuleContext<MiniCParser::VarDeclContext>(0);
}


size_t MiniCParser::BlockItemContext::getRuleIndex() const {
  return MiniCParser::RuleBlockItem;
}


std::any MiniCParser::BlockItemContext::accept(tree::ParseTreeVisitor *visitor) {
  if (auto parserVisitor = dynamic_cast<MiniCVisitor*>(visitor))
    return parserVisitor->visitBlockItem(this);
  else
    return visitor->visitChildren(this);
}

MiniCParser::BlockItemContext* MiniCParser::blockItem() {
  BlockItemContext *_localctx = _tracker.createInstance<BlockItemContext>(_ctx, getState());
  enterRule(_localctx, 8, MiniCParser::RuleBlockItem);

#if __cplusplus > 201703L
  auto onExit = finally([=, this] {
#else
  auto onExit = finally([=] {
#endif
    exitRule();
  });
  try {
    setState(72);
    _errHandler->sync(this);
    switch (_input->LA(1)) {
      case MiniCParser::T_L_PAREN:
      case MiniCParser::T_SEMICOLON:
      case MiniCParser::T_L_BRACE:
      case MiniCParser::T_SUB:
      case MiniCParser::T_RETURN:
      case MiniCParser::T_ID:
      case MiniCParser::T_DIGIT: {
        enterOuterAlt(_localctx, 1);
        setState(70);
        statement();
        break;
      }

      case MiniCParser::T_INT: {
        enterOuterAlt(_localctx, 2);
        setState(71);
        varDecl();
        break;
      }

    default:
      throw NoViableAltException(this);
    }
   
  }
  catch (RecognitionException &e) {
    _errHandler->reportError(this, e);
    _localctx->exception = std::current_exception();
    _errHandler->recover(this, _localctx->exception);
  }

  return _localctx;
}

//----------------- VarDeclContext ------------------------------------------------------------------

MiniCParser::VarDeclContext::VarDeclContext(ParserRuleContext *parent, size_t invokingState)
  : ParserRuleContext(parent, invokingState) {
}

MiniCParser::BasicTypeContext* MiniCParser::VarDeclContext::basicType() {
  return getRuleContext<MiniCParser::BasicTypeContext>(0);
}

std::vector<MiniCParser::VarDefContext *> MiniCParser::VarDeclContext::varDef() {
  return getRuleContexts<MiniCParser::VarDefContext>();
}

MiniCParser::VarDefContext* MiniCParser::VarDeclContext::varDef(size_t i) {
  return getRuleContext<MiniCParser::VarDefContext>(i);
}

tree::TerminalNode* MiniCParser::VarDeclContext::T_SEMICOLON() {
  return getToken(MiniCParser::T_SEMICOLON, 0);
}

std::vector<tree::TerminalNode *> MiniCParser::VarDeclContext::T_COMMA() {
  return getTokens(MiniCParser::T_COMMA);
}

tree::TerminalNode* MiniCParser::VarDeclContext::T_COMMA(size_t i) {
  return getToken(MiniCParser::T_COMMA, i);
}


size_t MiniCParser::VarDeclContext::getRuleIndex() const {
  return MiniCParser::RuleVarDecl;
}


std::any MiniCParser::VarDeclContext::accept(tree::ParseTreeVisitor *visitor) {
  if (auto parserVisitor = dynamic_cast<MiniCVisitor*>(visitor))
    return parserVisitor->visitVarDecl(this);
  else
    return visitor->visitChildren(this);
}

MiniCParser::VarDeclContext* MiniCParser::varDecl() {
  VarDeclContext *_localctx = _tracker.createInstance<VarDeclContext>(_ctx, getState());
  enterRule(_localctx, 10, MiniCParser::RuleVarDecl);
  size_t _la = 0;

#if __cplusplus > 201703L
  auto onExit = finally([=, this] {
#else
  auto onExit = finally([=] {
#endif
    exitRule();
  });
  try {
    enterOuterAlt(_localctx, 1);
    setState(74);
    basicType();
    setState(75);
    varDef();
    setState(80);
    _errHandler->sync(this);
    _la = _input->LA(1);
    while (_la == MiniCParser::T_COMMA) {
      setState(76);
      match(MiniCParser::T_COMMA);
      setState(77);
      varDef();
      setState(82);
      _errHandler->sync(this);
      _la = _input->LA(1);
    }
    setState(83);
    match(MiniCParser::T_SEMICOLON);
   
  }
  catch (RecognitionException &e) {
    _errHandler->reportError(this, e);
    _localctx->exception = std::current_exception();
    _errHandler->recover(this, _localctx->exception);
  }

  return _localctx;
}

//----------------- BasicTypeContext ------------------------------------------------------------------

MiniCParser::BasicTypeContext::BasicTypeContext(ParserRuleContext *parent, size_t invokingState)
  : ParserRuleContext(parent, invokingState) {
}

tree::TerminalNode* MiniCParser::BasicTypeContext::T_INT() {
  return getToken(MiniCParser::T_INT, 0);
}


size_t MiniCParser::BasicTypeContext::getRuleIndex() const {
  return MiniCParser::RuleBasicType;
}


std::any MiniCParser::BasicTypeContext::accept(tree::ParseTreeVisitor *visitor) {
  if (auto parserVisitor = dynamic_cast<MiniCVisitor*>(visitor))
    return parserVisitor->visitBasicType(this);
  else
    return visitor->visitChildren(this);
}

MiniCParser::BasicTypeContext* MiniCParser::basicType() {
  BasicTypeContext *_localctx = _tracker.createInstance<BasicTypeContext>(_ctx, getState());
  enterRule(_localctx, 12, MiniCParser::RuleBasicType);

#if __cplusplus > 201703L
  auto onExit = finally([=, this] {
#else
  auto onExit = finally([=] {
#endif
    exitRule();
  });
  try {
    enterOuterAlt(_localctx, 1);
    setState(85);
    match(MiniCParser::T_INT);
   
  }
  catch (RecognitionException &e) {
    _errHandler->reportError(this, e);
    _localctx->exception = std::current_exception();
    _errHandler->recover(this, _localctx->exception);
  }

  return _localctx;
}

//----------------- ArrayDimensionsContext ------------------------------------------------------------------

MiniCParser::ArrayDimensionsContext::ArrayDimensionsContext(ParserRuleContext *parent, size_t invokingState)
  : ParserRuleContext(parent, invokingState) {
}

std::vector<tree::TerminalNode *> MiniCParser::ArrayDimensionsContext::T_L_BRACKET() {
  return getTokens(MiniCParser::T_L_BRACKET);
}

tree::TerminalNode* MiniCParser::ArrayDimensionsContext::T_L_BRACKET(size_t i) {
  return getToken(MiniCParser::T_L_BRACKET, i);
}

std::vector<MiniCParser::ExprContext *> MiniCParser::ArrayDimensionsContext::expr() {
  return getRuleContexts<MiniCParser::ExprContext>();
}

MiniCParser::ExprContext* MiniCParser::ArrayDimensionsContext::expr(size_t i) {
  return getRuleContext<MiniCParser::ExprContext>(i);
}

std::vector<tree::TerminalNode *> MiniCParser::ArrayDimensionsContext::T_R_BRACKET() {
  return getTokens(MiniCParser::T_R_BRACKET);
}

tree::TerminalNode* MiniCParser::ArrayDimensionsContext::T_R_BRACKET(size_t i) {
  return getToken(MiniCParser::T_R_BRACKET, i);
}


size_t MiniCParser::ArrayDimensionsContext::getRuleIndex() const {
  return MiniCParser::RuleArrayDimensions;
}


std::any MiniCParser::ArrayDimensionsContext::accept(tree::ParseTreeVisitor *visitor) {
  if (auto parserVisitor = dynamic_cast<MiniCVisitor*>(visitor))
    return parserVisitor->visitArrayDimensions(this);
  else
    return visitor->visitChildren(this);
}

MiniCParser::ArrayDimensionsContext* MiniCParser::arrayDimensions() {
  ArrayDimensionsContext *_localctx = _tracker.createInstance<ArrayDimensionsContext>(_ctx, getState());
  enterRule(_localctx, 14, MiniCParser::RuleArrayDimensions);
  size_t _la = 0;

#if __cplusplus > 201703L
  auto onExit = finally([=, this] {
#else
  auto onExit = finally([=] {
#endif
    exitRule();
  });
  try {
    enterOuterAlt(_localctx, 1);
    setState(91); 
    _errHandler->sync(this);
    _la = _input->LA(1);
    do {
      setState(87);
      match(MiniCParser::T_L_BRACKET);
      setState(88);
      expr();
      setState(89);
      match(MiniCParser::T_R_BRACKET);
      setState(93); 
      _errHandler->sync(this);
      _la = _input->LA(1);
    } while (_la == MiniCParser::T_L_BRACKET);
   
  }
  catch (RecognitionException &e) {
    _errHandler->reportError(this, e);
    _localctx->exception = std::current_exception();
    _errHandler->recover(this, _localctx->exception);
  }

  return _localctx;
}

//----------------- InitListContext ------------------------------------------------------------------

MiniCParser::InitListContext::InitListContext(ParserRuleContext *parent, size_t invokingState)
  : ParserRuleContext(parent, invokingState) {
}

tree::TerminalNode* MiniCParser::InitListContext::T_L_BRACE() {
  return getToken(MiniCParser::T_L_BRACE, 0);
}

tree::TerminalNode* MiniCParser::InitListContext::T_R_BRACE() {
  return getToken(MiniCParser::T_R_BRACE, 0);
}

std::vector<MiniCParser::InitItemContext *> MiniCParser::InitListContext::initItem() {
  return getRuleContexts<MiniCParser::InitItemContext>();
}

MiniCParser::InitItemContext* MiniCParser::InitListContext::initItem(size_t i) {
  return getRuleContext<MiniCParser::InitItemContext>(i);
}

std::vector<tree::TerminalNode *> MiniCParser::InitListContext::T_COMMA() {
  return getTokens(MiniCParser::T_COMMA);
}

tree::TerminalNode* MiniCParser::InitListContext::T_COMMA(size_t i) {
  return getToken(MiniCParser::T_COMMA, i);
}


size_t MiniCParser::InitListContext::getRuleIndex() const {
  return MiniCParser::RuleInitList;
}


std::any MiniCParser::InitListContext::accept(tree::ParseTreeVisitor *visitor) {
  if (auto parserVisitor = dynamic_cast<MiniCVisitor*>(visitor))
    return parserVisitor->visitInitList(this);
  else
    return visitor->visitChildren(this);
}

MiniCParser::InitListContext* MiniCParser::initList() {
  InitListContext *_localctx = _tracker.createInstance<InitListContext>(_ctx, getState());
  enterRule(_localctx, 16, MiniCParser::RuleInitList);
  size_t _la = 0;

#if __cplusplus > 201703L
  auto onExit = finally([=, this] {
#else
  auto onExit = finally([=] {
#endif
    exitRule();
  });
  try {
    enterOuterAlt(_localctx, 1);
    setState(95);
    match(MiniCParser::T_L_BRACE);
    setState(104);
    _errHandler->sync(this);

    _la = _input->LA(1);
    if ((((_la & ~ 0x3fULL) == 0) &&
      ((1ULL << _la) & 788498) != 0)) {
      setState(96);
      initItem();
      setState(101);
      _errHandler->sync(this);
      _la = _input->LA(1);
      while (_la == MiniCParser::T_COMMA) {
        setState(97);
        match(MiniCParser::T_COMMA);
        setState(98);
        initItem();
        setState(103);
        _errHandler->sync(this);
        _la = _input->LA(1);
      }
    }
    setState(106);
    match(MiniCParser::T_R_BRACE);
   
  }
  catch (RecognitionException &e) {
    _errHandler->reportError(this, e);
    _localctx->exception = std::current_exception();
    _errHandler->recover(this, _localctx->exception);
  }

  return _localctx;
}

//----------------- InitItemContext ------------------------------------------------------------------

MiniCParser::InitItemContext::InitItemContext(ParserRuleContext *parent, size_t invokingState)
  : ParserRuleContext(parent, invokingState) {
}

MiniCParser::InitListContext* MiniCParser::InitItemContext::initList() {
  return getRuleContext<MiniCParser::InitListContext>(0);
}

MiniCParser::ExprContext* MiniCParser::InitItemContext::expr() {
  return getRuleContext<MiniCParser::ExprContext>(0);
}


size_t MiniCParser::InitItemContext::getRuleIndex() const {
  return MiniCParser::RuleInitItem;
}


std::any MiniCParser::InitItemContext::accept(tree::ParseTreeVisitor *visitor) {
  if (auto parserVisitor = dynamic_cast<MiniCVisitor*>(visitor))
    return parserVisitor->visitInitItem(this);
  else
    return visitor->visitChildren(this);
}

MiniCParser::InitItemContext* MiniCParser::initItem() {
  InitItemContext *_localctx = _tracker.createInstance<InitItemContext>(_ctx, getState());
  enterRule(_localctx, 18, MiniCParser::RuleInitItem);

#if __cplusplus > 201703L
  auto onExit = finally([=, this] {
#else
  auto onExit = finally([=] {
#endif
    exitRule();
  });
  try {
    setState(110);
    _errHandler->sync(this);
    switch (_input->LA(1)) {
      case MiniCParser::T_L_BRACE: {
        enterOuterAlt(_localctx, 1);
        setState(108);
        initList();
        break;
      }

      case MiniCParser::T_L_PAREN:
      case MiniCParser::T_SUB:
      case MiniCParser::T_ID:
      case MiniCParser::T_DIGIT: {
        enterOuterAlt(_localctx, 2);
        setState(109);
        expr();
        break;
      }

    default:
      throw NoViableAltException(this);
    }
   
  }
  catch (RecognitionException &e) {
    _errHandler->reportError(this, e);
    _localctx->exception = std::current_exception();
    _errHandler->recover(this, _localctx->exception);
  }

  return _localctx;
}

//----------------- VarDefContext ------------------------------------------------------------------

MiniCParser::VarDefContext::VarDefContext(ParserRuleContext *parent, size_t invokingState)
  : ParserRuleContext(parent, invokingState) {
}

tree::TerminalNode* MiniCParser::VarDefContext::T_ID() {
  return getToken(MiniCParser::T_ID, 0);
}

MiniCParser::ArrayDimensionsContext* MiniCParser::VarDefContext::arrayDimensions() {
  return getRuleContext<MiniCParser::ArrayDimensionsContext>(0);
}

tree::TerminalNode* MiniCParser::VarDefContext::T_ASSIGN() {
  return getToken(MiniCParser::T_ASSIGN, 0);
}

MiniCParser::InitListContext* MiniCParser::VarDefContext::initList() {
  return getRuleContext<MiniCParser::InitListContext>(0);
}

MiniCParser::ExprContext* MiniCParser::VarDefContext::expr() {
  return getRuleContext<MiniCParser::ExprContext>(0);
}


size_t MiniCParser::VarDefContext::getRuleIndex() const {
  return MiniCParser::RuleVarDef;
}


std::any MiniCParser::VarDefContext::accept(tree::ParseTreeVisitor *visitor) {
  if (auto parserVisitor = dynamic_cast<MiniCVisitor*>(visitor))
    return parserVisitor->visitVarDef(this);
  else
    return visitor->visitChildren(this);
}

MiniCParser::VarDefContext* MiniCParser::varDef() {
  VarDefContext *_localctx = _tracker.createInstance<VarDefContext>(_ctx, getState());
  enterRule(_localctx, 20, MiniCParser::RuleVarDef);
  size_t _la = 0;

#if __cplusplus > 201703L
  auto onExit = finally([=, this] {
#else
  auto onExit = finally([=] {
#endif
    exitRule();
  });
  try {
    enterOuterAlt(_localctx, 1);
    setState(112);
    match(MiniCParser::T_ID);
    setState(114);
    _errHandler->sync(this);

    _la = _input->LA(1);
    if (_la == MiniCParser::T_L_BRACKET) {
      setState(113);
      arrayDimensions();
    }
    setState(121);
    _errHandler->sync(this);

    _la = _input->LA(1);
    if (_la == MiniCParser::T_ASSIGN) {
      setState(116);
      match(MiniCParser::T_ASSIGN);
      setState(119);
      _errHandler->sync(this);
      switch (_input->LA(1)) {
        case MiniCParser::T_L_BRACE: {
          setState(117);
          initList();
          break;
        }

        case MiniCParser::T_L_PAREN:
        case MiniCParser::T_SUB:
        case MiniCParser::T_ID:
        case MiniCParser::T_DIGIT: {
          setState(118);
          expr();
          break;
        }

      default:
        throw NoViableAltException(this);
      }
    }
   
  }
  catch (RecognitionException &e) {
    _errHandler->reportError(this, e);
    _localctx->exception = std::current_exception();
    _errHandler->recover(this, _localctx->exception);
  }

  return _localctx;
}

//----------------- StatementContext ------------------------------------------------------------------

MiniCParser::StatementContext::StatementContext(ParserRuleContext *parent, size_t invokingState)
  : ParserRuleContext(parent, invokingState) {
}


size_t MiniCParser::StatementContext::getRuleIndex() const {
  return MiniCParser::RuleStatement;
}

void MiniCParser::StatementContext::copyFrom(StatementContext *ctx) {
  ParserRuleContext::copyFrom(ctx);
}

//----------------- BlockStatementContext ------------------------------------------------------------------

MiniCParser::BlockContext* MiniCParser::BlockStatementContext::block() {
  return getRuleContext<MiniCParser::BlockContext>(0);
}

MiniCParser::BlockStatementContext::BlockStatementContext(StatementContext *ctx) { copyFrom(ctx); }


std::any MiniCParser::BlockStatementContext::accept(tree::ParseTreeVisitor *visitor) {
  if (auto parserVisitor = dynamic_cast<MiniCVisitor*>(visitor))
    return parserVisitor->visitBlockStatement(this);
  else
    return visitor->visitChildren(this);
}
//----------------- AssignStatementContext ------------------------------------------------------------------

MiniCParser::LValContext* MiniCParser::AssignStatementContext::lVal() {
  return getRuleContext<MiniCParser::LValContext>(0);
}

tree::TerminalNode* MiniCParser::AssignStatementContext::T_ASSIGN() {
  return getToken(MiniCParser::T_ASSIGN, 0);
}

MiniCParser::ExprContext* MiniCParser::AssignStatementContext::expr() {
  return getRuleContext<MiniCParser::ExprContext>(0);
}

tree::TerminalNode* MiniCParser::AssignStatementContext::T_SEMICOLON() {
  return getToken(MiniCParser::T_SEMICOLON, 0);
}

MiniCParser::AssignStatementContext::AssignStatementContext(StatementContext *ctx) { copyFrom(ctx); }


std::any MiniCParser::AssignStatementContext::accept(tree::ParseTreeVisitor *visitor) {
  if (auto parserVisitor = dynamic_cast<MiniCVisitor*>(visitor))
    return parserVisitor->visitAssignStatement(this);
  else
    return visitor->visitChildren(this);
}
//----------------- ExpressionStatementContext ------------------------------------------------------------------

tree::TerminalNode* MiniCParser::ExpressionStatementContext::T_SEMICOLON() {
  return getToken(MiniCParser::T_SEMICOLON, 0);
}

MiniCParser::ExprContext* MiniCParser::ExpressionStatementContext::expr() {
  return getRuleContext<MiniCParser::ExprContext>(0);
}

MiniCParser::ExpressionStatementContext::ExpressionStatementContext(StatementContext *ctx) { copyFrom(ctx); }


std::any MiniCParser::ExpressionStatementContext::accept(tree::ParseTreeVisitor *visitor) {
  if (auto parserVisitor = dynamic_cast<MiniCVisitor*>(visitor))
    return parserVisitor->visitExpressionStatement(this);
  else
    return visitor->visitChildren(this);
}
//----------------- ReturnStatementContext ------------------------------------------------------------------

tree::TerminalNode* MiniCParser::ReturnStatementContext::T_RETURN() {
  return getToken(MiniCParser::T_RETURN, 0);
}

MiniCParser::ExprContext* MiniCParser::ReturnStatementContext::expr() {
  return getRuleContext<MiniCParser::ExprContext>(0);
}

tree::TerminalNode* MiniCParser::ReturnStatementContext::T_SEMICOLON() {
  return getToken(MiniCParser::T_SEMICOLON, 0);
}

MiniCParser::ReturnStatementContext::ReturnStatementContext(StatementContext *ctx) { copyFrom(ctx); }


std::any MiniCParser::ReturnStatementContext::accept(tree::ParseTreeVisitor *visitor) {
  if (auto parserVisitor = dynamic_cast<MiniCVisitor*>(visitor))
    return parserVisitor->visitReturnStatement(this);
  else
    return visitor->visitChildren(this);
}
MiniCParser::StatementContext* MiniCParser::statement() {
  StatementContext *_localctx = _tracker.createInstance<StatementContext>(_ctx, getState());
  enterRule(_localctx, 22, MiniCParser::RuleStatement);
  size_t _la = 0;

#if __cplusplus > 201703L
  auto onExit = finally([=, this] {
#else
  auto onExit = finally([=] {
#endif
    exitRule();
  });
  try {
    setState(137);
    _errHandler->sync(this);
    switch (getInterpreter<atn::ParserATNSimulator>()->adaptivePredict(_input, 14, _ctx)) {
    case 1: {
      _localctx = _tracker.createInstance<MiniCParser::ReturnStatementContext>(_localctx);
      enterOuterAlt(_localctx, 1);
      setState(123);
      match(MiniCParser::T_RETURN);
      setState(124);
      expr();
      setState(125);
      match(MiniCParser::T_SEMICOLON);
      break;
    }

    case 2: {
      _localctx = _tracker.createInstance<MiniCParser::AssignStatementContext>(_localctx);
      enterOuterAlt(_localctx, 2);
      setState(127);
      lVal();
      setState(128);
      match(MiniCParser::T_ASSIGN);
      setState(129);
      expr();
      setState(130);
      match(MiniCParser::T_SEMICOLON);
      break;
    }

    case 3: {
      _localctx = _tracker.createInstance<MiniCParser::BlockStatementContext>(_localctx);
      enterOuterAlt(_localctx, 3);
      setState(132);
      block();
      break;
    }

    case 4: {
      _localctx = _tracker.createInstance<MiniCParser::ExpressionStatementContext>(_localctx);
      enterOuterAlt(_localctx, 4);
      setState(134);
      _errHandler->sync(this);

      _la = _input->LA(1);
      if ((((_la & ~ 0x3fULL) == 0) &&
        ((1ULL << _la) & 788482) != 0)) {
        setState(133);
        expr();
      }
      setState(136);
      match(MiniCParser::T_SEMICOLON);
      break;
    }

    default:
      break;
    }
   
  }
  catch (RecognitionException &e) {
    _errHandler->reportError(this, e);
    _localctx->exception = std::current_exception();
    _errHandler->recover(this, _localctx->exception);
  }

  return _localctx;
}

//----------------- ExprContext ------------------------------------------------------------------

MiniCParser::ExprContext::ExprContext(ParserRuleContext *parent, size_t invokingState)
  : ParserRuleContext(parent, invokingState) {
}

MiniCParser::AddExpContext* MiniCParser::ExprContext::addExp() {
  return getRuleContext<MiniCParser::AddExpContext>(0);
}


size_t MiniCParser::ExprContext::getRuleIndex() const {
  return MiniCParser::RuleExpr;
}


std::any MiniCParser::ExprContext::accept(tree::ParseTreeVisitor *visitor) {
  if (auto parserVisitor = dynamic_cast<MiniCVisitor*>(visitor))
    return parserVisitor->visitExpr(this);
  else
    return visitor->visitChildren(this);
}

MiniCParser::ExprContext* MiniCParser::expr() {
  ExprContext *_localctx = _tracker.createInstance<ExprContext>(_ctx, getState());
  enterRule(_localctx, 24, MiniCParser::RuleExpr);

#if __cplusplus > 201703L
  auto onExit = finally([=, this] {
#else
  auto onExit = finally([=] {
#endif
    exitRule();
  });
  try {
    enterOuterAlt(_localctx, 1);
    setState(139);
    addExp();
   
  }
  catch (RecognitionException &e) {
    _errHandler->reportError(this, e);
    _localctx->exception = std::current_exception();
    _errHandler->recover(this, _localctx->exception);
  }

  return _localctx;
}

//----------------- AddExpContext ------------------------------------------------------------------

MiniCParser::AddExpContext::AddExpContext(ParserRuleContext *parent, size_t invokingState)
  : ParserRuleContext(parent, invokingState) {
}

std::vector<MiniCParser::MulExpContext *> MiniCParser::AddExpContext::mulExp() {
  return getRuleContexts<MiniCParser::MulExpContext>();
}

MiniCParser::MulExpContext* MiniCParser::AddExpContext::mulExp(size_t i) {
  return getRuleContext<MiniCParser::MulExpContext>(i);
}

std::vector<MiniCParser::AddOpContext *> MiniCParser::AddExpContext::addOp() {
  return getRuleContexts<MiniCParser::AddOpContext>();
}

MiniCParser::AddOpContext* MiniCParser::AddExpContext::addOp(size_t i) {
  return getRuleContext<MiniCParser::AddOpContext>(i);
}


size_t MiniCParser::AddExpContext::getRuleIndex() const {
  return MiniCParser::RuleAddExp;
}


std::any MiniCParser::AddExpContext::accept(tree::ParseTreeVisitor *visitor) {
  if (auto parserVisitor = dynamic_cast<MiniCVisitor*>(visitor))
    return parserVisitor->visitAddExp(this);
  else
    return visitor->visitChildren(this);
}

MiniCParser::AddExpContext* MiniCParser::addExp() {
  AddExpContext *_localctx = _tracker.createInstance<AddExpContext>(_ctx, getState());
  enterRule(_localctx, 26, MiniCParser::RuleAddExp);
  size_t _la = 0;

#if __cplusplus > 201703L
  auto onExit = finally([=, this] {
#else
  auto onExit = finally([=] {
#endif
    exitRule();
  });
  try {
    enterOuterAlt(_localctx, 1);
    setState(141);
    mulExp();
    setState(147);
    _errHandler->sync(this);
    _la = _input->LA(1);
    while (_la == MiniCParser::T_ADD

    || _la == MiniCParser::T_SUB) {
      setState(142);
      addOp();
      setState(143);
      mulExp();
      setState(149);
      _errHandler->sync(this);
      _la = _input->LA(1);
    }
   
  }
  catch (RecognitionException &e) {
    _errHandler->reportError(this, e);
    _localctx->exception = std::current_exception();
    _errHandler->recover(this, _localctx->exception);
  }

  return _localctx;
}

//----------------- AddOpContext ------------------------------------------------------------------

MiniCParser::AddOpContext::AddOpContext(ParserRuleContext *parent, size_t invokingState)
  : ParserRuleContext(parent, invokingState) {
}

tree::TerminalNode* MiniCParser::AddOpContext::T_ADD() {
  return getToken(MiniCParser::T_ADD, 0);
}

tree::TerminalNode* MiniCParser::AddOpContext::T_SUB() {
  return getToken(MiniCParser::T_SUB, 0);
}


size_t MiniCParser::AddOpContext::getRuleIndex() const {
  return MiniCParser::RuleAddOp;
}


std::any MiniCParser::AddOpContext::accept(tree::ParseTreeVisitor *visitor) {
  if (auto parserVisitor = dynamic_cast<MiniCVisitor*>(visitor))
    return parserVisitor->visitAddOp(this);
  else
    return visitor->visitChildren(this);
}

MiniCParser::AddOpContext* MiniCParser::addOp() {
  AddOpContext *_localctx = _tracker.createInstance<AddOpContext>(_ctx, getState());
  enterRule(_localctx, 28, MiniCParser::RuleAddOp);
  size_t _la = 0;

#if __cplusplus > 201703L
  auto onExit = finally([=, this] {
#else
  auto onExit = finally([=] {
#endif
    exitRule();
  });
  try {
    enterOuterAlt(_localctx, 1);
    setState(150);
    _la = _input->LA(1);
    if (!(_la == MiniCParser::T_ADD

    || _la == MiniCParser::T_SUB)) {
    _errHandler->recoverInline(this);
    }
    else {
      _errHandler->reportMatch(this);
      consume();
    }
   
  }
  catch (RecognitionException &e) {
    _errHandler->reportError(this, e);
    _localctx->exception = std::current_exception();
    _errHandler->recover(this, _localctx->exception);
  }

  return _localctx;
}

//----------------- MulOpContext ------------------------------------------------------------------

MiniCParser::MulOpContext::MulOpContext(ParserRuleContext *parent, size_t invokingState)
  : ParserRuleContext(parent, invokingState) {
}

tree::TerminalNode* MiniCParser::MulOpContext::T_MUL() {
  return getToken(MiniCParser::T_MUL, 0);
}

tree::TerminalNode* MiniCParser::MulOpContext::T_DIV() {
  return getToken(MiniCParser::T_DIV, 0);
}

tree::TerminalNode* MiniCParser::MulOpContext::T_MOD() {
  return getToken(MiniCParser::T_MOD, 0);
}


size_t MiniCParser::MulOpContext::getRuleIndex() const {
  return MiniCParser::RuleMulOp;
}


std::any MiniCParser::MulOpContext::accept(tree::ParseTreeVisitor *visitor) {
  if (auto parserVisitor = dynamic_cast<MiniCVisitor*>(visitor))
    return parserVisitor->visitMulOp(this);
  else
    return visitor->visitChildren(this);
}

MiniCParser::MulOpContext* MiniCParser::mulOp() {
  MulOpContext *_localctx = _tracker.createInstance<MulOpContext>(_ctx, getState());
  enterRule(_localctx, 30, MiniCParser::RuleMulOp);
  size_t _la = 0;

#if __cplusplus > 201703L
  auto onExit = finally([=, this] {
#else
  auto onExit = finally([=] {
#endif
    exitRule();
  });
  try {
    enterOuterAlt(_localctx, 1);
    setState(152);
    _la = _input->LA(1);
    if (!((((_la & ~ 0x3fULL) == 0) &&
      ((1ULL << _la) & 28672) != 0))) {
    _errHandler->recoverInline(this);
    }
    else {
      _errHandler->reportMatch(this);
      consume();
    }
   
  }
  catch (RecognitionException &e) {
    _errHandler->reportError(this, e);
    _localctx->exception = std::current_exception();
    _errHandler->recover(this, _localctx->exception);
  }

  return _localctx;
}

//----------------- MulExpContext ------------------------------------------------------------------

MiniCParser::MulExpContext::MulExpContext(ParserRuleContext *parent, size_t invokingState)
  : ParserRuleContext(parent, invokingState) {
}

std::vector<MiniCParser::UnaryExpContext *> MiniCParser::MulExpContext::unaryExp() {
  return getRuleContexts<MiniCParser::UnaryExpContext>();
}

MiniCParser::UnaryExpContext* MiniCParser::MulExpContext::unaryExp(size_t i) {
  return getRuleContext<MiniCParser::UnaryExpContext>(i);
}

std::vector<MiniCParser::MulOpContext *> MiniCParser::MulExpContext::mulOp() {
  return getRuleContexts<MiniCParser::MulOpContext>();
}

MiniCParser::MulOpContext* MiniCParser::MulExpContext::mulOp(size_t i) {
  return getRuleContext<MiniCParser::MulOpContext>(i);
}


size_t MiniCParser::MulExpContext::getRuleIndex() const {
  return MiniCParser::RuleMulExp;
}


std::any MiniCParser::MulExpContext::accept(tree::ParseTreeVisitor *visitor) {
  if (auto parserVisitor = dynamic_cast<MiniCVisitor*>(visitor))
    return parserVisitor->visitMulExp(this);
  else
    return visitor->visitChildren(this);
}

MiniCParser::MulExpContext* MiniCParser::mulExp() {
  MulExpContext *_localctx = _tracker.createInstance<MulExpContext>(_ctx, getState());
  enterRule(_localctx, 32, MiniCParser::RuleMulExp);
  size_t _la = 0;

#if __cplusplus > 201703L
  auto onExit = finally([=, this] {
#else
  auto onExit = finally([=] {
#endif
    exitRule();
  });
  try {
    enterOuterAlt(_localctx, 1);
    setState(154);
    unaryExp();
    setState(160);
    _errHandler->sync(this);
    _la = _input->LA(1);
    while ((((_la & ~ 0x3fULL) == 0) &&
      ((1ULL << _la) & 28672) != 0)) {
      setState(155);
      mulOp();
      setState(156);
      unaryExp();
      setState(162);
      _errHandler->sync(this);
      _la = _input->LA(1);
    }
   
  }
  catch (RecognitionException &e) {
    _errHandler->reportError(this, e);
    _localctx->exception = std::current_exception();
    _errHandler->recover(this, _localctx->exception);
  }

  return _localctx;
}

//----------------- UnaryOpContext ------------------------------------------------------------------

MiniCParser::UnaryOpContext::UnaryOpContext(ParserRuleContext *parent, size_t invokingState)
  : ParserRuleContext(parent, invokingState) {
}

tree::TerminalNode* MiniCParser::UnaryOpContext::T_SUB() {
  return getToken(MiniCParser::T_SUB, 0);
}


size_t MiniCParser::UnaryOpContext::getRuleIndex() const {
  return MiniCParser::RuleUnaryOp;
}


std::any MiniCParser::UnaryOpContext::accept(tree::ParseTreeVisitor *visitor) {
  if (auto parserVisitor = dynamic_cast<MiniCVisitor*>(visitor))
    return parserVisitor->visitUnaryOp(this);
  else
    return visitor->visitChildren(this);
}

MiniCParser::UnaryOpContext* MiniCParser::unaryOp() {
  UnaryOpContext *_localctx = _tracker.createInstance<UnaryOpContext>(_ctx, getState());
  enterRule(_localctx, 34, MiniCParser::RuleUnaryOp);

#if __cplusplus > 201703L
  auto onExit = finally([=, this] {
#else
  auto onExit = finally([=] {
#endif
    exitRule();
  });
  try {
    enterOuterAlt(_localctx, 1);
    setState(163);
    match(MiniCParser::T_SUB);
   
  }
  catch (RecognitionException &e) {
    _errHandler->reportError(this, e);
    _localctx->exception = std::current_exception();
    _errHandler->recover(this, _localctx->exception);
  }

  return _localctx;
}

//----------------- UnaryExpContext ------------------------------------------------------------------

MiniCParser::UnaryExpContext::UnaryExpContext(ParserRuleContext *parent, size_t invokingState)
  : ParserRuleContext(parent, invokingState) {
}

MiniCParser::PrimaryExpContext* MiniCParser::UnaryExpContext::primaryExp() {
  return getRuleContext<MiniCParser::PrimaryExpContext>(0);
}

std::vector<MiniCParser::UnaryOpContext *> MiniCParser::UnaryExpContext::unaryOp() {
  return getRuleContexts<MiniCParser::UnaryOpContext>();
}

MiniCParser::UnaryOpContext* MiniCParser::UnaryExpContext::unaryOp(size_t i) {
  return getRuleContext<MiniCParser::UnaryOpContext>(i);
}

tree::TerminalNode* MiniCParser::UnaryExpContext::T_ID() {
  return getToken(MiniCParser::T_ID, 0);
}

tree::TerminalNode* MiniCParser::UnaryExpContext::T_L_PAREN() {
  return getToken(MiniCParser::T_L_PAREN, 0);
}

tree::TerminalNode* MiniCParser::UnaryExpContext::T_R_PAREN() {
  return getToken(MiniCParser::T_R_PAREN, 0);
}

MiniCParser::RealParamListContext* MiniCParser::UnaryExpContext::realParamList() {
  return getRuleContext<MiniCParser::RealParamListContext>(0);
}


size_t MiniCParser::UnaryExpContext::getRuleIndex() const {
  return MiniCParser::RuleUnaryExp;
}


std::any MiniCParser::UnaryExpContext::accept(tree::ParseTreeVisitor *visitor) {
  if (auto parserVisitor = dynamic_cast<MiniCVisitor*>(visitor))
    return parserVisitor->visitUnaryExp(this);
  else
    return visitor->visitChildren(this);
}

MiniCParser::UnaryExpContext* MiniCParser::unaryExp() {
  UnaryExpContext *_localctx = _tracker.createInstance<UnaryExpContext>(_ctx, getState());
  enterRule(_localctx, 36, MiniCParser::RuleUnaryExp);
  size_t _la = 0;

#if __cplusplus > 201703L
  auto onExit = finally([=, this] {
#else
  auto onExit = finally([=] {
#endif
    exitRule();
  });
  try {
    setState(178);
    _errHandler->sync(this);
    switch (getInterpreter<atn::ParserATNSimulator>()->adaptivePredict(_input, 19, _ctx)) {
    case 1: {
      enterOuterAlt(_localctx, 1);
      setState(168);
      _errHandler->sync(this);
      _la = _input->LA(1);
      while (_la == MiniCParser::T_SUB) {
        setState(165);
        unaryOp();
        setState(170);
        _errHandler->sync(this);
        _la = _input->LA(1);
      }
      setState(171);
      primaryExp();
      break;
    }

    case 2: {
      enterOuterAlt(_localctx, 2);
      setState(172);
      match(MiniCParser::T_ID);
      setState(173);
      match(MiniCParser::T_L_PAREN);
      setState(175);
      _errHandler->sync(this);

      _la = _input->LA(1);
      if ((((_la & ~ 0x3fULL) == 0) &&
        ((1ULL << _la) & 788482) != 0)) {
        setState(174);
        realParamList();
      }
      setState(177);
      match(MiniCParser::T_R_PAREN);
      break;
    }

    default:
      break;
    }
   
  }
  catch (RecognitionException &e) {
    _errHandler->reportError(this, e);
    _localctx->exception = std::current_exception();
    _errHandler->recover(this, _localctx->exception);
  }

  return _localctx;
}

//----------------- PrimaryExpContext ------------------------------------------------------------------

MiniCParser::PrimaryExpContext::PrimaryExpContext(ParserRuleContext *parent, size_t invokingState)
  : ParserRuleContext(parent, invokingState) {
}

tree::TerminalNode* MiniCParser::PrimaryExpContext::T_L_PAREN() {
  return getToken(MiniCParser::T_L_PAREN, 0);
}

MiniCParser::ExprContext* MiniCParser::PrimaryExpContext::expr() {
  return getRuleContext<MiniCParser::ExprContext>(0);
}

tree::TerminalNode* MiniCParser::PrimaryExpContext::T_R_PAREN() {
  return getToken(MiniCParser::T_R_PAREN, 0);
}

tree::TerminalNode* MiniCParser::PrimaryExpContext::T_DIGIT() {
  return getToken(MiniCParser::T_DIGIT, 0);
}

MiniCParser::LValContext* MiniCParser::PrimaryExpContext::lVal() {
  return getRuleContext<MiniCParser::LValContext>(0);
}


size_t MiniCParser::PrimaryExpContext::getRuleIndex() const {
  return MiniCParser::RulePrimaryExp;
}


std::any MiniCParser::PrimaryExpContext::accept(tree::ParseTreeVisitor *visitor) {
  if (auto parserVisitor = dynamic_cast<MiniCVisitor*>(visitor))
    return parserVisitor->visitPrimaryExp(this);
  else
    return visitor->visitChildren(this);
}

MiniCParser::PrimaryExpContext* MiniCParser::primaryExp() {
  PrimaryExpContext *_localctx = _tracker.createInstance<PrimaryExpContext>(_ctx, getState());
  enterRule(_localctx, 38, MiniCParser::RulePrimaryExp);

#if __cplusplus > 201703L
  auto onExit = finally([=, this] {
#else
  auto onExit = finally([=] {
#endif
    exitRule();
  });
  try {
    setState(186);
    _errHandler->sync(this);
    switch (_input->LA(1)) {
      case MiniCParser::T_L_PAREN: {
        enterOuterAlt(_localctx, 1);
        setState(180);
        match(MiniCParser::T_L_PAREN);
        setState(181);
        expr();
        setState(182);
        match(MiniCParser::T_R_PAREN);
        break;
      }

      case MiniCParser::T_DIGIT: {
        enterOuterAlt(_localctx, 2);
        setState(184);
        match(MiniCParser::T_DIGIT);
        break;
      }

      case MiniCParser::T_ID: {
        enterOuterAlt(_localctx, 3);
        setState(185);
        lVal();
        break;
      }

    default:
      throw NoViableAltException(this);
    }
   
  }
  catch (RecognitionException &e) {
    _errHandler->reportError(this, e);
    _localctx->exception = std::current_exception();
    _errHandler->recover(this, _localctx->exception);
  }

  return _localctx;
}

//----------------- RealParamListContext ------------------------------------------------------------------

MiniCParser::RealParamListContext::RealParamListContext(ParserRuleContext *parent, size_t invokingState)
  : ParserRuleContext(parent, invokingState) {
}

std::vector<MiniCParser::ExprContext *> MiniCParser::RealParamListContext::expr() {
  return getRuleContexts<MiniCParser::ExprContext>();
}

MiniCParser::ExprContext* MiniCParser::RealParamListContext::expr(size_t i) {
  return getRuleContext<MiniCParser::ExprContext>(i);
}

std::vector<tree::TerminalNode *> MiniCParser::RealParamListContext::T_COMMA() {
  return getTokens(MiniCParser::T_COMMA);
}

tree::TerminalNode* MiniCParser::RealParamListContext::T_COMMA(size_t i) {
  return getToken(MiniCParser::T_COMMA, i);
}


size_t MiniCParser::RealParamListContext::getRuleIndex() const {
  return MiniCParser::RuleRealParamList;
}


std::any MiniCParser::RealParamListContext::accept(tree::ParseTreeVisitor *visitor) {
  if (auto parserVisitor = dynamic_cast<MiniCVisitor*>(visitor))
    return parserVisitor->visitRealParamList(this);
  else
    return visitor->visitChildren(this);
}

MiniCParser::RealParamListContext* MiniCParser::realParamList() {
  RealParamListContext *_localctx = _tracker.createInstance<RealParamListContext>(_ctx, getState());
  enterRule(_localctx, 40, MiniCParser::RuleRealParamList);
  size_t _la = 0;

#if __cplusplus > 201703L
  auto onExit = finally([=, this] {
#else
  auto onExit = finally([=] {
#endif
    exitRule();
  });
  try {
    enterOuterAlt(_localctx, 1);
    setState(188);
    expr();
    setState(193);
    _errHandler->sync(this);
    _la = _input->LA(1);
    while (_la == MiniCParser::T_COMMA) {
      setState(189);
      match(MiniCParser::T_COMMA);
      setState(190);
      expr();
      setState(195);
      _errHandler->sync(this);
      _la = _input->LA(1);
    }
   
  }
  catch (RecognitionException &e) {
    _errHandler->reportError(this, e);
    _localctx->exception = std::current_exception();
    _errHandler->recover(this, _localctx->exception);
  }

  return _localctx;
}

//----------------- LValContext ------------------------------------------------------------------

MiniCParser::LValContext::LValContext(ParserRuleContext *parent, size_t invokingState)
  : ParserRuleContext(parent, invokingState) {
}

tree::TerminalNode* MiniCParser::LValContext::T_ID() {
  return getToken(MiniCParser::T_ID, 0);
}

std::vector<tree::TerminalNode *> MiniCParser::LValContext::T_L_BRACKET() {
  return getTokens(MiniCParser::T_L_BRACKET);
}

tree::TerminalNode* MiniCParser::LValContext::T_L_BRACKET(size_t i) {
  return getToken(MiniCParser::T_L_BRACKET, i);
}

std::vector<MiniCParser::ExprContext *> MiniCParser::LValContext::expr() {
  return getRuleContexts<MiniCParser::ExprContext>();
}

MiniCParser::ExprContext* MiniCParser::LValContext::expr(size_t i) {
  return getRuleContext<MiniCParser::ExprContext>(i);
}

std::vector<tree::TerminalNode *> MiniCParser::LValContext::T_R_BRACKET() {
  return getTokens(MiniCParser::T_R_BRACKET);
}

tree::TerminalNode* MiniCParser::LValContext::T_R_BRACKET(size_t i) {
  return getToken(MiniCParser::T_R_BRACKET, i);
}


size_t MiniCParser::LValContext::getRuleIndex() const {
  return MiniCParser::RuleLVal;
}


std::any MiniCParser::LValContext::accept(tree::ParseTreeVisitor *visitor) {
  if (auto parserVisitor = dynamic_cast<MiniCVisitor*>(visitor))
    return parserVisitor->visitLVal(this);
  else
    return visitor->visitChildren(this);
}

MiniCParser::LValContext* MiniCParser::lVal() {
  LValContext *_localctx = _tracker.createInstance<LValContext>(_ctx, getState());
  enterRule(_localctx, 42, MiniCParser::RuleLVal);
  size_t _la = 0;

#if __cplusplus > 201703L
  auto onExit = finally([=, this] {
#else
  auto onExit = finally([=] {
#endif
    exitRule();
  });
  try {
    enterOuterAlt(_localctx, 1);
    setState(196);
    match(MiniCParser::T_ID);
    setState(203);
    _errHandler->sync(this);
    _la = _input->LA(1);
    while (_la == MiniCParser::T_L_BRACKET) {
      setState(197);
      match(MiniCParser::T_L_BRACKET);
      setState(198);
      expr();
      setState(199);
      match(MiniCParser::T_R_BRACKET);
      setState(205);
      _errHandler->sync(this);
      _la = _input->LA(1);
    }
   
  }
  catch (RecognitionException &e) {
    _errHandler->reportError(this, e);
    _localctx->exception = std::current_exception();
    _errHandler->recover(this, _localctx->exception);
  }

  return _localctx;
}

void MiniCParser::initialize() {
  ::antlr4::internal::call_once(minicParserOnceFlag, minicParserInitialize);
}
