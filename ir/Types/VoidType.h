///
/// @file VoidType.h
/// @brief void类型描述类
///
#pragma once

#include "Type.h"

class VoidType : public Type {

public:
	///
	/// @brief 获取类型，全局只有一份
	/// @return VoidType*
	///
	static VoidType * getType();

	///
	/// @brief 获取类型的IR标识符
	/// @return std::string IR标识符void
	///
	[[nodiscard]] std::string toString() const override
	{
		return "void";
	}

private:
	///
	/// @brief 构造函数
	///
	explicit VoidType() : Type(Type::VoidTyID)
	{}
};
