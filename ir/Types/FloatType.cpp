#include "FloatType.h"

FloatType * FloatType::getTypeFloat()
{
    static FloatType instance;
    return &instance;
}