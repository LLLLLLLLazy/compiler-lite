///
/// @file VoidType.cpp
/// @brief void类型描述类
///

#include "VoidType.h"

///
/// @brief 获取类型
/// @return VoidType*
///
VoidType * VoidType::getType()
{
	static VoidType * oneInstance = new VoidType();
	return oneInstance;
}