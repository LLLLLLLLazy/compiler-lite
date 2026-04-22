///
/// @file User.h
/// @brief 使用Value的User，该User也是Value。函数、指令都是User
///

#pragma once

#include <vector>

#include "Use.h"
#include "Value.h"

class User : public Value {

    std::vector<Use *> operands;

public:
    explicit User(Type * _type);

    std::vector<Use *> & getOperands();

    std::vector<Value *> getOperandsValue();

    int32_t getOperandsNum();

    Value * getOperand(int32_t pos);

    void setOperand(int32_t pos, Value * val);

    void addOperand(Value * val);

    void removeOperand(int pos);

    void removeOperand(Value * val);

    void removeOperand(Use * val);

    void removeOperandRaw(Use * use);

    void removeUse(Use * use);

    void clearOperands();

    void swapTwoOperands();

    void replaceOperand(Value * val, Value * newVal);
};
