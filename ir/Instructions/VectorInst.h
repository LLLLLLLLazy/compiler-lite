///
/// @file VectorInst.h
/// @brief RVV 向量 IR 指令
///

#pragma once

#include <cstdint>

#include "Instruction.h"

class Value;

class VSetVLInst final : public Instruction {
public:
    /// @brief 设置本轮 RVV VL，avl 表示剩余待处理元素数
    VSetVLInst(Function * func, Value * avl);
    Value * getAVL();
    void toString(std::string & str) override;
};

class VectorLoadInst final : public Instruction {
public:
    /// @brief 向量加载，stride 为按元素计数的步长，1 表示连续 load
    VectorLoadInst(Function * func, Value * ptr, Value * vl, Type * elemType, int32_t stride = 1);
    Value * getPointerOperand();
    Value * getVL();
    Type * getElementType() const;
    int32_t getStride() const { return stride; }
    void toString(std::string & str) override;

private:
    Type * elemType = nullptr;
    int32_t stride = 1;
};

class VectorStoreInst final : public Instruction {
public:
    /// @brief 向量存储，stride 为按元素计数的步长，1 表示连续 store
    VectorStoreInst(Function * func, Value * value, Value * ptr, Value * vl, int32_t stride = 1);
    Value * getValueOperand();
    Value * getPointerOperand();
    Value * getVL();
    int32_t getStride() const { return stride; }
    void toString(std::string & str) override;

private:
    int32_t stride = 1;
};

class VectorSplatInst final : public Instruction {
public:
    /// @brief 将循环不变量或标量值广播为当前 VL 下的向量值
    VectorSplatInst(Function * func, Value * scalar, Value * vl, Type * elemType);
    Value * getScalarOperand();
    Value * getVL();
    Type * getElementType() const;
    void toString(std::string & str) override;

private:
    Type * elemType = nullptr;
};

class VectorBinaryInst final : public Instruction {
public:
    /// @brief 向量二元运算；preserveLhsTail 用于归约累加器保留未激活 lane
    VectorBinaryInst(Function * func,
                     IRInstOperator scalarOp,
                     Value * lhs,
                     Value * rhs,
                     Type * vectorType,
                     Value * vl,
                     bool preserveLhsTail = false);
    IRInstOperator getScalarOp() const { return scalarOp; }
    bool shouldPreserveLhsTail() const { return preserveLhsTail; }
    Value * getLHS();
    Value * getRHS();
    Value * getVL();
    void toString(std::string & str) override;

private:
    IRInstOperator scalarOp = IRInstOperator::IRINST_OP_MAX;
    bool preserveLhsTail = false;
};

class VectorReduceInst final : public Instruction {
public:
    /// @brief 将向量横向归约到 lane0，结果仍用单 lane 向量承载以便后续 extract
    VectorReduceInst(Function * func, IRInstOperator scalarOp, Value * value, Value * init, Type * vectorType, Value * vl);
    IRInstOperator getScalarOp() const { return scalarOp; }
    Value * getValueOperand();
    Value * getInitOperand();
    Value * getVL();
    void toString(std::string & str) override;

private:
    IRInstOperator scalarOp = IRInstOperator::IRINST_OP_MAX;
};

class VectorExtractInst final : public Instruction {
public:
    /// @brief 提取 lane0 为标量，主要用于向量归约后的最终回写
    VectorExtractInst(Function * func, Value * vector, Type * scalarType);
    Value * getVectorOperand();
    void toString(std::string & str) override;
};
