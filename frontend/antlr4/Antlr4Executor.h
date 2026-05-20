///
/// @file Antlr4Executor.h
/// @brief antlr4的词法与语法分析解析器
///

#include "FrontEndExecutor.h"

class Antlr4Executor : public FrontEndExecutor {
public:
	Antlr4Executor(std::string filename, bool extendedGrammar = false)
	    : FrontEndExecutor(filename), extendedGrammar(extendedGrammar)
	{}
	virtual ~Antlr4Executor()
	{}

	/// @brief 前端词法与语法解析生成AST
	/// @return true: 成功 false：错误
	bool run() override;

private:
	bool extendedGrammar;
};
