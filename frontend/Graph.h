///
/// @file Graph.h
/// @brief 利用graphviz图形化显示AST的头文件
///
#pragma once

#include <string>

#include "AST.h"

///
/// @brief 抽象语法树AST的图形化显示，这里用C语言实现
/// @param root 抽象语法树的根
/// @param filePath 转换成图形的文件名，主要要通过文件名后缀来区分图片的类型，如png，svg，pdf等皆可
///
void OutputAST(ast_node * root, std::string filePath);
