///
/// @file IntegerType.cpp
/// @brief 整型类型类，可描述1位的i1类型或32位的int类型
///

#include "IntegerType.h"

///
/// @brief 获取类型i1
/// @return VoidType*
///
IntegerType * IntegerType::getTypeInt1()
{
	static IntegerType * oneInstanceInt1 = new IntegerType(1);
	return oneInstanceInt1;
}

///
/// @brief 获取类型i32
/// @return VoidType*
///
IntegerType * IntegerType::getTypeInt32()
{
	static IntegerType * oneInstanceInt32 = new IntegerType(32);
	return oneInstanceInt32;
}
