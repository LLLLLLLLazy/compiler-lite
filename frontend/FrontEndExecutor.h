///
/// @file FrontEndExecutor.h
/// @brief 前端分析执行器的接口类
///
#pragma once

#include <string>

#include "AST.h"

///
/// @brief 前端执行器的接口类
///
class FrontEndExecutor {
public:
	///
	/// @brief 构造函数
	/// @param[in] _filename 源文件路径
	///
	explicit FrontEndExecutor(std::string _filename) : filename(_filename)
	{}

	///
	/// @brief 析构函数
	///
	virtual ~FrontEndExecutor()
	{}

	/// @brief 前端执行器的运行函数
	/// @return true: 成功 false: 失败
	virtual bool run() = 0;

	///
	/// @brief  返回抽象语法树的根
	/// @return ast_node*
	///
	[[nodiscard]] ast_node * getASTRoot() const
	{
		return astRoot;
	}

protected:
	///
	/// @brief 要解析的文件路径
	///
	std::string filename;

	///
	/// @brief  抽象语法树的根
	///
	ast_node * astRoot = nullptr;
};
