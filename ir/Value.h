///
/// @file Value.h
/// @brief 值操作类型，所有的变量、函数、常量都是Value
///

#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "Type.h"

class Use;

class Value {

protected:
    std::string name;
    std::string IRName;
    Type * type;
    std::vector<Use *> uses;

public:
    /// @brief 构造一个 Value 对象
    explicit Value(Type * _type);

    /// @brief 析构函数
    virtual ~Value();

    /// @brief 获取值名称
    [[nodiscard]] virtual std::string getName() const;

    /// @brief 获取使用当前值的 Use 列表
    [[nodiscard]] const std::vector<Use *> & getUseList() const
    {
        return uses;
    }

    /// @brief 设置值名称
    void setName(std::string _name);

    /// @brief 获取值的 IR 名称
    [[nodiscard]] virtual std::string getIRName() const;

    /// @brief 设置值的 IR 名称
    void setIRName(std::string _name);

    /// @brief 获取值类型
    virtual Type * getType();

    /// @brief 添加一条使用当前值的 Use 边
    void addUse(Use * use);

    /// @brief 删除一条使用当前值的 Use 边
    void removeUse(Use * use);

    /// @brief 删除当前值的全部 Use 边
    void removeUses();

    /// @brief 将当前值的所有使用替换为另一个值
    void replaceAllUseWith(Value * new_val);

    /// @brief 获取值所在的作用域层级
    virtual int32_t getScopeLevel();
};
