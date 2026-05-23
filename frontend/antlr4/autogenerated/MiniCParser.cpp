
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
      "compileUnit", "decl", "funcDef", "funcType", "formalParamList", "formalParam", 
      "formalParamDims", "block", "blockItemList", "blockItem", "constDecl", 
      "varDecl", "constDeclNoSemi", "varDeclNoSemi", "constDef", "basicType", 
      "varDef", "arrayDefDims", "initVal", "statement", "matchedStatement", 
      "unmatchedStatement", "forInit", "forStep", "expr", "cond", "lOrExp", 
      "lAndExp", "eqExp", "eqOp", "relExp", "relOp", "addExp", "addOp", 
      "mulExp", "mulOp", "unaryExp", "unaryOp", "primaryExp", "realParam", 
      "realParamList", "lVal"
    },
    std::vector<std::string>{
      "", "'('", "')'", "'['", "']'", "';'", "'{'", "'}'", "','", "'='", 
      "'=='", "'!='", "'<='", "'>='", "'<'", "'>'", "'+'", "'-'", "'*'", 
      "'/'", "'%'", "'!'", "'&&'", "'||'", "'++'", "'--'", "'if'", "'else'", 
      "'while'", "'for'", "'break'", "'continue'", "'return'", "'const'", 
      "'static'", "'int'", "'float'", "'void'"
    },
    std::vector<std::string>{
      "", "T_L_PAREN", "T_R_PAREN", "T_L_BRACK", "T_R_BRACK", "T_SEMICOLON", 
      "T_L_BRACE", "T_R_BRACE", "T_COMMA", "T_ASSIGN", "T_EQ", "T_NE", "T_LE", 
      "T_GE", "T_LT", "T_GT", "T_ADD", "T_SUB", "T_MUL", "T_DIV", "T_MOD", 
      "T_NOT", "T_LAND", "T_LOR", "T_INC", "T_DEC", "T_IF", "T_ELSE", "T_WHILE", 
      "T_FOR", "T_BREAK", "T_CONTINUE", "T_RETURN", "T_CONST", "T_STATIC", 
      "T_INT", "T_FLOAT", "T_VOID", "T_ID", "T_STRING_LITERAL", "T_FLOAT_LITERAL", 
      "T_DIGIT", "LINE_COMMENT", "BLOCK_COMMENT", "WS"
    }
  );
  static const int32_t serializedATNSegment[] = {
  	4,1,44,440,2,0,7,0,2,1,7,1,2,2,7,2,2,3,7,3,2,4,7,4,2,5,7,5,2,6,7,6,2,
  	7,7,7,2,8,7,8,2,9,7,9,2,10,7,10,2,11,7,11,2,12,7,12,2,13,7,13,2,14,7,
  	14,2,15,7,15,2,16,7,16,2,17,7,17,2,18,7,18,2,19,7,19,2,20,7,20,2,21,7,
  	21,2,22,7,22,2,23,7,23,2,24,7,24,2,25,7,25,2,26,7,26,2,27,7,27,2,28,7,
  	28,2,29,7,29,2,30,7,30,2,31,7,31,2,32,7,32,2,33,7,33,2,34,7,34,2,35,7,
  	35,2,36,7,36,2,37,7,37,2,38,7,38,2,39,7,39,2,40,7,40,2,41,7,41,1,0,1,
  	0,5,0,87,8,0,10,0,12,0,90,9,0,1,0,1,0,1,1,1,1,3,1,96,8,1,1,2,1,2,1,2,
  	1,2,3,2,102,8,2,1,2,1,2,1,2,1,3,1,3,1,4,1,4,1,4,5,4,112,8,4,10,4,12,4,
  	115,9,4,1,5,1,5,1,5,3,5,120,8,5,1,6,1,6,1,6,1,6,1,6,1,6,5,6,128,8,6,10,
  	6,12,6,131,9,6,1,7,1,7,3,7,135,8,7,1,7,1,7,1,8,4,8,140,8,8,11,8,12,8,
  	141,1,9,1,9,3,9,146,8,9,1,10,1,10,1,10,1,11,1,11,1,11,1,12,3,12,155,8,
  	12,1,12,1,12,1,12,1,12,1,12,5,12,162,8,12,10,12,12,12,165,9,12,1,13,3,
  	13,168,8,13,1,13,1,13,1,13,1,13,5,13,174,8,13,10,13,12,13,177,9,13,1,
  	14,1,14,3,14,181,8,14,1,14,1,14,1,14,1,15,1,15,1,16,1,16,3,16,190,8,16,
  	1,16,1,16,3,16,194,8,16,1,17,1,17,1,17,1,17,4,17,200,8,17,11,17,12,17,
  	201,1,18,1,18,1,18,1,18,1,18,5,18,209,8,18,10,18,12,18,212,9,18,3,18,
  	214,8,18,1,18,3,18,217,8,18,1,19,1,19,3,19,221,8,19,1,20,1,20,3,20,225,
  	8,20,1,20,1,20,1,20,1,20,1,20,1,20,1,20,1,20,1,20,1,20,1,20,1,20,1,20,
  	1,20,1,20,1,20,1,20,1,20,1,20,1,20,1,20,1,20,1,20,3,20,250,8,20,1,20,
  	1,20,3,20,254,8,20,1,20,1,20,3,20,258,8,20,1,20,1,20,1,20,1,20,1,20,1,
  	20,1,20,1,20,3,20,268,8,20,1,20,3,20,271,8,20,1,21,1,21,1,21,1,21,1,21,
  	1,21,1,21,1,21,1,21,1,21,1,21,1,21,1,21,1,21,1,21,1,21,1,21,1,21,1,21,
  	1,21,1,21,1,21,1,21,3,21,296,8,21,1,21,1,21,3,21,300,8,21,1,21,1,21,3,
  	21,304,8,21,1,21,1,21,3,21,308,8,21,1,22,1,22,1,22,1,22,1,22,1,22,1,22,
  	3,22,317,8,22,1,23,1,23,1,23,1,23,1,23,3,23,324,8,23,1,24,1,24,1,25,1,
  	25,1,26,1,26,1,26,5,26,333,8,26,10,26,12,26,336,9,26,1,27,1,27,1,27,5,
  	27,341,8,27,10,27,12,27,344,9,27,1,28,1,28,1,28,1,28,5,28,350,8,28,10,
  	28,12,28,353,9,28,1,29,1,29,1,30,1,30,1,30,1,30,5,30,361,8,30,10,30,12,
  	30,364,9,30,1,31,1,31,1,32,1,32,1,32,1,32,5,32,372,8,32,10,32,12,32,375,
  	9,32,1,33,1,33,1,34,1,34,1,34,1,34,5,34,383,8,34,10,34,12,34,386,9,34,
  	1,35,1,35,1,36,1,36,1,36,1,36,3,36,394,8,36,1,36,1,36,1,36,1,36,1,36,
  	1,36,1,36,1,36,1,36,3,36,405,8,36,1,37,1,37,1,38,1,38,1,38,1,38,1,38,
  	1,38,1,38,3,38,416,8,38,1,39,1,39,3,39,420,8,39,1,40,1,40,1,40,5,40,425,
  	8,40,10,40,12,40,428,9,40,1,41,1,41,1,41,1,41,1,41,5,41,435,8,41,10,41,
  	12,41,438,9,41,1,41,0,0,42,0,2,4,6,8,10,12,14,16,18,20,22,24,26,28,30,
  	32,34,36,38,40,42,44,46,48,50,52,54,56,58,60,62,64,66,68,70,72,74,76,
  	78,80,82,0,8,1,0,35,37,1,0,35,36,1,0,10,11,1,0,12,15,1,0,16,17,1,0,18,
  	20,1,0,24,25,2,0,16,17,21,21,459,0,88,1,0,0,0,2,95,1,0,0,0,4,97,1,0,0,
  	0,6,106,1,0,0,0,8,108,1,0,0,0,10,116,1,0,0,0,12,121,1,0,0,0,14,132,1,
  	0,0,0,16,139,1,0,0,0,18,145,1,0,0,0,20,147,1,0,0,0,22,150,1,0,0,0,24,
  	154,1,0,0,0,26,167,1,0,0,0,28,178,1,0,0,0,30,185,1,0,0,0,32,187,1,0,0,
  	0,34,199,1,0,0,0,36,216,1,0,0,0,38,220,1,0,0,0,40,270,1,0,0,0,42,307,
  	1,0,0,0,44,316,1,0,0,0,46,323,1,0,0,0,48,325,1,0,0,0,50,327,1,0,0,0,52,
  	329,1,0,0,0,54,337,1,0,0,0,56,345,1,0,0,0,58,354,1,0,0,0,60,356,1,0,0,
  	0,62,365,1,0,0,0,64,367,1,0,0,0,66,376,1,0,0,0,68,378,1,0,0,0,70,387,
  	1,0,0,0,72,404,1,0,0,0,74,406,1,0,0,0,76,415,1,0,0,0,78,419,1,0,0,0,80,
  	421,1,0,0,0,82,429,1,0,0,0,84,87,3,4,2,0,85,87,3,2,1,0,86,84,1,0,0,0,
  	86,85,1,0,0,0,87,90,1,0,0,0,88,86,1,0,0,0,88,89,1,0,0,0,89,91,1,0,0,0,
  	90,88,1,0,0,0,91,92,5,0,0,1,92,1,1,0,0,0,93,96,3,20,10,0,94,96,3,22,11,
  	0,95,93,1,0,0,0,95,94,1,0,0,0,96,3,1,0,0,0,97,98,3,6,3,0,98,99,5,38,0,
  	0,99,101,5,1,0,0,100,102,3,8,4,0,101,100,1,0,0,0,101,102,1,0,0,0,102,
  	103,1,0,0,0,103,104,5,2,0,0,104,105,3,14,7,0,105,5,1,0,0,0,106,107,7,
  	0,0,0,107,7,1,0,0,0,108,113,3,10,5,0,109,110,5,8,0,0,110,112,3,10,5,0,
  	111,109,1,0,0,0,112,115,1,0,0,0,113,111,1,0,0,0,113,114,1,0,0,0,114,9,
  	1,0,0,0,115,113,1,0,0,0,116,117,3,30,15,0,117,119,5,38,0,0,118,120,3,
  	12,6,0,119,118,1,0,0,0,119,120,1,0,0,0,120,11,1,0,0,0,121,122,5,3,0,0,
  	122,129,5,4,0,0,123,124,5,3,0,0,124,125,3,48,24,0,125,126,5,4,0,0,126,
  	128,1,0,0,0,127,123,1,0,0,0,128,131,1,0,0,0,129,127,1,0,0,0,129,130,1,
  	0,0,0,130,13,1,0,0,0,131,129,1,0,0,0,132,134,5,6,0,0,133,135,3,16,8,0,
  	134,133,1,0,0,0,134,135,1,0,0,0,135,136,1,0,0,0,136,137,5,7,0,0,137,15,
  	1,0,0,0,138,140,3,18,9,0,139,138,1,0,0,0,140,141,1,0,0,0,141,139,1,0,
  	0,0,141,142,1,0,0,0,142,17,1,0,0,0,143,146,3,38,19,0,144,146,3,2,1,0,
  	145,143,1,0,0,0,145,144,1,0,0,0,146,19,1,0,0,0,147,148,3,24,12,0,148,
  	149,5,5,0,0,149,21,1,0,0,0,150,151,3,26,13,0,151,152,5,5,0,0,152,23,1,
  	0,0,0,153,155,5,34,0,0,154,153,1,0,0,0,154,155,1,0,0,0,155,156,1,0,0,
  	0,156,157,5,33,0,0,157,158,3,30,15,0,158,163,3,28,14,0,159,160,5,8,0,
  	0,160,162,3,28,14,0,161,159,1,0,0,0,162,165,1,0,0,0,163,161,1,0,0,0,163,
  	164,1,0,0,0,164,25,1,0,0,0,165,163,1,0,0,0,166,168,5,34,0,0,167,166,1,
  	0,0,0,167,168,1,0,0,0,168,169,1,0,0,0,169,170,3,30,15,0,170,175,3,32,
  	16,0,171,172,5,8,0,0,172,174,3,32,16,0,173,171,1,0,0,0,174,177,1,0,0,
  	0,175,173,1,0,0,0,175,176,1,0,0,0,176,27,1,0,0,0,177,175,1,0,0,0,178,
  	180,5,38,0,0,179,181,3,34,17,0,180,179,1,0,0,0,180,181,1,0,0,0,181,182,
  	1,0,0,0,182,183,5,9,0,0,183,184,3,36,18,0,184,29,1,0,0,0,185,186,7,1,
  	0,0,186,31,1,0,0,0,187,189,5,38,0,0,188,190,3,34,17,0,189,188,1,0,0,0,
  	189,190,1,0,0,0,190,193,1,0,0,0,191,192,5,9,0,0,192,194,3,36,18,0,193,
  	191,1,0,0,0,193,194,1,0,0,0,194,33,1,0,0,0,195,196,5,3,0,0,196,197,3,
  	48,24,0,197,198,5,4,0,0,198,200,1,0,0,0,199,195,1,0,0,0,200,201,1,0,0,
  	0,201,199,1,0,0,0,201,202,1,0,0,0,202,35,1,0,0,0,203,217,3,48,24,0,204,
  	213,5,6,0,0,205,210,3,36,18,0,206,207,5,8,0,0,207,209,3,36,18,0,208,206,
  	1,0,0,0,209,212,1,0,0,0,210,208,1,0,0,0,210,211,1,0,0,0,211,214,1,0,0,
  	0,212,210,1,0,0,0,213,205,1,0,0,0,213,214,1,0,0,0,214,215,1,0,0,0,215,
  	217,5,7,0,0,216,203,1,0,0,0,216,204,1,0,0,0,217,37,1,0,0,0,218,221,3,
  	40,20,0,219,221,3,42,21,0,220,218,1,0,0,0,220,219,1,0,0,0,221,39,1,0,
  	0,0,222,224,5,32,0,0,223,225,3,48,24,0,224,223,1,0,0,0,224,225,1,0,0,
  	0,225,226,1,0,0,0,226,271,5,5,0,0,227,228,3,82,41,0,228,229,5,9,0,0,229,
  	230,3,48,24,0,230,231,5,5,0,0,231,271,1,0,0,0,232,233,5,26,0,0,233,234,
  	5,1,0,0,234,235,3,50,25,0,235,236,5,2,0,0,236,237,3,40,20,0,237,238,5,
  	27,0,0,238,239,3,40,20,0,239,271,1,0,0,0,240,241,5,28,0,0,241,242,5,1,
  	0,0,242,243,3,50,25,0,243,244,5,2,0,0,244,245,3,40,20,0,245,271,1,0,0,
  	0,246,247,5,29,0,0,247,249,5,1,0,0,248,250,3,44,22,0,249,248,1,0,0,0,
  	249,250,1,0,0,0,250,251,1,0,0,0,251,253,5,5,0,0,252,254,3,50,25,0,253,
  	252,1,0,0,0,253,254,1,0,0,0,254,255,1,0,0,0,255,257,5,5,0,0,256,258,3,
  	46,23,0,257,256,1,0,0,0,257,258,1,0,0,0,258,259,1,0,0,0,259,260,5,2,0,
  	0,260,271,3,40,20,0,261,262,5,30,0,0,262,271,5,5,0,0,263,264,5,31,0,0,
  	264,271,5,5,0,0,265,271,3,14,7,0,266,268,3,48,24,0,267,266,1,0,0,0,267,
  	268,1,0,0,0,268,269,1,0,0,0,269,271,5,5,0,0,270,222,1,0,0,0,270,227,1,
  	0,0,0,270,232,1,0,0,0,270,240,1,0,0,0,270,246,1,0,0,0,270,261,1,0,0,0,
  	270,263,1,0,0,0,270,265,1,0,0,0,270,267,1,0,0,0,271,41,1,0,0,0,272,273,
  	5,26,0,0,273,274,5,1,0,0,274,275,3,50,25,0,275,276,5,2,0,0,276,277,3,
  	38,19,0,277,308,1,0,0,0,278,279,5,26,0,0,279,280,5,1,0,0,280,281,3,50,
  	25,0,281,282,5,2,0,0,282,283,3,40,20,0,283,284,5,27,0,0,284,285,3,42,
  	21,0,285,308,1,0,0,0,286,287,5,28,0,0,287,288,5,1,0,0,288,289,3,50,25,
  	0,289,290,5,2,0,0,290,291,3,42,21,0,291,308,1,0,0,0,292,293,5,29,0,0,
  	293,295,5,1,0,0,294,296,3,44,22,0,295,294,1,0,0,0,295,296,1,0,0,0,296,
  	297,1,0,0,0,297,299,5,5,0,0,298,300,3,50,25,0,299,298,1,0,0,0,299,300,
  	1,0,0,0,300,301,1,0,0,0,301,303,5,5,0,0,302,304,3,46,23,0,303,302,1,0,
  	0,0,303,304,1,0,0,0,304,305,1,0,0,0,305,306,5,2,0,0,306,308,3,42,21,0,
  	307,272,1,0,0,0,307,278,1,0,0,0,307,286,1,0,0,0,307,292,1,0,0,0,308,43,
  	1,0,0,0,309,317,3,24,12,0,310,317,3,26,13,0,311,312,3,82,41,0,312,313,
  	5,9,0,0,313,314,3,48,24,0,314,317,1,0,0,0,315,317,3,48,24,0,316,309,1,
  	0,0,0,316,310,1,0,0,0,316,311,1,0,0,0,316,315,1,0,0,0,317,45,1,0,0,0,
  	318,319,3,82,41,0,319,320,5,9,0,0,320,321,3,48,24,0,321,324,1,0,0,0,322,
  	324,3,48,24,0,323,318,1,0,0,0,323,322,1,0,0,0,324,47,1,0,0,0,325,326,
  	3,52,26,0,326,49,1,0,0,0,327,328,3,48,24,0,328,51,1,0,0,0,329,334,3,54,
  	27,0,330,331,5,23,0,0,331,333,3,54,27,0,332,330,1,0,0,0,333,336,1,0,0,
  	0,334,332,1,0,0,0,334,335,1,0,0,0,335,53,1,0,0,0,336,334,1,0,0,0,337,
  	342,3,56,28,0,338,339,5,22,0,0,339,341,3,56,28,0,340,338,1,0,0,0,341,
  	344,1,0,0,0,342,340,1,0,0,0,342,343,1,0,0,0,343,55,1,0,0,0,344,342,1,
  	0,0,0,345,351,3,60,30,0,346,347,3,58,29,0,347,348,3,60,30,0,348,350,1,
  	0,0,0,349,346,1,0,0,0,350,353,1,0,0,0,351,349,1,0,0,0,351,352,1,0,0,0,
  	352,57,1,0,0,0,353,351,1,0,0,0,354,355,7,2,0,0,355,59,1,0,0,0,356,362,
  	3,64,32,0,357,358,3,62,31,0,358,359,3,64,32,0,359,361,1,0,0,0,360,357,
  	1,0,0,0,361,364,1,0,0,0,362,360,1,0,0,0,362,363,1,0,0,0,363,61,1,0,0,
  	0,364,362,1,0,0,0,365,366,7,3,0,0,366,63,1,0,0,0,367,373,3,68,34,0,368,
  	369,3,66,33,0,369,370,3,68,34,0,370,372,1,0,0,0,371,368,1,0,0,0,372,375,
  	1,0,0,0,373,371,1,0,0,0,373,374,1,0,0,0,374,65,1,0,0,0,375,373,1,0,0,
  	0,376,377,7,4,0,0,377,67,1,0,0,0,378,384,3,72,36,0,379,380,3,70,35,0,
  	380,381,3,72,36,0,381,383,1,0,0,0,382,379,1,0,0,0,383,386,1,0,0,0,384,
  	382,1,0,0,0,384,385,1,0,0,0,385,69,1,0,0,0,386,384,1,0,0,0,387,388,7,
  	5,0,0,388,71,1,0,0,0,389,405,3,76,38,0,390,391,5,38,0,0,391,393,5,1,0,
  	0,392,394,3,80,40,0,393,392,1,0,0,0,393,394,1,0,0,0,394,395,1,0,0,0,395,
  	405,5,2,0,0,396,397,3,74,37,0,397,398,3,72,36,0,398,405,1,0,0,0,399,400,
  	7,6,0,0,400,405,3,82,41,0,401,402,3,82,41,0,402,403,7,6,0,0,403,405,1,
  	0,0,0,404,389,1,0,0,0,404,390,1,0,0,0,404,396,1,0,0,0,404,399,1,0,0,0,
  	404,401,1,0,0,0,405,73,1,0,0,0,406,407,7,7,0,0,407,75,1,0,0,0,408,409,
  	5,1,0,0,409,410,3,48,24,0,410,411,5,2,0,0,411,416,1,0,0,0,412,416,5,40,
  	0,0,413,416,5,41,0,0,414,416,3,82,41,0,415,408,1,0,0,0,415,412,1,0,0,
  	0,415,413,1,0,0,0,415,414,1,0,0,0,416,77,1,0,0,0,417,420,3,48,24,0,418,
  	420,5,39,0,0,419,417,1,0,0,0,419,418,1,0,0,0,420,79,1,0,0,0,421,426,3,
  	78,39,0,422,423,5,8,0,0,423,425,3,78,39,0,424,422,1,0,0,0,425,428,1,0,
  	0,0,426,424,1,0,0,0,426,427,1,0,0,0,427,81,1,0,0,0,428,426,1,0,0,0,429,
  	436,5,38,0,0,430,431,5,3,0,0,431,432,3,48,24,0,432,433,5,4,0,0,433,435,
  	1,0,0,0,434,430,1,0,0,0,435,438,1,0,0,0,436,434,1,0,0,0,436,437,1,0,0,
  	0,437,83,1,0,0,0,438,436,1,0,0,0,46,86,88,95,101,113,119,129,134,141,
  	145,154,163,167,175,180,189,193,201,210,213,216,220,224,249,253,257,267,
  	270,295,299,303,307,316,323,334,342,351,362,373,384,393,404,415,419,426,
  	436
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

std::vector<MiniCParser::DeclContext *> MiniCParser::CompileUnitContext::decl() {
  return getRuleContexts<MiniCParser::DeclContext>();
}

MiniCParser::DeclContext* MiniCParser::CompileUnitContext::decl(size_t i) {
  return getRuleContext<MiniCParser::DeclContext>(i);
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
    setState(88);
    _errHandler->sync(this);
    _la = _input->LA(1);
    while ((((_la & ~ 0x3fULL) == 0) &&
      ((1ULL << _la) & 266287972352) != 0)) {
      setState(86);
      _errHandler->sync(this);
      switch (getInterpreter<atn::ParserATNSimulator>()->adaptivePredict(_input, 0, _ctx)) {
      case 1: {
        setState(84);
        funcDef();
        break;
      }

      case 2: {
        setState(85);
        decl();
        break;
      }

      default:
        break;
      }
      setState(90);
      _errHandler->sync(this);
      _la = _input->LA(1);
    }
    setState(91);
    match(MiniCParser::EOF);
   
  }
  catch (RecognitionException &e) {
    _errHandler->reportError(this, e);
    _localctx->exception = std::current_exception();
    _errHandler->recover(this, _localctx->exception);
  }

  return _localctx;
}

