///
/// @file Value.cpp
/// @brief 值操作类型，所有的变量、函数、常量都是Value
///

#include "Value.h"

#include <algorithm>

#include "Use.h"
#include "User.h"

/// @brief 构造一个 Value 对象
/// @param _type 当前值的类型
Value::Value(Type * _type) : type(_type)
{}

/// @brief 析构函数
Value::~Value() = default;

/// @brief 获取值名称
/// @return 当前值名称
std::string Value::getName() const
{
    return name;
}

/// @brief 设置值名称
/// @param _name 新名称
void Value::setName(std::string _name)
{
    this->name = std::move(_name);
}

/// @brief 获取值的 IR 名称
/// @return 当前值的 IR 名称
std::string Value::getIRName() const
{
    return IRName;
}

/// @brief 设置值的 IR 名称
/// @param _name 新的 IR 名称
void Value::setIRName(std::string _name)
{
    this->IRName = std::move(_name);
}

/// @brief 获取值类型
/// @return 当前值的类型对象
Type * Value::getType()
{
    return type;
}

/// @brief 添加一条使用当前值的 Use 边
/// @param use 新增的 Use 对象
void Value::addUse(Use * use)
{
    uses.push_back(use);
}

/// @brief 删除一条使用当前值的 Use 边
/// @param use 待删除的 Use 对象
void Value::removeUse(Use * use)
{
    auto pIter = std::find(uses.begin(), uses.end(), use);
    if (pIter != uses.end()) {
        uses.erase(pIter);
    }
}

/// @brief 删除当前值的全部 Use 边
void Value::removeUses()
{
    while (!uses.empty()) {
        Use * use = uses.front();
        use->remove();
        delete use;
    }
}

/// @brief 获取值所在的作用域层级
/// @return 默认返回 -1，表示未绑定具体作用域层级
int32_t Value::getScopeLevel()
{
    return -1;
}

/// @brief 将当前值的所有使用替换为另一个值
/// @param new_val 用于替换的新值
void Value::replaceAllUseWith(Value * new_val)
{
    std::vector<Use *> usesCopy = uses;
    for (auto use: usesCopy) {
        auto user = dynamic_cast<User *>(use->getUser());
        if (!user) {
            continue;
        }

        for (int32_t index = 0; index < user->getOperandsNum(); ++index) {
            if (user->getOperand(index) == this) {
                user->setOperand(index, new_val);
                break;
            }
        }
    }
}
