///
/// @file User.cpp
/// @brief 使用Value的User，该User也是Value。函数、指令都是User
///

#include "User.h"

#include <algorithm>

/// @brief 构造一个可使用其他 Value 的 User 对象
/// @param _type 当前 User 的值类型
User::User(Type * _type) : Value(_type)
{}

/// @brief 析构函数，负责释放所有操作数 Use 边
User::~User()
{
    clearOperands();
}

/// @brief 设置指定位置的操作数值
/// @param pos 操作数位置
/// @param val 新的操作数值
void User::setOperand(int32_t pos, Value * val)
{
    if (pos < static_cast<int32_t>(operands.size())) {
        operands[pos]->setUsee(val);
    }
}

/// @brief 在末尾追加一个操作数
/// @param val 待追加的操作数值
void User::addOperand(Value * val)
{
    auto use = new Use(val, this);
    operands.push_back(use);
    val->addUse(use);
}

/// @brief 按值删除一个操作数
/// @param val 待删除的操作数值
void User::removeOperand(Value * val)
{
    for (auto & use: operands) {
        if (use->getUsee() == val) {
            use->remove();
            delete use;
            break;
        }
    }
}

/// @brief 删除指定 Use 对应的操作数
/// @param use 待删除的 Use 对象
void User::removeOperand(Use * use)
{
    removeUse(use);
    delete use;
}

/// @brief 按位置删除一个操作数
/// @param pos 操作数位置
void User::removeOperand(int pos)
{
    if (pos < static_cast<int32_t>(operands.size())) {
        Use * use = operands[pos];
        use->remove();
        delete use;
    }
}

/// @brief 仅从操作数列表中移除 Use，不修改其引用关系
/// @param use 待移除的 Use 对象
void User::removeOperandRaw(Use * use)
{
    auto pIter = std::find(operands.begin(), operands.end(), use);
    if (pIter != operands.end()) {
        operands.erase(pIter);
    }
}

/// @brief 删除并解除一个 Use 引用关系
/// @param use 待解除的 Use 对象
void User::removeUse(Use * use)
{
    auto pIter = std::find(operands.begin(), operands.end(), use);
    if (pIter != operands.end()) {
        use->remove();
    }
}

/// @brief 清空全部操作数
void User::clearOperands()
{
    while (!operands.empty()) {
        Use * use = operands.front();
        use->remove();
        delete use;
    }
}

/// @brief 获取操作数 Use 列表
/// @return 操作数列表引用
std::vector<Use *> & User::getOperands()
{
    return operands;
}

/// @brief 获取操作数对应的 Value 列表
/// @return 仅包含 Value 的操作数数组
std::vector<Value *> User::getOperandsValue()
{
    std::vector<Value *> operandsVec;
    operandsVec.reserve(operands.size());
    for (auto & use: operands) {
        operandsVec.emplace_back(use->getUsee());
    }
    return operandsVec;
}

/// @brief 获取操作数个数
/// @return 当前操作数数量
int32_t User::getOperandsNum()
{
    return static_cast<int32_t>(operands.size());
}

/// @brief 获取指定位置的操作数值
/// @param pos 操作数位置
/// @return 指定位置的值，不存在时返回空指针
Value * User::getOperand(int32_t pos)
{
    if (pos < static_cast<int32_t>(operands.size())) {
        return operands[pos]->getUsee();
    }

    return nullptr;
}

/// @brief 交换两个操作数的位置
void User::swapTwoOperands()
{
    if (operands.size() == 2) {
        std::swap(operands[0], operands[1]);
    }
}

/// @brief 将某个旧操作数替换为新操作数
/// @param val 旧操作数
/// @param newVal 新操作数
void User::replaceOperand(Value * val, Value * newVal)
{
    for (auto & use: operands) {
        if (use->getUsee() == val) {
            use->setUsee(newVal);
            break;
        }
    }
}