//----------------- DeclContext ------------------------------------------------------------------

MiniCParser::DeclContext::DeclContext(ParserRuleContext *parent, size_t invokingState)
  : ParserRuleContext(parent, invokingState) {
}

MiniCParser::ConstDeclContext* MiniCParser::DeclContext::constDecl() {
  return getRuleContext<MiniCParser::ConstDeclContext>(0);
}

MiniCParser::VarDeclContext* MiniCParser::DeclContext::varDecl() {
  return getRuleContext<MiniCParser::VarDeclContext>(0);
}


size_t MiniCParser::DeclContext::getRuleIndex() const {
  return MiniCParser::RuleDecl;
}


std::any MiniCParser::DeclContext::accept(tree::ParseTreeVisitor *visitor) {
  if (auto parserVisitor = dynamic_cast<MiniCVisitor*>(visitor))
    return parserVisitor->visitDecl(this);
  else
    return visitor->visitChildren(this);
}

MiniCParser::DeclContext* MiniCParser::decl() {
  DeclContext *_localctx = _tracker.createInstance<DeclContext>(_ctx, getState());
  enterRule(_localctx, 2, MiniCParser::RuleDecl);

#if __cplusplus > 201703L
  auto onExit = finally([=, this] {
#else
  auto onExit = finally([=] {
#endif
    exitRule();
  });
  try {
    setState(95);
    _errHandler->sync(this);
    switch (getInterpreter<atn::ParserATNSimulator>()->adaptivePredict(_input, 2, _ctx)) {
    case 1: {
      enterOuterAlt(_localctx, 1);
      setState(93);
      constDecl();
      break;
    }

    case 2: {
      enterOuterAlt(_localctx, 2);
      setState(94);
      varDecl();
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

//----------------- FuncDefContext ------------------------------------------------------------------

MiniCParser::FuncDefContext::FuncDefContext(ParserRuleContext *parent, size_t invokingState)
  : ParserRuleContext(parent, invokingState) {
}

MiniCParser::FuncTypeContext* MiniCParser::FuncDefContext::funcType() {
  return getRuleContext<MiniCParser::FuncTypeContext>(0);
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

MiniCParser::FormalParamListContext* MiniCParser::FuncDefContext::formalParamList() {
  return getRuleContext<MiniCParser::FormalParamListContext>(0);
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
  enterRule(_localctx, 4, MiniCParser::RuleFuncDef);
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
    setState(97);
    funcType();
    setState(98);
    match(MiniCParser::T_ID);
    setState(99);
    match(MiniCParser::T_L_PAREN);
    setState(101);
    _errHandler->sync(this);

    _la = _input->LA(1);
    if (_la == MiniCParser::T_INT

    || _la == MiniCParser::T_FLOAT) {
      setState(100);
      formalParamList();
    }
    setState(103);
    match(MiniCParser::T_R_PAREN);
    setState(104);
    block();
   
  }
  catch (RecognitionException &e) {
    _errHandler->reportError(this, e);
    _localctx->exception = std::current_exception();
    _errHandler->recover(this, _localctx->exception);
  }

  return _localctx;
}

//----------------- FuncTypeContext ------------------------------------------------------------------

MiniCParser::FuncTypeContext::FuncTypeContext(ParserRuleContext *parent, size_t invokingState)
  : ParserRuleContext(parent, invokingState) {
}

tree::TerminalNode* MiniCParser::FuncTypeContext::T_INT() {
  return getToken(MiniCParser::T_INT, 0);
}

tree::TerminalNode* MiniCParser::FuncTypeContext::T_FLOAT() {
  return getToken(MiniCParser::T_FLOAT, 0);
}

tree::TerminalNode* MiniCParser::FuncTypeContext::T_VOID() {
  return getToken(MiniCParser::T_VOID, 0);
}


size_t MiniCParser::FuncTypeContext::getRuleIndex() const {
  return MiniCParser::RuleFuncType;
}


std::any MiniCParser::FuncTypeContext::accept(tree::ParseTreeVisitor *visitor) {
  if (auto parserVisitor = dynamic_cast<MiniCVisitor*>(visitor))
    return parserVisitor->visitFuncType(this);
  else
    return visitor->visitChildren(this);
}

MiniCParser::FuncTypeContext* MiniCParser::funcType() {
  FuncTypeContext *_localctx = _tracker.createInstance<FuncTypeContext>(_ctx, getState());
  enterRule(_localctx, 6, MiniCParser::RuleFuncType);
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
    setState(106);
    _la = _input->LA(1);
    if (!((((_la & ~ 0x3fULL) == 0) &&
      ((1ULL << _la) & 240518168576) != 0))) {
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

//----------------- FormalParamListContext ------------------------------------------------------------------

MiniCParser::FormalParamListContext::FormalParamListContext(ParserRuleContext *parent, size_t invokingState)
  : ParserRuleContext(parent, invokingState) {
}

std::vector<MiniCParser::FormalParamContext *> MiniCParser::FormalParamListContext::formalParam() {
  return getRuleContexts<MiniCParser::FormalParamContext>();
}

MiniCParser::FormalParamContext* MiniCParser::FormalParamListContext::formalParam(size_t i) {
  return getRuleContext<MiniCParser::FormalParamContext>(i);
}

std::vector<tree::TerminalNode *> MiniCParser::FormalParamListContext::T_COMMA() {
  return getTokens(MiniCParser::T_COMMA);
}

tree::TerminalNode* MiniCParser::FormalParamListContext::T_COMMA(size_t i) {
  return getToken(MiniCParser::T_COMMA, i);
}


size_t MiniCParser::FormalParamListContext::getRuleIndex() const {
  return MiniCParser::RuleFormalParamList;
}


std::any MiniCParser::FormalParamListContext::accept(tree::ParseTreeVisitor *visitor) {
  if (auto parserVisitor = dynamic_cast<MiniCVisitor*>(visitor))
    return parserVisitor->visitFormalParamList(this);
  else
    return visitor->visitChildren(this);
}

MiniCParser::FormalParamListContext* MiniCParser::formalParamList() {
  FormalParamListContext *_localctx = _tracker.createInstance<FormalParamListContext>(_ctx, getState());
  enterRule(_localctx, 8, MiniCParser::RuleFormalParamList);
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
    setState(108);
    formalParam();
    setState(113);
    _errHandler->sync(this);
    _la = _input->LA(1);
    while (_la == MiniCParser::T_COMMA) {
      setState(109);
      match(MiniCParser::T_COMMA);
      setState(110);
      formalParam();
      setState(115);
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

//----------------- FormalParamContext ------------------------------------------------------------------

MiniCParser::FormalParamContext::FormalParamContext(ParserRuleContext *parent, size_t invokingState)
  : ParserRuleContext(parent, invokingState) {
}

MiniCParser::BasicTypeContext* MiniCParser::FormalParamContext::basicType() {
  return getRuleContext<MiniCParser::BasicTypeContext>(0);
}

tree::TerminalNode* MiniCParser::FormalParamContext::T_ID() {
  return getToken(MiniCParser::T_ID, 0);
}

MiniCParser::FormalParamDimsContext* MiniCParser::FormalParamContext::formalParamDims() {
  return getRuleContext<MiniCParser::FormalParamDimsContext>(0);
}


size_t MiniCParser::FormalParamContext::getRuleIndex() const {
  return MiniCParser::RuleFormalParam;
}


std::any MiniCParser::FormalParamContext::accept(tree::ParseTreeVisitor *visitor) {
  if (auto parserVisitor = dynamic_cast<MiniCVisitor*>(visitor))
    return parserVisitor->visitFormalParam(this);
  else
    return visitor->visitChildren(this);
}

MiniCParser::FormalParamContext* MiniCParser::formalParam() {
  FormalParamContext *_localctx = _tracker.createInstance<FormalParamContext>(_ctx, getState());
  enterRule(_localctx, 10, MiniCParser::RuleFormalParam);
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
    setState(116);
    basicType();
    setState(117);
    match(MiniCParser::T_ID);
    setState(119);
    _errHandler->sync(this);

    _la = _input->LA(1);
    if (_la == MiniCParser::T_L_BRACK) {
      setState(118);
      formalParamDims();
    }
   
  }
  catch (RecognitionException &e) {
    _errHandler->reportError(this, e);
    _localctx->exception = std::current_exception();
    _errHandler->recover(this, _localctx->exception);
  }

  return _localctx;
}

//----------------- FormalParamDimsContext ------------------------------------------------------------------

MiniCParser::FormalParamDimsContext::FormalParamDimsContext(ParserRuleContext *parent, size_t invokingState)
  : ParserRuleContext(parent, invokingState) {
}

std::vector<tree::TerminalNode *> MiniCParser::FormalParamDimsContext::T_L_BRACK() {
  return getTokens(MiniCParser::T_L_BRACK);
}

tree::TerminalNode* MiniCParser::FormalParamDimsContext::T_L_BRACK(size_t i) {
  return getToken(MiniCParser::T_L_BRACK, i);
}

std::vector<tree::TerminalNode *> MiniCParser::FormalParamDimsContext::T_R_BRACK() {
  return getTokens(MiniCParser::T_R_BRACK);
}

tree::TerminalNode* MiniCParser::FormalParamDimsContext::T_R_BRACK(size_t i) {
  return getToken(MiniCParser::T_R_BRACK, i);
}

std::vector<MiniCParser::ExprContext *> MiniCParser::FormalParamDimsContext::expr() {
  return getRuleContexts<MiniCParser::ExprContext>();
}

MiniCParser::ExprContext* MiniCParser::FormalParamDimsContext::expr(size_t i) {
  return getRuleContext<MiniCParser::ExprContext>(i);
}


size_t MiniCParser::FormalParamDimsContext::getRuleIndex() const {
  return MiniCParser::RuleFormalParamDims;
}


std::any MiniCParser::FormalParamDimsContext::accept(tree::ParseTreeVisitor *visitor) {
  if (auto parserVisitor = dynamic_cast<MiniCVisitor*>(visitor))
    return parserVisitor->visitFormalParamDims(this);
  else
    return visitor->visitChildren(this);
}

MiniCParser::FormalParamDimsContext* MiniCParser::formalParamDims() {
  FormalParamDimsContext *_localctx = _tracker.createInstance<FormalParamDimsContext>(_ctx, getState());
  enterRule(_localctx, 12, MiniCParser::RuleFormalParamDims);
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
    setState(121);
    match(MiniCParser::T_L_BRACK);
    setState(122);
    match(MiniCParser::T_R_BRACK);
    setState(129);
    _errHandler->sync(this);
    _la = _input->LA(1);
    while (_la == MiniCParser::T_L_BRACK) {
      setState(123);
      match(MiniCParser::T_L_BRACK);
      setState(124);
      expr();
      setState(125);
      match(MiniCParser::T_R_BRACK);
      setState(131);
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
  enterRule(_localctx, 14, MiniCParser::RuleBlock);
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
    setState(132);
    match(MiniCParser::T_L_BRACE);
    setState(134);
    _errHandler->sync(this);

    _la = _input->LA(1);
    if ((((_la & ~ 0x3fULL) == 0) &&
      ((1ULL << _la) & 3710703042658) != 0)) {
      setState(133);
      blockItemList();
    }
    setState(136);
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
  enterRule(_localctx, 16, MiniCParser::RuleBlockItemList);
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
    setState(139); 
    _errHandler->sync(this);
    _la = _input->LA(1);
    do {
      setState(138);
      blockItem();
      setState(141); 
      _errHandler->sync(this);
      _la = _input->LA(1);
    } while ((((_la & ~ 0x3fULL) == 0) &&
      ((1ULL << _la) & 3710703042658) != 0));
   
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

MiniCParser::DeclContext* MiniCParser::BlockItemContext::decl() {
  return getRuleContext<MiniCParser::DeclContext>(0);
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
  enterRule(_localctx, 18, MiniCParser::RuleBlockItem);

#if __cplusplus > 201703L
  auto onExit = finally([=, this] {
#else
  auto onExit = finally([=] {
#endif
    exitRule();
  });
  try {
    setState(145);
    _errHandler->sync(this);
    switch (_input->LA(1)) {
      case MiniCParser::T_L_PAREN:
      case MiniCParser::T_SEMICOLON:
      case MiniCParser::T_L_BRACE:
      case MiniCParser::T_ADD:
      case MiniCParser::T_SUB:
      case MiniCParser::T_NOT:
      case MiniCParser::T_INC:
      case MiniCParser::T_DEC:
      case MiniCParser::T_IF:
      case MiniCParser::T_WHILE:
      case MiniCParser::T_FOR:
      case MiniCParser::T_BREAK:
      case MiniCParser::T_CONTINUE:
      case MiniCParser::T_RETURN:
      case MiniCParser::T_ID:
      case MiniCParser::T_FLOAT_LITERAL:
      case MiniCParser::T_DIGIT: {
        enterOuterAlt(_localctx, 1);
        setState(143);
        statement();
        break;
      }

      case MiniCParser::T_CONST:
      case MiniCParser::T_STATIC:
      case MiniCParser::T_INT:
      case MiniCParser::T_FLOAT: {
        enterOuterAlt(_localctx, 2);
        setState(144);
        decl();
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

//----------------- ConstDeclContext ------------------------------------------------------------------

MiniCParser::ConstDeclContext::ConstDeclContext(ParserRuleContext *parent, size_t invokingState)
  : ParserRuleContext(parent, invokingState) {
}

MiniCParser::ConstDeclNoSemiContext* MiniCParser::ConstDeclContext::constDeclNoSemi() {
  return getRuleContext<MiniCParser::ConstDeclNoSemiContext>(0);
}

tree::TerminalNode* MiniCParser::ConstDeclContext::T_SEMICOLON() {
  return getToken(MiniCParser::T_SEMICOLON, 0);
}


size_t MiniCParser::ConstDeclContext::getRuleIndex() const {
  return MiniCParser::RuleConstDecl;
}


std::any MiniCParser::ConstDeclContext::accept(tree::ParseTreeVisitor *visitor) {
  if (auto parserVisitor = dynamic_cast<MiniCVisitor*>(visitor))
    return parserVisitor->visitConstDecl(this);
  else
    return visitor->visitChildren(this);
}

MiniCParser::ConstDeclContext* MiniCParser::constDecl() {
  ConstDeclContext *_localctx = _tracker.createInstance<ConstDeclContext>(_ctx, getState());
  enterRule(_localctx, 20, MiniCParser::RuleConstDecl);

#if __cplusplus > 201703L
  auto onExit = finally([=, this] {
#else
  auto onExit = finally([=] {
#endif
    exitRule();
  });
  try {
    enterOuterAlt(_localctx, 1);
    setState(147);
    constDeclNoSemi();
    setState(148);
    match(MiniCParser::T_SEMICOLON);
   
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

MiniCParser::VarDeclNoSemiContext* MiniCParser::VarDeclContext::varDeclNoSemi() {
  return getRuleContext<MiniCParser::VarDeclNoSemiContext>(0);
}

tree::TerminalNode* MiniCParser::VarDeclContext::T_SEMICOLON() {
  return getToken(MiniCParser::T_SEMICOLON, 0);
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
  enterRule(_localctx, 22, MiniCParser::RuleVarDecl);

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
    varDeclNoSemi();
    setState(151);
    match(MiniCParser::T_SEMICOLON);
   
  }
  catch (RecognitionException &e) {
    _errHandler->reportError(this, e);
    _localctx->exception = std::current_exception();
    _errHandler->recover(this, _localctx->exception);
  }

  return _localctx;
}

//----------------- ConstDeclNoSemiContext ------------------------------------------------------------------

MiniCParser::ConstDeclNoSemiContext::ConstDeclNoSemiContext(ParserRuleContext *parent, size_t invokingState)
  : ParserRuleContext(parent, invokingState) {
}

tree::TerminalNode* MiniCParser::ConstDeclNoSemiContext::T_CONST() {
  return getToken(MiniCParser::T_CONST, 0);
}

MiniCParser::BasicTypeContext* MiniCParser::ConstDeclNoSemiContext::basicType() {
  return getRuleContext<MiniCParser::BasicTypeContext>(0);
}

std::vector<MiniCParser::ConstDefContext *> MiniCParser::ConstDeclNoSemiContext::constDef() {
  return getRuleContexts<MiniCParser::ConstDefContext>();
}

MiniCParser::ConstDefContext* MiniCParser::ConstDeclNoSemiContext::constDef(size_t i) {
  return getRuleContext<MiniCParser::ConstDefContext>(i);
}

tree::TerminalNode* MiniCParser::ConstDeclNoSemiContext::T_STATIC() {
  return getToken(MiniCParser::T_STATIC, 0);
}

std::vector<tree::TerminalNode *> MiniCParser::ConstDeclNoSemiContext::T_COMMA() {
  return getTokens(MiniCParser::T_COMMA);
}

tree::TerminalNode* MiniCParser::ConstDeclNoSemiContext::T_COMMA(size_t i) {
  return getToken(MiniCParser::T_COMMA, i);
}


size_t MiniCParser::ConstDeclNoSemiContext::getRuleIndex() const {
  return MiniCParser::RuleConstDeclNoSemi;
}


std::any MiniCParser::ConstDeclNoSemiContext::accept(tree::ParseTreeVisitor *visitor) {
  if (auto parserVisitor = dynamic_cast<MiniCVisitor*>(visitor))
    return parserVisitor->visitConstDeclNoSemi(this);
  else
    return visitor->visitChildren(this);
}

MiniCParser::ConstDeclNoSemiContext* MiniCParser::constDeclNoSemi() {
  ConstDeclNoSemiContext *_localctx = _tracker.createInstance<ConstDeclNoSemiContext>(_ctx, getState());
  enterRule(_localctx, 24, MiniCParser::RuleConstDeclNoSemi);
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
    _errHandler->sync(this);

    _la = _input->LA(1);
    if (_la == MiniCParser::T_STATIC) {
      setState(153);
      match(MiniCParser::T_STATIC);
    }
    setState(156);
    match(MiniCParser::T_CONST);
    setState(157);
    basicType();
    setState(158);
    constDef();
    setState(163);
    _errHandler->sync(this);
    _la = _input->LA(1);
    while (_la == MiniCParser::T_COMMA) {
      setState(159);
      match(MiniCParser::T_COMMA);
      setState(160);
      constDef();
      setState(165);
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

//----------------- VarDeclNoSemiContext ------------------------------------------------------------------

MiniCParser::VarDeclNoSemiContext::VarDeclNoSemiContext(ParserRuleContext *parent, size_t invokingState)
  : ParserRuleContext(parent, invokingState) {
}

MiniCParser::BasicTypeContext* MiniCParser::VarDeclNoSemiContext::basicType() {
  return getRuleContext<MiniCParser::BasicTypeContext>(0);
}

std::vector<MiniCParser::VarDefContext *> MiniCParser::VarDeclNoSemiContext::varDef() {
  return getRuleContexts<MiniCParser::VarDefContext>();
}

MiniCParser::VarDefContext* MiniCParser::VarDeclNoSemiContext::varDef(size_t i) {
  return getRuleContext<MiniCParser::VarDefContext>(i);
}

tree::TerminalNode* MiniCParser::VarDeclNoSemiContext::T_STATIC() {
  return getToken(MiniCParser::T_STATIC, 0);
}

std::vector<tree::TerminalNode *> MiniCParser::VarDeclNoSemiContext::T_COMMA() {
  return getTokens(MiniCParser::T_COMMA);
}

tree::TerminalNode* MiniCParser::VarDeclNoSemiContext::T_COMMA(size_t i) {
  return getToken(MiniCParser::T_COMMA, i);
}


size_t MiniCParser::VarDeclNoSemiContext::getRuleIndex() const {
  return MiniCParser::RuleVarDeclNoSemi;
}


std::any MiniCParser::VarDeclNoSemiContext::accept(tree::ParseTreeVisitor *visitor) {
  if (auto parserVisitor = dynamic_cast<MiniCVisitor*>(visitor))
    return parserVisitor->visitVarDeclNoSemi(this);
  else
    return visitor->visitChildren(this);
}

MiniCParser::VarDeclNoSemiContext* MiniCParser::varDeclNoSemi() {
  VarDeclNoSemiContext *_localctx = _tracker.createInstance<VarDeclNoSemiContext>(_ctx, getState());
  enterRule(_localctx, 26, MiniCParser::RuleVarDeclNoSemi);
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
    setState(167);
    _errHandler->sync(this);

    _la = _input->LA(1);
    if (_la == MiniCParser::T_STATIC) {
      setState(166);
      match(MiniCParser::T_STATIC);
    }
    setState(169);
    basicType();
    setState(170);
    varDef();
    setState(175);
    _errHandler->sync(this);
    _la = _input->LA(1);
    while (_la == MiniCParser::T_COMMA) {
      setState(171);
      match(MiniCParser::T_COMMA);
      setState(172);
      varDef();
      setState(177);
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

//----------------- ConstDefContext ------------------------------------------------------------------

MiniCParser::ConstDefContext::ConstDefContext(ParserRuleContext *parent, size_t invokingState)
  : ParserRuleContext(parent, invokingState) {
}

tree::TerminalNode* MiniCParser::ConstDefContext::T_ID() {
  return getToken(MiniCParser::T_ID, 0);
}

tree::TerminalNode* MiniCParser::ConstDefContext::T_ASSIGN() {
  return getToken(MiniCParser::T_ASSIGN, 0);
}

MiniCParser::InitValContext* MiniCParser::ConstDefContext::initVal() {
  return getRuleContext<MiniCParser::InitValContext>(0);
}

MiniCParser::ArrayDefDimsContext* MiniCParser::ConstDefContext::arrayDefDims() {
  return getRuleContext<MiniCParser::ArrayDefDimsContext>(0);
}


size_t MiniCParser::ConstDefContext::getRuleIndex() const {
  return MiniCParser::RuleConstDef;
}


std::any MiniCParser::ConstDefContext::accept(tree::ParseTreeVisitor *visitor) {
  if (auto parserVisitor = dynamic_cast<MiniCVisitor*>(visitor))
    return parserVisitor->visitConstDef(this);
  else
    return visitor->visitChildren(this);
}

MiniCParser::ConstDefContext* MiniCParser::constDef() {
  ConstDefContext *_localctx = _tracker.createInstance<ConstDefContext>(_ctx, getState());
  enterRule(_localctx, 28, MiniCParser::RuleConstDef);
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
    setState(178);
    match(MiniCParser::T_ID);
    setState(180);
    _errHandler->sync(this);

    _la = _input->LA(1);
    if (_la == MiniCParser::T_L_BRACK) {
      setState(179);
      arrayDefDims();
    }
    setState(182);
    match(MiniCParser::T_ASSIGN);
    setState(183);
    initVal();
   
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

tree::TerminalNode* MiniCParser::BasicTypeContext::T_FLOAT() {
  return getToken(MiniCParser::T_FLOAT, 0);
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
  enterRule(_localctx, 30, MiniCParser::RuleBasicType);
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
    setState(185);
    _la = _input->LA(1);
    if (!(_la == MiniCParser::T_INT

    || _la == MiniCParser::T_FLOAT)) {
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

//----------------- VarDefContext ------------------------------------------------------------------

MiniCParser::VarDefContext::VarDefContext(ParserRuleContext *parent, size_t invokingState)
  : ParserRuleContext(parent, invokingState) {
}

tree::TerminalNode* MiniCParser::VarDefContext::T_ID() {
  return getToken(MiniCParser::T_ID, 0);
}

MiniCParser::ArrayDefDimsContext* MiniCParser::VarDefContext::arrayDefDims() {
  return getRuleContext<MiniCParser::ArrayDefDimsContext>(0);
}

tree::TerminalNode* MiniCParser::VarDefContext::T_ASSIGN() {
  return getToken(MiniCParser::T_ASSIGN, 0);
}

MiniCParser::InitValContext* MiniCParser::VarDefContext::initVal() {
  return getRuleContext<MiniCParser::InitValContext>(0);
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
  enterRule(_localctx, 32, MiniCParser::RuleVarDef);
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
    setState(187);
    match(MiniCParser::T_ID);
    setState(189);
    _errHandler->sync(this);

    _la = _input->LA(1);
    if (_la == MiniCParser::T_L_BRACK) {
      setState(188);
      arrayDefDims();
    }
    setState(193);
    _errHandler->sync(this);

    _la = _input->LA(1);
    if (_la == MiniCParser::T_ASSIGN) {
      setState(191);
      match(MiniCParser::T_ASSIGN);
      setState(192);
      initVal();
    }
   
  }
  catch (RecognitionException &e) {
    _errHandler->reportError(this, e);
    _localctx->exception = std::current_exception();
    _errHandler->recover(this, _localctx->exception);
  }

  return _localctx;
}

//----------------- ArrayDefDimsContext ------------------------------------------------------------------

MiniCParser::ArrayDefDimsContext::ArrayDefDimsContext(ParserRuleContext *parent, size_t invokingState)
  : ParserRuleContext(parent, invokingState) {
}

std::vector<tree::TerminalNode *> MiniCParser::ArrayDefDimsContext::T_L_BRACK() {
  return getTokens(MiniCParser::T_L_BRACK);
}

tree::TerminalNode* MiniCParser::ArrayDefDimsContext::T_L_BRACK(size_t i) {
  return getToken(MiniCParser::T_L_BRACK, i);
}

std::vector<MiniCParser::ExprContext *> MiniCParser::ArrayDefDimsContext::expr() {
  return getRuleContexts<MiniCParser::ExprContext>();
}

MiniCParser::ExprContext* MiniCParser::ArrayDefDimsContext::expr(size_t i) {
  return getRuleContext<MiniCParser::ExprContext>(i);
}

std::vector<tree::TerminalNode *> MiniCParser::ArrayDefDimsContext::T_R_BRACK() {
  return getTokens(MiniCParser::T_R_BRACK);
}

tree::TerminalNode* MiniCParser::ArrayDefDimsContext::T_R_BRACK(size_t i) {
  return getToken(MiniCParser::T_R_BRACK, i);
}


size_t MiniCParser::ArrayDefDimsContext::getRuleIndex() const {
  return MiniCParser::RuleArrayDefDims;
}


std::any MiniCParser::ArrayDefDimsContext::accept(tree::ParseTreeVisitor *visitor) {
  if (auto parserVisitor = dynamic_cast<MiniCVisitor*>(visitor))
    return parserVisitor->visitArrayDefDims(this);
  else
    return visitor->visitChildren(this);
}

MiniCParser::ArrayDefDimsContext* MiniCParser::arrayDefDims() {
  ArrayDefDimsContext *_localctx = _tracker.createInstance<ArrayDefDimsContext>(_ctx, getState());
  enterRule(_localctx, 34, MiniCParser::RuleArrayDefDims);
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
    setState(199); 
    _errHandler->sync(this);
    _la = _input->LA(1);
    do {
      setState(195);
      match(MiniCParser::T_L_BRACK);
      setState(196);
      expr();
      setState(197);
      match(MiniCParser::T_R_BRACK);
      setState(201); 
      _errHandler->sync(this);
      _la = _input->LA(1);
    } while (_la == MiniCParser::T_L_BRACK);
   
  }
  catch (RecognitionException &e) {
    _errHandler->reportError(this, e);
    _localctx->exception = std::current_exception();
    _errHandler->recover(this, _localctx->exception);
  }

  return _localctx;
}

//----------------- InitValContext ------------------------------------------------------------------

MiniCParser::InitValContext::InitValContext(ParserRuleContext *parent, size_t invokingState)
  : ParserRuleContext(parent, invokingState) {
}

MiniCParser::ExprContext* MiniCParser::InitValContext::expr() {
  return getRuleContext<MiniCParser::ExprContext>(0);
}

tree::TerminalNode* MiniCParser::InitValContext::T_L_BRACE() {
  return getToken(MiniCParser::T_L_BRACE, 0);
}

tree::TerminalNode* MiniCParser::InitValContext::T_R_BRACE() {
  return getToken(MiniCParser::T_R_BRACE, 0);
}

std::vector<MiniCParser::InitValContext *> MiniCParser::InitValContext::initVal() {
  return getRuleContexts<MiniCParser::InitValContext>();
}

MiniCParser::InitValContext* MiniCParser::InitValContext::initVal(size_t i) {
  return getRuleContext<MiniCParser::InitValContext>(i);
}

std::vector<tree::TerminalNode *> MiniCParser::InitValContext::T_COMMA() {
  return getTokens(MiniCParser::T_COMMA);
}

tree::TerminalNode* MiniCParser::InitValContext::T_COMMA(size_t i) {
  return getToken(MiniCParser::T_COMMA, i);
}


size_t MiniCParser::InitValContext::getRuleIndex() const {
  return MiniCParser::RuleInitVal;
}


std::any MiniCParser::InitValContext::accept(tree::ParseTreeVisitor *visitor) {
  if (auto parserVisitor = dynamic_cast<MiniCVisitor*>(visitor))
    return parserVisitor->visitInitVal(this);
  else
    return visitor->visitChildren(this);
}

MiniCParser::InitValContext* MiniCParser::initVal() {
  InitValContext *_localctx = _tracker.createInstance<InitValContext>(_ctx, getState());
  enterRule(_localctx, 36, MiniCParser::RuleInitVal);
  size_t _la = 0;

#if __cplusplus > 201703L
  auto onExit = finally([=, this] {
#else
  auto onExit = finally([=] {
#endif
    exitRule();
  });
  try {
    setState(216);
    _errHandler->sync(this);
    switch (_input->LA(1)) {
      case MiniCParser::T_L_PAREN:
      case MiniCParser::T_ADD:
      case MiniCParser::T_SUB:
      case MiniCParser::T_NOT:
      case MiniCParser::T_INC:
      case MiniCParser::T_DEC:
      case MiniCParser::T_ID:
      case MiniCParser::T_FLOAT_LITERAL:
      case MiniCParser::T_DIGIT: {
        enterOuterAlt(_localctx, 1);
        setState(203);
        expr();
        break;
      }

      case MiniCParser::T_L_BRACE: {
        enterOuterAlt(_localctx, 2);
        setState(204);
        match(MiniCParser::T_L_BRACE);
        setState(213);
        _errHandler->sync(this);

        _la = _input->LA(1);
        if ((((_la & ~ 0x3fULL) == 0) &&
          ((1ULL << _la) & 3573465415746) != 0)) {
          setState(205);
          initVal();
          setState(210);
          _errHandler->sync(this);
          _la = _input->LA(1);
          while (_la == MiniCParser::T_COMMA) {
            setState(206);
            match(MiniCParser::T_COMMA);
            setState(207);
            initVal();
            setState(212);
            _errHandler->sync(this);
            _la = _input->LA(1);
          }
        }
        setState(215);
        match(MiniCParser::T_R_BRACE);
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

//----------------- UnmatchedStatementWrapperContext ------------------------------------------------------------------

MiniCParser::UnmatchedStatementContext* MiniCParser::UnmatchedStatementWrapperContext::unmatchedStatement() {
  return getRuleContext<MiniCParser::UnmatchedStatementContext>(0);
}

MiniCParser::UnmatchedStatementWrapperContext::UnmatchedStatementWrapperContext(StatementContext *ctx) { copyFrom(ctx); }


std::any MiniCParser::UnmatchedStatementWrapperContext::accept(tree::ParseTreeVisitor *visitor) {
  if (auto parserVisitor = dynamic_cast<MiniCVisitor*>(visitor))
    return parserVisitor->visitUnmatchedStatementWrapper(this);
  else
    return visitor->visitChildren(this);
}
//----------------- MatchedStatementWrapperContext ------------------------------------------------------------------

MiniCParser::MatchedStatementContext* MiniCParser::MatchedStatementWrapperContext::matchedStatement() {
  return getRuleContext<MiniCParser::MatchedStatementContext>(0);
}

MiniCParser::MatchedStatementWrapperContext::MatchedStatementWrapperContext(StatementContext *ctx) { copyFrom(ctx); }


std::any MiniCParser::MatchedStatementWrapperContext::accept(tree::ParseTreeVisitor *visitor) {
  if (auto parserVisitor = dynamic_cast<MiniCVisitor*>(visitor))
    return parserVisitor->visitMatchedStatementWrapper(this);
  else
    return visitor->visitChildren(this);
}
MiniCParser::StatementContext* MiniCParser::statement() {
  StatementContext *_localctx = _tracker.createInstance<StatementContext>(_ctx, getState());
  enterRule(_localctx, 38, MiniCParser::RuleStatement);

#if __cplusplus > 201703L
  auto onExit = finally([=, this] {
#else
  auto onExit = finally([=] {
#endif
    exitRule();
  });
  try {
    setState(220);
    _errHandler->sync(this);
    switch (getInterpreter<atn::ParserATNSimulator>()->adaptivePredict(_input, 21, _ctx)) {
    case 1: {
      _localctx = _tracker.createInstance<MiniCParser::MatchedStatementWrapperContext>(_localctx);
      enterOuterAlt(_localctx, 1);
      setState(218);
      matchedStatement();
      break;
    }

    case 2: {
      _localctx = _tracker.createInstance<MiniCParser::UnmatchedStatementWrapperContext>(_localctx);
      enterOuterAlt(_localctx, 2);
      setState(219);
      unmatchedStatement();
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

//----------------- MatchedStatementContext ------------------------------------------------------------------

MiniCParser::MatchedStatementContext::MatchedStatementContext(ParserRuleContext *parent, size_t invokingState)
  : ParserRuleContext(parent, invokingState) {
}


size_t MiniCParser::MatchedStatementContext::getRuleIndex() const {
  return MiniCParser::RuleMatchedStatement;
}

void MiniCParser::MatchedStatementContext::copyFrom(MatchedStatementContext *ctx) {
  ParserRuleContext::copyFrom(ctx);
}

//----------------- BlockStatementContext ------------------------------------------------------------------

MiniCParser::BlockContext* MiniCParser::BlockStatementContext::block() {
  return getRuleContext<MiniCParser::BlockContext>(0);
}

MiniCParser::BlockStatementContext::BlockStatementContext(MatchedStatementContext *ctx) { copyFrom(ctx); }


std::any MiniCParser::BlockStatementContext::accept(tree::ParseTreeVisitor *visitor) {
  if (auto parserVisitor = dynamic_cast<MiniCVisitor*>(visitor))
    return parserVisitor->visitBlockStatement(this);
  else
    return visitor->visitChildren(this);
}
//----------------- IfElseMatchedStatementContext ------------------------------------------------------------------

tree::TerminalNode* MiniCParser::IfElseMatchedStatementContext::T_IF() {
  return getToken(MiniCParser::T_IF, 0);
}

tree::TerminalNode* MiniCParser::IfElseMatchedStatementContext::T_L_PAREN() {
  return getToken(MiniCParser::T_L_PAREN, 0);
}

MiniCParser::CondContext* MiniCParser::IfElseMatchedStatementContext::cond() {
  return getRuleContext<MiniCParser::CondContext>(0);
}

tree::TerminalNode* MiniCParser::IfElseMatchedStatementContext::T_R_PAREN() {
  return getToken(MiniCParser::T_R_PAREN, 0);
}

std::vector<MiniCParser::MatchedStatementContext *> MiniCParser::IfElseMatchedStatementContext::matchedStatement() {
  return getRuleContexts<MiniCParser::MatchedStatementContext>();
}

MiniCParser::MatchedStatementContext* MiniCParser::IfElseMatchedStatementContext::matchedStatement(size_t i) {
  return getRuleContext<MiniCParser::MatchedStatementContext>(i);
}

tree::TerminalNode* MiniCParser::IfElseMatchedStatementContext::T_ELSE() {
  return getToken(MiniCParser::T_ELSE, 0);
}

MiniCParser::IfElseMatchedStatementContext::IfElseMatchedStatementContext(MatchedStatementContext *ctx) { copyFrom(ctx); }


std::any MiniCParser::IfElseMatchedStatementContext::accept(tree::ParseTreeVisitor *visitor) {
  if (auto parserVisitor = dynamic_cast<MiniCVisitor*>(visitor))
    return parserVisitor->visitIfElseMatchedStatement(this);
  else
    return visitor->visitChildren(this);
}
//----------------- WhileMatchedStatementContext ------------------------------------------------------------------

tree::TerminalNode* MiniCParser::WhileMatchedStatementContext::T_WHILE() {
  return getToken(MiniCParser::T_WHILE, 0);
}

tree::TerminalNode* MiniCParser::WhileMatchedStatementContext::T_L_PAREN() {
  return getToken(MiniCParser::T_L_PAREN, 0);
}

MiniCParser::CondContext* MiniCParser::WhileMatchedStatementContext::cond() {
  return getRuleContext<MiniCParser::CondContext>(0);
}

tree::TerminalNode* MiniCParser::WhileMatchedStatementContext::T_R_PAREN() {
  return getToken(MiniCParser::T_R_PAREN, 0);
}

MiniCParser::MatchedStatementContext* MiniCParser::WhileMatchedStatementContext::matchedStatement() {
  return getRuleContext<MiniCParser::MatchedStatementContext>(0);
}

MiniCParser::WhileMatchedStatementContext::WhileMatchedStatementContext(MatchedStatementContext *ctx) { copyFrom(ctx); }


std::any MiniCParser::WhileMatchedStatementContext::accept(tree::ParseTreeVisitor *visitor) {
  if (auto parserVisitor = dynamic_cast<MiniCVisitor*>(visitor))
    return parserVisitor->visitWhileMatchedStatement(this);
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

MiniCParser::AssignStatementContext::AssignStatementContext(MatchedStatementContext *ctx) { copyFrom(ctx); }


std::any MiniCParser::AssignStatementContext::accept(tree::ParseTreeVisitor *visitor) {
  if (auto parserVisitor = dynamic_cast<MiniCVisitor*>(visitor))
    return parserVisitor->visitAssignStatement(this);
  else
    return visitor->visitChildren(this);
}
//----------------- ForStatementContext ------------------------------------------------------------------

tree::TerminalNode* MiniCParser::ForStatementContext::T_FOR() {
  return getToken(MiniCParser::T_FOR, 0);
}

tree::TerminalNode* MiniCParser::ForStatementContext::T_L_PAREN() {
  return getToken(MiniCParser::T_L_PAREN, 0);
}

std::vector<tree::TerminalNode *> MiniCParser::ForStatementContext::T_SEMICOLON() {
  return getTokens(MiniCParser::T_SEMICOLON);
}

tree::TerminalNode* MiniCParser::ForStatementContext::T_SEMICOLON(size_t i) {
  return getToken(MiniCParser::T_SEMICOLON, i);
}

tree::TerminalNode* MiniCParser::ForStatementContext::T_R_PAREN() {
  return getToken(MiniCParser::T_R_PAREN, 0);
}

MiniCParser::MatchedStatementContext* MiniCParser::ForStatementContext::matchedStatement() {
  return getRuleContext<MiniCParser::MatchedStatementContext>(0);
}

MiniCParser::ForInitContext* MiniCParser::ForStatementContext::forInit() {
  return getRuleContext<MiniCParser::ForInitContext>(0);
}

MiniCParser::CondContext* MiniCParser::ForStatementContext::cond() {
  return getRuleContext<MiniCParser::CondContext>(0);
}

MiniCParser::ForStepContext* MiniCParser::ForStatementContext::forStep() {
  return getRuleContext<MiniCParser::ForStepContext>(0);
}

MiniCParser::ForStatementContext::ForStatementContext(MatchedStatementContext *ctx) { copyFrom(ctx); }


std::any MiniCParser::ForStatementContext::accept(tree::ParseTreeVisitor *visitor) {
  if (auto parserVisitor = dynamic_cast<MiniCVisitor*>(visitor))
    return parserVisitor->visitForStatement(this);
  else
    return visitor->visitChildren(this);
}
//----------------- BreakStatementContext ------------------------------------------------------------------

tree::TerminalNode* MiniCParser::BreakStatementContext::T_BREAK() {
  return getToken(MiniCParser::T_BREAK, 0);
}

tree::TerminalNode* MiniCParser::BreakStatementContext::T_SEMICOLON() {
  return getToken(MiniCParser::T_SEMICOLON, 0);
}

MiniCParser::BreakStatementContext::BreakStatementContext(MatchedStatementContext *ctx) { copyFrom(ctx); }


std::any MiniCParser::BreakStatementContext::accept(tree::ParseTreeVisitor *visitor) {
  if (auto parserVisitor = dynamic_cast<MiniCVisitor*>(visitor))
    return parserVisitor->visitBreakStatement(this);
  else
    return visitor->visitChildren(this);
}
//----------------- ContinueStatementContext ------------------------------------------------------------------

tree::TerminalNode* MiniCParser::ContinueStatementContext::T_CONTINUE() {
  return getToken(MiniCParser::T_CONTINUE, 0);
}

tree::TerminalNode* MiniCParser::ContinueStatementContext::T_SEMICOLON() {
  return getToken(MiniCParser::T_SEMICOLON, 0);
}

MiniCParser::ContinueStatementContext::ContinueStatementContext(MatchedStatementContext *ctx) { copyFrom(ctx); }


std::any MiniCParser::ContinueStatementContext::accept(tree::ParseTreeVisitor *visitor) {
  if (auto parserVisitor = dynamic_cast<MiniCVisitor*>(visitor))
    return parserVisitor->visitContinueStatement(this);
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

MiniCParser::ExpressionStatementContext::ExpressionStatementContext(MatchedStatementContext *ctx) { copyFrom(ctx); }


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

tree::TerminalNode* MiniCParser::ReturnStatementContext::T_SEMICOLON() {
  return getToken(MiniCParser::T_SEMICOLON, 0);
}

MiniCParser::ExprContext* MiniCParser::ReturnStatementContext::expr() {
  return getRuleContext<MiniCParser::ExprContext>(0);
}

MiniCParser::ReturnStatementContext::ReturnStatementContext(MatchedStatementContext *ctx) { copyFrom(ctx); }


std::any MiniCParser::ReturnStatementContext::accept(tree::ParseTreeVisitor *visitor) {
  if (auto parserVisitor = dynamic_cast<MiniCVisitor*>(visitor))
    return parserVisitor->visitReturnStatement(this);
  else
    return visitor->visitChildren(this);
}
MiniCParser::MatchedStatementContext* MiniCParser::matchedStatement() {
  MatchedStatementContext *_localctx = _tracker.createInstance<MatchedStatementContext>(_ctx, getState());
  enterRule(_localctx, 40, MiniCParser::RuleMatchedStatement);
  size_t _la = 0;

#if __cplusplus > 201703L
  auto onExit = finally([=, this] {
#else
  auto onExit = finally([=] {
#endif
    exitRule();
  });
  try {
    setState(270);
    _errHandler->sync(this);
    switch (getInterpreter<atn::ParserATNSimulator>()->adaptivePredict(_input, 27, _ctx)) {
    case 1: {
      _localctx = _tracker.createInstance<MiniCParser::ReturnStatementContext>(_localctx);
      enterOuterAlt(_localctx, 1);
      setState(222);
      match(MiniCParser::T_RETURN);
      setState(224);
      _errHandler->sync(this);

      _la = _input->LA(1);
      if ((((_la & ~ 0x3fULL) == 0) &&
        ((1ULL << _la) & 3573465415682) != 0)) {
        setState(223);
        expr();
      }
      setState(226);
      match(MiniCParser::T_SEMICOLON);
      break;
    }

    case 2: {
      _localctx = _tracker.createInstance<MiniCParser::AssignStatementContext>(_localctx);
      enterOuterAlt(_localctx, 2);
      setState(227);
      lVal();
      setState(228);
      match(MiniCParser::T_ASSIGN);
      setState(229);
      expr();
      setState(230);
      match(MiniCParser::T_SEMICOLON);
      break;
    }

    case 3: {
      _localctx = _tracker.createInstance<MiniCParser::IfElseMatchedStatementContext>(_localctx);
      enterOuterAlt(_localctx, 3);
      setState(232);
      match(MiniCParser::T_IF);
      setState(233);
      match(MiniCParser::T_L_PAREN);
      setState(234);
      cond();
      setState(235);
      match(MiniCParser::T_R_PAREN);
      setState(236);
      matchedStatement();
      setState(237);
      match(MiniCParser::T_ELSE);
      setState(238);
      matchedStatement();
      break;
    }

    case 4: {
      _localctx = _tracker.createInstance<MiniCParser::WhileMatchedStatementContext>(_localctx);
      enterOuterAlt(_localctx, 4);
      setState(240);
      match(MiniCParser::T_WHILE);
      setState(241);
      match(MiniCParser::T_L_PAREN);
      setState(242);
      cond();
      setState(243);
      match(MiniCParser::T_R_PAREN);
      setState(244);
      matchedStatement();
      break;
    }

    case 5: {
      _localctx = _tracker.createInstance<MiniCParser::ForStatementContext>(_localctx);
      enterOuterAlt(_localctx, 5);
      setState(246);
      match(MiniCParser::T_FOR);
      setState(247);
      match(MiniCParser::T_L_PAREN);
      setState(249);
      _errHandler->sync(this);

      _la = _input->LA(1);
      if ((((_la & ~ 0x3fULL) == 0) &&
        ((1ULL << _la) & 3702314434562) != 0)) {
        setState(248);
        forInit();
      }
      setState(251);
      match(MiniCParser::T_SEMICOLON);
      setState(253);
      _errHandler->sync(this);

      _la = _input->LA(1);
      if ((((_la & ~ 0x3fULL) == 0) &&
        ((1ULL << _la) & 3573465415682) != 0)) {
        setState(252);
        cond();
      }
      setState(255);
      match(MiniCParser::T_SEMICOLON);
      setState(257);
      _errHandler->sync(this);

      _la = _input->LA(1);
      if ((((_la & ~ 0x3fULL) == 0) &&
        ((1ULL << _la) & 3573465415682) != 0)) {
        setState(256);
        forStep();
      }
      setState(259);
      match(MiniCParser::T_R_PAREN);
      setState(260);
      matchedStatement();
      break;
    }

    case 6: {
      _localctx = _tracker.createInstance<MiniCParser::BreakStatementContext>(_localctx);
      enterOuterAlt(_localctx, 6);
      setState(261);
      match(MiniCParser::T_BREAK);
      setState(262);
      match(MiniCParser::T_SEMICOLON);
      break;
    }

    case 7: {
      _localctx = _tracker.createInstance<MiniCParser::ContinueStatementContext>(_localctx);
      enterOuterAlt(_localctx, 7);
      setState(263);
      match(MiniCParser::T_CONTINUE);
      setState(264);
      match(MiniCParser::T_SEMICOLON);
      break;
    }

    case 8: {
      _localctx = _tracker.createInstance<MiniCParser::BlockStatementContext>(_localctx);
      enterOuterAlt(_localctx, 8);
      setState(265);
      block();
      break;
    }

    case 9: {
      _localctx = _tracker.createInstance<MiniCParser::ExpressionStatementContext>(_localctx);
      enterOuterAlt(_localctx, 9);
      setState(267);
      _errHandler->sync(this);

      _la = _input->LA(1);
      if ((((_la & ~ 0x3fULL) == 0) &&
        ((1ULL << _la) & 3573465415682) != 0)) {
        setState(266);
        expr();
      }
      setState(269);
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

//----------------- UnmatchedStatementContext ------------------------------------------------------------------

MiniCParser::UnmatchedStatementContext::UnmatchedStatementContext(ParserRuleContext *parent, size_t invokingState)
  : ParserRuleContext(parent, invokingState) {
}


size_t MiniCParser::UnmatchedStatementContext::getRuleIndex() const {
  return MiniCParser::RuleUnmatchedStatement;
}

void MiniCParser::UnmatchedStatementContext::copyFrom(UnmatchedStatementContext *ctx) {
  ParserRuleContext::copyFrom(ctx);
}

//----------------- IfElseUnmatchedStatementContext ------------------------------------------------------------------

tree::TerminalNode* MiniCParser::IfElseUnmatchedStatementContext::T_IF() {
  return getToken(MiniCParser::T_IF, 0);
}

tree::TerminalNode* MiniCParser::IfElseUnmatchedStatementContext::T_L_PAREN() {
  return getToken(MiniCParser::T_L_PAREN, 0);
}

MiniCParser::CondContext* MiniCParser::IfElseUnmatchedStatementContext::cond() {
  return getRuleContext<MiniCParser::CondContext>(0);
}

tree::TerminalNode* MiniCParser::IfElseUnmatchedStatementContext::T_R_PAREN() {
  return getToken(MiniCParser::T_R_PAREN, 0);
}

MiniCParser::MatchedStatementContext* MiniCParser::IfElseUnmatchedStatementContext::matchedStatement() {
  return getRuleContext<MiniCParser::MatchedStatementContext>(0);
}

tree::TerminalNode* MiniCParser::IfElseUnmatchedStatementContext::T_ELSE() {
  return getToken(MiniCParser::T_ELSE, 0);
}

MiniCParser::UnmatchedStatementContext* MiniCParser::IfElseUnmatchedStatementContext::unmatchedStatement() {
  return getRuleContext<MiniCParser::UnmatchedStatementContext>(0);
}

MiniCParser::IfElseUnmatchedStatementContext::IfElseUnmatchedStatementContext(UnmatchedStatementContext *ctx) { copyFrom(ctx); }


std::any MiniCParser::IfElseUnmatchedStatementContext::accept(tree::ParseTreeVisitor *visitor) {
  if (auto parserVisitor = dynamic_cast<MiniCVisitor*>(visitor))
    return parserVisitor->visitIfElseUnmatchedStatement(this);
  else
    return visitor->visitChildren(this);
}
//----------------- WhileUnmatchedStatementContext ------------------------------------------------------------------

tree::TerminalNode* MiniCParser::WhileUnmatchedStatementContext::T_WHILE() {
  return getToken(MiniCParser::T_WHILE, 0);
}

tree::TerminalNode* MiniCParser::WhileUnmatchedStatementContext::T_L_PAREN() {
  return getToken(MiniCParser::T_L_PAREN, 0);
}

MiniCParser::CondContext* MiniCParser::WhileUnmatchedStatementContext::cond() {
  return getRuleContext<MiniCParser::CondContext>(0);
}

tree::TerminalNode* MiniCParser::WhileUnmatchedStatementContext::T_R_PAREN() {
  return getToken(MiniCParser::T_R_PAREN, 0);
}

MiniCParser::UnmatchedStatementContext* MiniCParser::WhileUnmatchedStatementContext::unmatchedStatement() {
  return getRuleContext<MiniCParser::UnmatchedStatementContext>(0);
}

MiniCParser::WhileUnmatchedStatementContext::WhileUnmatchedStatementContext(UnmatchedStatementContext *ctx) { copyFrom(ctx); }


std::any MiniCParser::WhileUnmatchedStatementContext::accept(tree::ParseTreeVisitor *visitor) {
  if (auto parserVisitor = dynamic_cast<MiniCVisitor*>(visitor))
    return parserVisitor->visitWhileUnmatchedStatement(this);
  else
    return visitor->visitChildren(this);
}
//----------------- IfWithoutElseStatementContext ------------------------------------------------------------------

tree::TerminalNode* MiniCParser::IfWithoutElseStatementContext::T_IF() {
  return getToken(MiniCParser::T_IF, 0);
}

tree::TerminalNode* MiniCParser::IfWithoutElseStatementContext::T_L_PAREN() {
  return getToken(MiniCParser::T_L_PAREN, 0);
}

MiniCParser::CondContext* MiniCParser::IfWithoutElseStatementContext::cond() {
  return getRuleContext<MiniCParser::CondContext>(0);
}

tree::TerminalNode* MiniCParser::IfWithoutElseStatementContext::T_R_PAREN() {
  return getToken(MiniCParser::T_R_PAREN, 0);
}

MiniCParser::StatementContext* MiniCParser::IfWithoutElseStatementContext::statement() {
  return getRuleContext<MiniCParser::StatementContext>(0);
}

MiniCParser::IfWithoutElseStatementContext::IfWithoutElseStatementContext(UnmatchedStatementContext *ctx) { copyFrom(ctx); }


std::any MiniCParser::IfWithoutElseStatementContext::accept(tree::ParseTreeVisitor *visitor) {
  if (auto parserVisitor = dynamic_cast<MiniCVisitor*>(visitor))
    return parserVisitor->visitIfWithoutElseStatement(this);
  else
    return visitor->visitChildren(this);
}
//----------------- ForUnmatchedStatementContext ------------------------------------------------------------------

tree::TerminalNode* MiniCParser::ForUnmatchedStatementContext::T_FOR() {
  return getToken(MiniCParser::T_FOR, 0);
}

tree::TerminalNode* MiniCParser::ForUnmatchedStatementContext::T_L_PAREN() {
  return getToken(MiniCParser::T_L_PAREN, 0);
}

std::vector<tree::TerminalNode *> MiniCParser::ForUnmatchedStatementContext::T_SEMICOLON() {
  return getTokens(MiniCParser::T_SEMICOLON);
}

tree::TerminalNode* MiniCParser::ForUnmatchedStatementContext::T_SEMICOLON(size_t i) {
  return getToken(MiniCParser::T_SEMICOLON, i);
}

tree::TerminalNode* MiniCParser::ForUnmatchedStatementContext::T_R_PAREN() {
  return getToken(MiniCParser::T_R_PAREN, 0);
}

MiniCParser::UnmatchedStatementContext* MiniCParser::ForUnmatchedStatementContext::unmatchedStatement() {
  return getRuleContext<MiniCParser::UnmatchedStatementContext>(0);
}

MiniCParser::ForInitContext* MiniCParser::ForUnmatchedStatementContext::forInit() {
  return getRuleContext<MiniCParser::ForInitContext>(0);
}

MiniCParser::CondContext* MiniCParser::ForUnmatchedStatementContext::cond() {
  return getRuleContext<MiniCParser::CondContext>(0);
}

MiniCParser::ForStepContext* MiniCParser::ForUnmatchedStatementContext::forStep() {
  return getRuleContext<MiniCParser::ForStepContext>(0);
}

MiniCParser::ForUnmatchedStatementContext::ForUnmatchedStatementContext(UnmatchedStatementContext *ctx) { copyFrom(ctx); }


std::any MiniCParser::ForUnmatchedStatementContext::accept(tree::ParseTreeVisitor *visitor) {
  if (auto parserVisitor = dynamic_cast<MiniCVisitor*>(visitor))
    return parserVisitor->visitForUnmatchedStatement(this);
  else
    return visitor->visitChildren(this);
}
MiniCParser::UnmatchedStatementContext* MiniCParser::unmatchedStatement() {
  UnmatchedStatementContext *_localctx = _tracker.createInstance<UnmatchedStatementContext>(_ctx, getState());
  enterRule(_localctx, 42, MiniCParser::RuleUnmatchedStatement);
  size_t _la = 0;

#if __cplusplus > 201703L
  auto onExit = finally([=, this] {
#else
  auto onExit = finally([=] {
#endif
    exitRule();
  });
  try {
    setState(307);
    _errHandler->sync(this);
    switch (getInterpreter<atn::ParserATNSimulator>()->adaptivePredict(_input, 31, _ctx)) {
    case 1: {
      _localctx = _tracker.createInstance<MiniCParser::IfWithoutElseStatementContext>(_localctx);
      enterOuterAlt(_localctx, 1);
      setState(272);
      match(MiniCParser::T_IF);
      setState(273);
      match(MiniCParser::T_L_PAREN);
      setState(274);
      cond();
      setState(275);
      match(MiniCParser::T_R_PAREN);
      setState(276);
      statement();
      break;
    }

    case 2: {
      _localctx = _tracker.createInstance<MiniCParser::IfElseUnmatchedStatementContext>(_localctx);
      enterOuterAlt(_localctx, 2);
      setState(278);
      match(MiniCParser::T_IF);
      setState(279);
      match(MiniCParser::T_L_PAREN);
      setState(280);
      cond();
      setState(281);
      match(MiniCParser::T_R_PAREN);
      setState(282);
      matchedStatement();
      setState(283);
      match(MiniCParser::T_ELSE);
      setState(284);
      unmatchedStatement();
      break;
    }

    case 3: {
      _localctx = _tracker.createInstance<MiniCParser::WhileUnmatchedStatementContext>(_localctx);
      enterOuterAlt(_localctx, 3);
      setState(286);
      match(MiniCParser::T_WHILE);
      setState(287);
      match(MiniCParser::T_L_PAREN);
      setState(288);
      cond();
      setState(289);
      match(MiniCParser::T_R_PAREN);
      setState(290);
      unmatchedStatement();
      break;
    }

    case 4: {
      _localctx = _tracker.createInstance<MiniCParser::ForUnmatchedStatementContext>(_localctx);
      enterOuterAlt(_localctx, 4);
      setState(292);
      match(MiniCParser::T_FOR);
      setState(293);
      match(MiniCParser::T_L_PAREN);
      setState(295);
      _errHandler->sync(this);

      _la = _input->LA(1);
      if ((((_la & ~ 0x3fULL) == 0) &&
        ((1ULL << _la) & 3702314434562) != 0)) {
        setState(294);
        forInit();
      }
      setState(297);
      match(MiniCParser::T_SEMICOLON);
      setState(299);
      _errHandler->sync(this);

      _la = _input->LA(1);
      if ((((_la & ~ 0x3fULL) == 0) &&
        ((1ULL << _la) & 3573465415682) != 0)) {
        setState(298);
        cond();
      }
      setState(301);
      match(MiniCParser::T_SEMICOLON);
      setState(303);
      _errHandler->sync(this);

      _la = _input->LA(1);
      if ((((_la & ~ 0x3fULL) == 0) &&
        ((1ULL << _la) & 3573465415682) != 0)) {
        setState(302);
        forStep();
      }
      setState(305);
      match(MiniCParser::T_R_PAREN);
      setState(306);
      unmatchedStatement();
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

//----------------- ForInitContext ------------------------------------------------------------------

MiniCParser::ForInitContext::ForInitContext(ParserRuleContext *parent, size_t invokingState)
  : ParserRuleContext(parent, invokingState) {
}

MiniCParser::ConstDeclNoSemiContext* MiniCParser::ForInitContext::constDeclNoSemi() {
  return getRuleContext<MiniCParser::ConstDeclNoSemiContext>(0);
}

MiniCParser::VarDeclNoSemiContext* MiniCParser::ForInitContext::varDeclNoSemi() {
  return getRuleContext<MiniCParser::VarDeclNoSemiContext>(0);
}

MiniCParser::LValContext* MiniCParser::ForInitContext::lVal() {
  return getRuleContext<MiniCParser::LValContext>(0);
}

tree::TerminalNode* MiniCParser::ForInitContext::T_ASSIGN() {
  return getToken(MiniCParser::T_ASSIGN, 0);
}

MiniCParser::ExprContext* MiniCParser::ForInitContext::expr() {
  return getRuleContext<MiniCParser::ExprContext>(0);
}


size_t MiniCParser::ForInitContext::getRuleIndex() const {
  return MiniCParser::RuleForInit;
}


std::any MiniCParser::ForInitContext::accept(tree::ParseTreeVisitor *visitor) {
  if (auto parserVisitor = dynamic_cast<MiniCVisitor*>(visitor))
    return parserVisitor->visitForInit(this);
  else
    return visitor->visitChildren(this);
}

MiniCParser::ForInitContext* MiniCParser::forInit() {
  ForInitContext *_localctx = _tracker.createInstance<ForInitContext>(_ctx, getState());
  enterRule(_localctx, 44, MiniCParser::RuleForInit);

#if __cplusplus > 201703L
  auto onExit = finally([=, this] {
#else
  auto onExit = finally([=] {
#endif
    exitRule();
  });
  try {
    setState(316);
    _errHandler->sync(this);
    switch (getInterpreter<atn::ParserATNSimulator>()->adaptivePredict(_input, 32, _ctx)) {
    case 1: {
      enterOuterAlt(_localctx, 1);
      setState(309);
      constDeclNoSemi();
      break;
    }

    case 2: {
      enterOuterAlt(_localctx, 2);
      setState(310);
      varDeclNoSemi();
      break;
    }

    case 3: {
      enterOuterAlt(_localctx, 3);
      setState(311);
      lVal();
      setState(312);
      match(MiniCParser::T_ASSIGN);
      setState(313);
      expr();
      break;
    }

    case 4: {
      enterOuterAlt(_localctx, 4);
      setState(315);
      expr();
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

//----------------- ForStepContext ------------------------------------------------------------------

MiniCParser::ForStepContext::ForStepContext(ParserRuleContext *parent, size_t invokingState)
  : ParserRuleContext(parent, invokingState) {
}

MiniCParser::LValContext* MiniCParser::ForStepContext::lVal() {
  return getRuleContext<MiniCParser::LValContext>(0);
}

tree::TerminalNode* MiniCParser::ForStepContext::T_ASSIGN() {
  return getToken(MiniCParser::T_ASSIGN, 0);
}

MiniCParser::ExprContext* MiniCParser::ForStepContext::expr() {
  return getRuleContext<MiniCParser::ExprContext>(0);
}


size_t MiniCParser::ForStepContext::getRuleIndex() const {
  return MiniCParser::RuleForStep;
}


std::any MiniCParser::ForStepContext::accept(tree::ParseTreeVisitor *visitor) {
  if (auto parserVisitor = dynamic_cast<MiniCVisitor*>(visitor))
    return parserVisitor->visitForStep(this);
  else
    return visitor->visitChildren(this);
}

MiniCParser::ForStepContext* MiniCParser::forStep() {
  ForStepContext *_localctx = _tracker.createInstance<ForStepContext>(_ctx, getState());
  enterRule(_localctx, 46, MiniCParser::RuleForStep);

#if __cplusplus > 201703L
  auto onExit = finally([=, this] {
#else
  auto onExit = finally([=] {
#endif
    exitRule();
  });
  try {
    setState(323);
    _errHandler->sync(this);
    switch (getInterpreter<atn::ParserATNSimulator>()->adaptivePredict(_input, 33, _ctx)) {
    case 1: {
      enterOuterAlt(_localctx, 1);
      setState(318);
      lVal();
      setState(319);
      match(MiniCParser::T_ASSIGN);
      setState(320);
      expr();
      break;
    }

    case 2: {
      enterOuterAlt(_localctx, 2);
      setState(322);
      expr();
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

MiniCParser::LOrExpContext* MiniCParser::ExprContext::lOrExp() {
  return getRuleContext<MiniCParser::LOrExpContext>(0);
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
  enterRule(_localctx, 48, MiniCParser::RuleExpr);

#if __cplusplus > 201703L
  auto onExit = finally([=, this] {
#else
  auto onExit = finally([=] {
#endif
    exitRule();
  });
  try {
    enterOuterAlt(_localctx, 1);
    setState(325);
    lOrExp();
   
  }
  catch (RecognitionException &e) {
    _errHandler->reportError(this, e);
    _localctx->exception = std::current_exception();
    _errHandler->recover(this, _localctx->exception);
  }

  return _localctx;
}

//----------------- CondContext ------------------------------------------------------------------

MiniCParser::CondContext::CondContext(ParserRuleContext *parent, size_t invokingState)
  : ParserRuleContext(parent, invokingState) {
}

MiniCParser::ExprContext* MiniCParser::CondContext::expr() {
  return getRuleContext<MiniCParser::ExprContext>(0);
}


size_t MiniCParser::CondContext::getRuleIndex() const {
  return MiniCParser::RuleCond;
}


std::any MiniCParser::CondContext::accept(tree::ParseTreeVisitor *visitor) {
  if (auto parserVisitor = dynamic_cast<MiniCVisitor*>(visitor))
    return parserVisitor->visitCond(this);
  else
    return visitor->visitChildren(this);
}

MiniCParser::CondContext* MiniCParser::cond() {
  CondContext *_localctx = _tracker.createInstance<CondContext>(_ctx, getState());
  enterRule(_localctx, 50, MiniCParser::RuleCond);

#if __cplusplus > 201703L
  auto onExit = finally([=, this] {
#else
  auto onExit = finally([=] {
#endif
    exitRule();
  });
  try {
    enterOuterAlt(_localctx, 1);
    setState(327);
    expr();
   
  }
  catch (RecognitionException &e) {
    _errHandler->reportError(this, e);
    _localctx->exception = std::current_exception();
    _errHandler->recover(this, _localctx->exception);
  }

  return _localctx;
}

//----------------- LOrExpContext ------------------------------------------------------------------

MiniCParser::LOrExpContext::LOrExpContext(ParserRuleContext *parent, size_t invokingState)
  : ParserRuleContext(parent, invokingState) {
}

std::vector<MiniCParser::LAndExpContext *> MiniCParser::LOrExpContext::lAndExp() {
  return getRuleContexts<MiniCParser::LAndExpContext>();
}

MiniCParser::LAndExpContext* MiniCParser::LOrExpContext::lAndExp(size_t i) {
  return getRuleContext<MiniCParser::LAndExpContext>(i);
}

std::vector<tree::TerminalNode *> MiniCParser::LOrExpContext::T_LOR() {
  return getTokens(MiniCParser::T_LOR);
}

tree::TerminalNode* MiniCParser::LOrExpContext::T_LOR(size_t i) {
  return getToken(MiniCParser::T_LOR, i);
}


size_t MiniCParser::LOrExpContext::getRuleIndex() const {
  return MiniCParser::RuleLOrExp;
}


std::any MiniCParser::LOrExpContext::accept(tree::ParseTreeVisitor *visitor) {
  if (auto parserVisitor = dynamic_cast<MiniCVisitor*>(visitor))
    return parserVisitor->visitLOrExp(this);
  else
    return visitor->visitChildren(this);
}

MiniCParser::LOrExpContext* MiniCParser::lOrExp() {
  LOrExpContext *_localctx = _tracker.createInstance<LOrExpContext>(_ctx, getState());
  enterRule(_localctx, 52, MiniCParser::RuleLOrExp);
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
    setState(329);
    lAndExp();
    setState(334);
    _errHandler->sync(this);
    _la = _input->LA(1);
    while (_la == MiniCParser::T_LOR) {
      setState(330);
      match(MiniCParser::T_LOR);
      setState(331);
      lAndExp();
      setState(336);
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

//----------------- LAndExpContext ------------------------------------------------------------------

MiniCParser::LAndExpContext::LAndExpContext(ParserRuleContext *parent, size_t invokingState)
  : ParserRuleContext(parent, invokingState) {
}

std::vector<MiniCParser::EqExpContext *> MiniCParser::LAndExpContext::eqExp() {
  return getRuleContexts<MiniCParser::EqExpContext>();
}

MiniCParser::EqExpContext* MiniCParser::LAndExpContext::eqExp(size_t i) {
  return getRuleContext<MiniCParser::EqExpContext>(i);
}

std::vector<tree::TerminalNode *> MiniCParser::LAndExpContext::T_LAND() {
  return getTokens(MiniCParser::T_LAND);
}

tree::TerminalNode* MiniCParser::LAndExpContext::T_LAND(size_t i) {
  return getToken(MiniCParser::T_LAND, i);
}


size_t MiniCParser::LAndExpContext::getRuleIndex() const {
  return MiniCParser::RuleLAndExp;
}


std::any MiniCParser::LAndExpContext::accept(tree::ParseTreeVisitor *visitor) {
  if (auto parserVisitor = dynamic_cast<MiniCVisitor*>(visitor))
    return parserVisitor->visitLAndExp(this);
  else
    return visitor->visitChildren(this);
}

MiniCParser::LAndExpContext* MiniCParser::lAndExp() {
  LAndExpContext *_localctx = _tracker.createInstance<LAndExpContext>(_ctx, getState());
  enterRule(_localctx, 54, MiniCParser::RuleLAndExp);
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
    setState(337);
    eqExp();
    setState(342);
    _errHandler->sync(this);
    _la = _input->LA(1);
    while (_la == MiniCParser::T_LAND) {
      setState(338);
      match(MiniCParser::T_LAND);
      setState(339);
      eqExp();
      setState(344);
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

//----------------- EqExpContext ------------------------------------------------------------------

MiniCParser::EqExpContext::EqExpContext(ParserRuleContext *parent, size_t invokingState)
  : ParserRuleContext(parent, invokingState) {
}

std::vector<MiniCParser::RelExpContext *> MiniCParser::EqExpContext::relExp() {
  return getRuleContexts<MiniCParser::RelExpContext>();
}

MiniCParser::RelExpContext* MiniCParser::EqExpContext::relExp(size_t i) {
  return getRuleContext<MiniCParser::RelExpContext>(i);
}

std::vector<MiniCParser::EqOpContext *> MiniCParser::EqExpContext::eqOp() {
  return getRuleContexts<MiniCParser::EqOpContext>();
}

MiniCParser::EqOpContext* MiniCParser::EqExpContext::eqOp(size_t i) {
  return getRuleContext<MiniCParser::EqOpContext>(i);
}


size_t MiniCParser::EqExpContext::getRuleIndex() const {
  return MiniCParser::RuleEqExp;
}


std::any MiniCParser::EqExpContext::accept(tree::ParseTreeVisitor *visitor) {
  if (auto parserVisitor = dynamic_cast<MiniCVisitor*>(visitor))
    return parserVisitor->visitEqExp(this);
  else
    return visitor->visitChildren(this);
}

MiniCParser::EqExpContext* MiniCParser::eqExp() {
  EqExpContext *_localctx = _tracker.createInstance<EqExpContext>(_ctx, getState());
  enterRule(_localctx, 56, MiniCParser::RuleEqExp);
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
    setState(345);
    relExp();
    setState(351);
    _errHandler->sync(this);
    _la = _input->LA(1);
    while (_la == MiniCParser::T_EQ

    || _la == MiniCParser::T_NE) {
      setState(346);
      eqOp();
      setState(347);
      relExp();
      setState(353);
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

//----------------- EqOpContext ------------------------------------------------------------------

MiniCParser::EqOpContext::EqOpContext(ParserRuleContext *parent, size_t invokingState)
  : ParserRuleContext(parent, invokingState) {
}

tree::TerminalNode* MiniCParser::EqOpContext::T_EQ() {
  return getToken(MiniCParser::T_EQ, 0);
}

tree::TerminalNode* MiniCParser::EqOpContext::T_NE() {
  return getToken(MiniCParser::T_NE, 0);
}


size_t MiniCParser::EqOpContext::getRuleIndex() const {
  return MiniCParser::RuleEqOp;
}


std::any MiniCParser::EqOpContext::accept(tree::ParseTreeVisitor *visitor) {
  if (auto parserVisitor = dynamic_cast<MiniCVisitor*>(visitor))
    return parserVisitor->visitEqOp(this);
  else
    return visitor->visitChildren(this);
}

MiniCParser::EqOpContext* MiniCParser::eqOp() {
  EqOpContext *_localctx = _tracker.createInstance<EqOpContext>(_ctx, getState());
  enterRule(_localctx, 58, MiniCParser::RuleEqOp);
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
    setState(354);
    _la = _input->LA(1);
    if (!(_la == MiniCParser::T_EQ

    || _la == MiniCParser::T_NE)) {
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

//----------------- RelExpContext ------------------------------------------------------------------

MiniCParser::RelExpContext::RelExpContext(ParserRuleContext *parent, size_t invokingState)
  : ParserRuleContext(parent, invokingState) {
}

std::vector<MiniCParser::AddExpContext *> MiniCParser::RelExpContext::addExp() {
  return getRuleContexts<MiniCParser::AddExpContext>();
}

MiniCParser::AddExpContext* MiniCParser::RelExpContext::addExp(size_t i) {
  return getRuleContext<MiniCParser::AddExpContext>(i);
}

std::vector<MiniCParser::RelOpContext *> MiniCParser::RelExpContext::relOp() {
  return getRuleContexts<MiniCParser::RelOpContext>();
}

MiniCParser::RelOpContext* MiniCParser::RelExpContext::relOp(size_t i) {
  return getRuleContext<MiniCParser::RelOpContext>(i);
}


size_t MiniCParser::RelExpContext::getRuleIndex() const {
  return MiniCParser::RuleRelExp;
}


std::any MiniCParser::RelExpContext::accept(tree::ParseTreeVisitor *visitor) {
  if (auto parserVisitor = dynamic_cast<MiniCVisitor*>(visitor))
    return parserVisitor->visitRelExp(this);
  else
    return visitor->visitChildren(this);
}

MiniCParser::RelExpContext* MiniCParser::relExp() {
  RelExpContext *_localctx = _tracker.createInstance<RelExpContext>(_ctx, getState());
  enterRule(_localctx, 60, MiniCParser::RuleRelExp);
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
    setState(356);
    addExp();
    setState(362);
    _errHandler->sync(this);
    _la = _input->LA(1);
    while ((((_la & ~ 0x3fULL) == 0) &&
      ((1ULL << _la) & 61440) != 0)) {
      setState(357);
      relOp();
      setState(358);
      addExp();
      setState(364);
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

//----------------- RelOpContext ------------------------------------------------------------------

MiniCParser::RelOpContext::RelOpContext(ParserRuleContext *parent, size_t invokingState)
  : ParserRuleContext(parent, invokingState) {
}

tree::TerminalNode* MiniCParser::RelOpContext::T_LT() {
  return getToken(MiniCParser::T_LT, 0);
}

tree::TerminalNode* MiniCParser::RelOpContext::T_GT() {
  return getToken(MiniCParser::T_GT, 0);
}

tree::TerminalNode* MiniCParser::RelOpContext::T_LE() {
  return getToken(MiniCParser::T_LE, 0);
}

tree::TerminalNode* MiniCParser::RelOpContext::T_GE() {
  return getToken(MiniCParser::T_GE, 0);
}


size_t MiniCParser::RelOpContext::getRuleIndex() const {
  return MiniCParser::RuleRelOp;
}


std::any MiniCParser::RelOpContext::accept(tree::ParseTreeVisitor *visitor) {
  if (auto parserVisitor = dynamic_cast<MiniCVisitor*>(visitor))
    return parserVisitor->visitRelOp(this);
  else
    return visitor->visitChildren(this);
}

MiniCParser::RelOpContext* MiniCParser::relOp() {
  RelOpContext *_localctx = _tracker.createInstance<RelOpContext>(_ctx, getState());
  enterRule(_localctx, 62, MiniCParser::RuleRelOp);
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
    setState(365);
    _la = _input->LA(1);
    if (!((((_la & ~ 0x3fULL) == 0) &&
      ((1ULL << _la) & 61440) != 0))) {
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
  enterRule(_localctx, 64, MiniCParser::RuleAddExp);
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
    setState(367);
    mulExp();
    setState(373);
    _errHandler->sync(this);
    _la = _input->LA(1);
    while (_la == MiniCParser::T_ADD

    || _la == MiniCParser::T_SUB) {
      setState(368);
      addOp();
      setState(369);
      mulExp();
      setState(375);
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
  enterRule(_localctx, 66, MiniCParser::RuleAddOp);
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
    setState(376);
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
  enterRule(_localctx, 68, MiniCParser::RuleMulExp);
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
    setState(378);
    unaryExp();
    setState(384);
    _errHandler->sync(this);
    _la = _input->LA(1);
    while ((((_la & ~ 0x3fULL) == 0) &&
      ((1ULL << _la) & 1835008) != 0)) {
      setState(379);
      mulOp();
      setState(380);
      unaryExp();
      setState(386);
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
  enterRule(_localctx, 70, MiniCParser::RuleMulOp);
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
    setState(387);
    _la = _input->LA(1);
    if (!((((_la & ~ 0x3fULL) == 0) &&
      ((1ULL << _la) & 1835008) != 0))) {
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

//----------------- UnaryExpContext ------------------------------------------------------------------

MiniCParser::UnaryExpContext::UnaryExpContext(ParserRuleContext *parent, size_t invokingState)
  : ParserRuleContext(parent, invokingState) {
}

MiniCParser::PrimaryExpContext* MiniCParser::UnaryExpContext::primaryExp() {
  return getRuleContext<MiniCParser::PrimaryExpContext>(0);
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

MiniCParser::UnaryOpContext* MiniCParser::UnaryExpContext::unaryOp() {
  return getRuleContext<MiniCParser::UnaryOpContext>(0);
}

MiniCParser::UnaryExpContext* MiniCParser::UnaryExpContext::unaryExp() {
  return getRuleContext<MiniCParser::UnaryExpContext>(0);
}

MiniCParser::LValContext* MiniCParser::UnaryExpContext::lVal() {
  return getRuleContext<MiniCParser::LValContext>(0);
}

tree::TerminalNode* MiniCParser::UnaryExpContext::T_INC() {
  return getToken(MiniCParser::T_INC, 0);
}

tree::TerminalNode* MiniCParser::UnaryExpContext::T_DEC() {
  return getToken(MiniCParser::T_DEC, 0);
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
  enterRule(_localctx, 72, MiniCParser::RuleUnaryExp);
  size_t _la = 0;

#if __cplusplus > 201703L
  auto onExit = finally([=, this] {
#else
  auto onExit = finally([=] {
#endif
    exitRule();
  });
  try {
    setState(404);
    _errHandler->sync(this);
    switch (getInterpreter<atn::ParserATNSimulator>()->adaptivePredict(_input, 41, _ctx)) {
    case 1: {
      enterOuterAlt(_localctx, 1);
      setState(389);
      primaryExp();
      break;
    }

    case 2: {
      enterOuterAlt(_localctx, 2);
      setState(390);
      match(MiniCParser::T_ID);
      setState(391);
      match(MiniCParser::T_L_PAREN);
      setState(393);
      _errHandler->sync(this);

      _la = _input->LA(1);
      if ((((_la & ~ 0x3fULL) == 0) &&
        ((1ULL << _la) & 4123221229570) != 0)) {
        setState(392);
        realParamList();
      }
      setState(395);
      match(MiniCParser::T_R_PAREN);
      break;
    }

    case 3: {
      enterOuterAlt(_localctx, 3);
      setState(396);
      unaryOp();
      setState(397);
      unaryExp();
      break;
    }

    case 4: {
      enterOuterAlt(_localctx, 4);
      setState(399);
      _la = _input->LA(1);
      if (!(_la == MiniCParser::T_INC

      || _la == MiniCParser::T_DEC)) {
      _errHandler->recoverInline(this);
      }
      else {
        _errHandler->reportMatch(this);
        consume();
      }
      setState(400);
      lVal();
      break;
    }

    case 5: {
      enterOuterAlt(_localctx, 5);
      setState(401);
      lVal();
      setState(402);
      _la = _input->LA(1);
      if (!(_la == MiniCParser::T_INC

      || _la == MiniCParser::T_DEC)) {
      _errHandler->recoverInline(this);
      }
      else {
        _errHandler->reportMatch(this);
        consume();
      }
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

//----------------- UnaryOpContext ------------------------------------------------------------------

MiniCParser::UnaryOpContext::UnaryOpContext(ParserRuleContext *parent, size_t invokingState)
  : ParserRuleContext(parent, invokingState) {
}

tree::TerminalNode* MiniCParser::UnaryOpContext::T_ADD() {
  return getToken(MiniCParser::T_ADD, 0);
}

tree::TerminalNode* MiniCParser::UnaryOpContext::T_SUB() {
  return getToken(MiniCParser::T_SUB, 0);
}

tree::TerminalNode* MiniCParser::UnaryOpContext::T_NOT() {
  return getToken(MiniCParser::T_NOT, 0);
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
  enterRule(_localctx, 74, MiniCParser::RuleUnaryOp);
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
    setState(406);
    _la = _input->LA(1);
    if (!((((_la & ~ 0x3fULL) == 0) &&
      ((1ULL << _la) & 2293760) != 0))) {
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

tree::TerminalNode* MiniCParser::PrimaryExpContext::T_FLOAT_LITERAL() {
  return getToken(MiniCParser::T_FLOAT_LITERAL, 0);
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
  enterRule(_localctx, 76, MiniCParser::RulePrimaryExp);

#if __cplusplus > 201703L
  auto onExit = finally([=, this] {
#else
  auto onExit = finally([=] {
#endif
    exitRule();
  });
  try {
    setState(415);
    _errHandler->sync(this);
    switch (_input->LA(1)) {
      case MiniCParser::T_L_PAREN: {
        enterOuterAlt(_localctx, 1);
        setState(408);
        match(MiniCParser::T_L_PAREN);
        setState(409);
        expr();
        setState(410);
        match(MiniCParser::T_R_PAREN);
        break;
      }

      case MiniCParser::T_FLOAT_LITERAL: {
        enterOuterAlt(_localctx, 2);
        setState(412);
        match(MiniCParser::T_FLOAT_LITERAL);
        break;
      }

      case MiniCParser::T_DIGIT: {
        enterOuterAlt(_localctx, 3);
        setState(413);
        match(MiniCParser::T_DIGIT);
        break;
      }

      case MiniCParser::T_ID: {
        enterOuterAlt(_localctx, 4);
        setState(414);
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

//----------------- RealParamContext ------------------------------------------------------------------

MiniCParser::RealParamContext::RealParamContext(ParserRuleContext *parent, size_t invokingState)
  : ParserRuleContext(parent, invokingState) {
}

MiniCParser::ExprContext* MiniCParser::RealParamContext::expr() {
  return getRuleContext<MiniCParser::ExprContext>(0);
}

tree::TerminalNode* MiniCParser::RealParamContext::T_STRING_LITERAL() {
  return getToken(MiniCParser::T_STRING_LITERAL, 0);
}


size_t MiniCParser::RealParamContext::getRuleIndex() const {
  return MiniCParser::RuleRealParam;
}


std::any MiniCParser::RealParamContext::accept(tree::ParseTreeVisitor *visitor) {
  if (auto parserVisitor = dynamic_cast<MiniCVisitor*>(visitor))
    return parserVisitor->visitRealParam(this);
  else
    return visitor->visitChildren(this);
}

MiniCParser::RealParamContext* MiniCParser::realParam() {
  RealParamContext *_localctx = _tracker.createInstance<RealParamContext>(_ctx, getState());
  enterRule(_localctx, 78, MiniCParser::RuleRealParam);

#if __cplusplus > 201703L
  auto onExit = finally([=, this] {
#else
  auto onExit = finally([=] {
#endif
    exitRule();
  });
  try {
    setState(419);
    _errHandler->sync(this);
    switch (_input->LA(1)) {
      case MiniCParser::T_L_PAREN:
      case MiniCParser::T_ADD:
      case MiniCParser::T_SUB:
      case MiniCParser::T_NOT:
      case MiniCParser::T_INC:
      case MiniCParser::T_DEC:
      case MiniCParser::T_ID:
      case MiniCParser::T_FLOAT_LITERAL:
      case MiniCParser::T_DIGIT: {
        enterOuterAlt(_localctx, 1);
        setState(417);
        expr();
        break;
      }

      case MiniCParser::T_STRING_LITERAL: {
        enterOuterAlt(_localctx, 2);
        setState(418);
        match(MiniCParser::T_STRING_LITERAL);
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

std::vector<MiniCParser::RealParamContext *> MiniCParser::RealParamListContext::realParam() {
  return getRuleContexts<MiniCParser::RealParamContext>();
}

MiniCParser::RealParamContext* MiniCParser::RealParamListContext::realParam(size_t i) {
  return getRuleContext<MiniCParser::RealParamContext>(i);
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
  enterRule(_localctx, 80, MiniCParser::RuleRealParamList);
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
    setState(421);
    realParam();
    setState(426);
    _errHandler->sync(this);
    _la = _input->LA(1);
    while (_la == MiniCParser::T_COMMA) {
      setState(422);
      match(MiniCParser::T_COMMA);
      setState(423);
      realParam();
      setState(428);
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

std::vector<tree::TerminalNode *> MiniCParser::LValContext::T_L_BRACK() {
  return getTokens(MiniCParser::T_L_BRACK);
}

tree::TerminalNode* MiniCParser::LValContext::T_L_BRACK(size_t i) {
  return getToken(MiniCParser::T_L_BRACK, i);
}

std::vector<MiniCParser::ExprContext *> MiniCParser::LValContext::expr() {
  return getRuleContexts<MiniCParser::ExprContext>();
}

MiniCParser::ExprContext* MiniCParser::LValContext::expr(size_t i) {
  return getRuleContext<MiniCParser::ExprContext>(i);
}

std::vector<tree::TerminalNode *> MiniCParser::LValContext::T_R_BRACK() {
  return getTokens(MiniCParser::T_R_BRACK);
}

tree::TerminalNode* MiniCParser::LValContext::T_R_BRACK(size_t i) {
  return getToken(MiniCParser::T_R_BRACK, i);
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
  enterRule(_localctx, 82, MiniCParser::RuleLVal);
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
    setState(429);
    match(MiniCParser::T_ID);
    setState(436);
    _errHandler->sync(this);
    _la = _input->LA(1);
    while (_la == MiniCParser::T_L_BRACK) {
      setState(430);
      match(MiniCParser::T_L_BRACK);
      setState(431);
      expr();
      setState(432);
      match(MiniCParser::T_R_BRACK);
      setState(438);
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
