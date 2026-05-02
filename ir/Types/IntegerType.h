///
/// @file IntegerType.h
/// @brief 整型类型类，可描述1位的i1类型或32位的int类型
///

#pragma once

#include <cstdint>

#include "Type.h"

class IntegerType final : public Type {

public:
	///
	/// @brief 获取类型，全局只有一份
	/// @return VoidType*
	///
	static IntegerType * getTypeInt1();

	///
	/// @brief 获取类型，全局只有一份
	/// @return VoidType*
	///
	static IntegerType * getTypeInt32();

	///
	/// @brief 获取类型的IR标识符
	/// @return std::string IR标识符void
	///
	[[nodiscard]] std::string toString() const override
	{
		return "i" + std::to_string(bitWidth);
	}

	///
	/// @brief 获取整数的位宽
	/// @return int32_t
	///
	[[nodiscard]] int32_t getBitWidth() const
	{
		return this->bitWidth;
	}

	///
	/// @brief 是否是 i1 类型，也就是 1 位整数类型
	/// @return true
	/// @return false
	///
	[[nodiscard]] bool isInt1Type() const override
	{
		return bitWidth == 1;
	}

	///
	/// @brief 是否是int类型，也就是32位整数类型
	/// @return true
	/// @return false
	///
	[[nodiscard]] bool isInt32Type() const override
	{
		return bitWidth == 32;
	}

	///
	/// @brief 获得类型所占内存空间大小
	/// @return int32_t
	///
	[[nodiscard]] int32_t getSize() const override
	{
		return 4;
	}

private:
	///
	/// @brief 构造函数
	///
	explicit IntegerType(int32_t _bitWidth) : Type(Type::IntegerTyID), bitWidth(_bitWidth)
	{}

	///
	/// @brief 位宽
	///
	int32_t bitWidth;
};
