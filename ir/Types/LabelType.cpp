///
/// @file LabelType.cpp
/// @brief Label名称符号类
///

#include "LabelType.h"

LabelType * LabelType::getType()
{
    static LabelType * oneInstance = new LabelType();
    return oneInstance;
}
