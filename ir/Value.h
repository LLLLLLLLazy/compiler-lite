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
    explicit Value(Type * _type);
    virtual ~Value();

    [[nodiscard]] virtual std::string getName() const;

    [[nodiscard]] const std::vector<Use *> & getUseList() const
    {
        return uses;
    }

    void setName(std::string _name);

    [[nodiscard]] virtual std::string getIRName() const;

    void setIRName(std::string _name);

    virtual Type * getType();

    void addUse(Use * use);

    void removeUse(Use * use);

    void removeUses();

    void replaceAllUseWith(Value * new_val);

    virtual int32_t getScopeLevel();
};
