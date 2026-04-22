///
/// @file RegVariable.h
/// @brief 后端物理寄存器对应的Value封装
///

#pragma once

#include <cstdint>
#include <string>

#include "Value.h"

class RegVariable : public Value {

public:
	explicit RegVariable(Type * type, std::string regName, int32_t regNo) : Value(type), regNo(regNo)
	{
		name = std::move(regName);
	}

	[[nodiscard]] int32_t getRegNo() const
	{
		return regNo;
	}

	int32_t getScopeLevel() override
	{
		return 0;
	}

private:
	int32_t regNo;
};
