// slang-emit-cuda.cpp
#include "slang-emit-cuda.h"

#include "../core/slang-writer.h"

#include "slang-emit-source-writer.h"
#include "slang-mangled-lexer.h"

#include <assert.h>

namespace Slang {

static bool _isSingleNameBasicType(IROp op)
{
    switch (op)
    {
        case kIROp_Int64Type:   
        case kIROp_UInt8Type: 
        case kIROp_UInt16Type:
        case kIROp_UIntType: 
        case kIROp_UInt64Type:
        {
            return false;
        }
        default: return true;

    }
}

/* static */ UnownedStringSlice CUDASourceEmitter::getBuiltinTypeName(IROp op)
{
    switch (op)
    {
        case kIROp_VoidType:    return UnownedStringSlice("void");
        case kIROp_BoolType:    return UnownedStringSlice("bool");

        case kIROp_Int8Type:    return UnownedStringSlice("char");
        case kIROp_Int16Type:   return UnownedStringSlice("short");
        case kIROp_IntType:     return UnownedStringSlice("int");
        case kIROp_Int64Type:   return UnownedStringSlice("longlong");

        case kIROp_UInt8Type:   return UnownedStringSlice("uchar");
        case kIROp_UInt16Type:  return UnownedStringSlice("ushort");
        case kIROp_UIntType:    return UnownedStringSlice("uint");
        case kIROp_UInt64Type:  return UnownedStringSlice("ulonglong");

            // Not clear just yet how we should handle half... we want all processing as float probly, but when reading/writing to memory converting
        case kIROp_HalfType:    return UnownedStringSlice("half");

        case kIROp_FloatType:   return UnownedStringSlice("float");
        case kIROp_DoubleType:  return UnownedStringSlice("double");
        default:                return UnownedStringSlice();
    }
}


/* static */ UnownedStringSlice CUDASourceEmitter::getVectorPrefix(IROp op)
{
    switch (op)
    {
        case kIROp_BoolType:    return UnownedStringSlice("bool");

        case kIROp_Int8Type:    return UnownedStringSlice("char");
        case kIROp_Int16Type:   return UnownedStringSlice("short");
        case kIROp_IntType:     return UnownedStringSlice("int");
        case kIROp_Int64Type:   return UnownedStringSlice("longlong");

        case kIROp_UInt8Type:   return UnownedStringSlice("uchar");
        case kIROp_UInt16Type:  return UnownedStringSlice("ushort");
        case kIROp_UIntType:    return UnownedStringSlice("uint");
        case kIROp_UInt64Type:  return UnownedStringSlice("ulonglong");

            // Not clear just yet how we should handle half... we want all processing as float probly, but when reading/writing to memory converting
        case kIROp_HalfType:    return UnownedStringSlice("half");

        case kIROp_FloatType:   return UnownedStringSlice("float");
        case kIROp_DoubleType:  return UnownedStringSlice("double");
        default:                return UnownedStringSlice();
    }
}

SlangResult CUDASourceEmitter::_calcCUDATextureTypeName(IRTextureTypeBase* texType, StringBuilder& outName)
{
    // Not clear how to do this yet
    if (texType->isMultisample())
    {
        return SLANG_FAIL;
    }

    switch (texType->getAccess())
    {
        case SLANG_RESOURCE_ACCESS_READ:
        {
            outName << "CUtexObject";
            return SLANG_OK;
        }
        case SLANG_RESOURCE_ACCESS_READ_WRITE:
        {
            outName << "CUsurfObject";
            return SLANG_OK;
        }
        default: break;
    }
    return SLANG_FAIL;
}

SlangResult CUDASourceEmitter::calcScalarFuncName(HLSLIntrinsic::Op op, IRBasicType* type, StringBuilder& outBuilder)
{
    typedef HLSLIntrinsic::Op Op;

    UnownedStringSlice funcName;
    
    switch (op)
    {
        case Op::FRem:
        {
            if (type->op == kIROp_FloatType || type->op == kIROp_DoubleType)
            {
                funcName = HLSLIntrinsic::getInfo(op).funcName;
            }
            break;
        }
        default: break;
    }

    if (funcName.getLength())
    {
        outBuilder << funcName;
        if (type->op == kIROp_FloatType)
        {
            outBuilder << "f";
        }
        return SLANG_OK;
    }

    // Defer to the supers impl
    return Super::calcScalarFuncName(op, type, outBuilder);
}

SlangResult CUDASourceEmitter::calcTypeName(IRType* type, CodeGenTarget target, StringBuilder& out)
{
    SLANG_UNUSED(target);

    if (target == CodeGenTarget::CSource)
    {
        return Super::calcTypeName(type, target, out);
    }

    // We allow C source, because if we need a name 
    SLANG_ASSERT(target == CodeGenTarget::CUDASource);

    switch (type->op)
    {
        case kIROp_HalfType:
        {
            // Special case half
            out << getBuiltinTypeName(kIROp_FloatType);
            return SLANG_OK;
        }
        case kIROp_VectorType:
        {
            auto vecType = static_cast<IRVectorType*>(type);
            auto vecCount = int(getIntVal(vecType->getElementCount()));
            const IROp elemType = vecType->getElementType()->op;

            UnownedStringSlice prefix = getVectorPrefix(elemType);
            if (prefix.getLength() <= 0)
            {
                return SLANG_FAIL;
            }
            out << prefix << vecCount;
            return SLANG_OK;
        }

#if 0
        case kIROp_MatrixType:
        {
            auto matType = static_cast<IRMatrixType*>(type);

            auto elementType = matType->getElementType();
            const auto rowCount = int(getIntVal(matType->getRowCount()));
            const auto colCount = int(getIntVal(matType->getColumnCount()));

            out << "Matrix<" << getBuiltinTypeName(elementType->op) << ", " << rowCount << ", " << colCount << ">";
            return SLANG_OK;
        }
        case kIROp_UnsizedArrayType:
        {
            auto arrayType = static_cast<IRUnsizedArrayType*>(type);
            auto elementType = arrayType->getElementType();

            out << "Array<";
            SLANG_RETURN_ON_FAIL(_calcTypeName(elementType, target, out));
            out << ">";
            return SLANG_OK;
        }
#endif
        default:
        {
            if (isNominalOp(type->op))
            {
                out << getName(type);
                return SLANG_OK;
            }

            if (IRBasicType::isaImpl(type->op))
            {
                out << getBuiltinTypeName(type->op);
                return SLANG_OK;
            }

            if (auto texType = as<IRTextureTypeBase>(type))
            {
                // We don't support TextureSampler, so ignore that
                if (texType->op != kIROp_TextureSamplerType)
                {
                    return _calcCUDATextureTypeName(texType, out);
                }
            }

            switch (type->op)
            {
                case kIROp_SamplerStateType:                    out << "SamplerState"; return SLANG_OK;
                case kIROp_SamplerComparisonStateType:          out << "SamplerComparisonState"; return SLANG_OK;
                default: break;
            }

            break;
        }
    }

    return Super::calcTypeName(type, target, out);
}

void CUDASourceEmitter::emitLayoutSemanticsImpl(IRInst* inst, char const* uniformSemanticSpelling)
{
    Super::emitLayoutSemanticsImpl(inst, uniformSemanticSpelling);
}

void CUDASourceEmitter::emitParameterGroupImpl(IRGlobalParam* varDecl, IRUniformParameterGroupType* type)
{
    Super::emitParameterGroupImpl(varDecl, type);
}

void CUDASourceEmitter::emitEntryPointAttributesImpl(IRFunc* irFunc, IREntryPointDecoration* entryPointDecor)
{
    SLANG_UNUSED(irFunc);
    SLANG_UNUSED(entryPointDecor);
}

void CUDASourceEmitter::emitFunctionPreambleImpl(IRInst* inst)
{
    if(inst && inst->findDecoration<IREntryPointDecoration>())
    {
        m_writer->emit("extern \"C\" __global__ ");
    }
    else
    {
        m_writer->emit("__device__ ");
    }
}


void CUDASourceEmitter::emitCall(const HLSLIntrinsic* specOp, IRInst* inst, const IRUse* operands, int numOperands, const EmitOpInfo& inOuterPrec)
{
    switch (specOp->op)
    {
        case HLSLIntrinsic::Op::Init:
        {
            // For CUDA vector types we construct with make_

            auto writer = m_writer;

            IRType* retType = specOp->returnType;

            if (IRVectorType* vecType = as<IRVectorType>(retType))
            {
                if (numOperands == getIntVal(vecType->getElementCount()))
                {
                    // Get the type name
                    writer->emit("make_");
                    emitType(retType);
                    writer->emitChar('(');

                    for (int i = 0; i < numOperands; ++i)
                    {
                        if (i > 0)
                        {
                            writer->emit(", ");
                        }
                        emitOperand(operands[i].get(), getInfo(EmitOp::General));
                    }

                    writer->emitChar(')');
                    return;
                }
            }
            // Just use the default
            break;
        }
        default: break;
    }

    return Super::emitCall(specOp, inst, operands, numOperands, inOuterPrec);
}

void CUDASourceEmitter::emitLoopControlDecorationImpl(IRLoopControlDecoration* decl)
{
    if (decl->getMode() == kIRLoopControl_Unroll)
    {
        m_writer->emit("#pragma unroll\n");
    }
}

static bool _areEquivalent(IRType* a, IRType* b)
{
    if (a == b)
    {
        return true;
    }
    if (a->op != b->op)
    {
        return false;
    }

    switch (a->op)
    {
        case kIROp_VectorType:
        {
            IRVectorType* vecA = static_cast<IRVectorType*>(a);
            IRVectorType* vecB = static_cast<IRVectorType*>(b);
            return getIntVal(vecA->getElementCount()) == getIntVal(vecB->getElementCount()) &&
                _areEquivalent(vecA->getElementType(), vecB->getElementType());
        }
        case kIROp_MatrixType:
        {
            IRMatrixType* matA = static_cast<IRMatrixType*>(a);
            IRMatrixType* matB = static_cast<IRMatrixType*>(b);
            return getIntVal(matA->getColumnCount()) == getIntVal(matB->getColumnCount()) &&
                getIntVal(matA->getRowCount()) == getIntVal(matB->getRowCount()) && 
                _areEquivalent(matA->getElementType(), matB->getElementType());
        }
        default:
        {
            return as<IRBasicType>(a) != nullptr;
        }
    }
}

void CUDASourceEmitter::_emitInitializerListValue(IRType* dstType, IRInst* value)
{
    // When constructing a matrix or vector from a single value this is handled by the default path

    switch (value->op)
    {
        case kIROp_Construct:
        case kIROp_MakeMatrix:
        case kIROp_makeVector:
        {
            IRType* type = value->getDataType();

            // If the types are the same, we can can just break down and use
            if (_areEquivalent(dstType, type))
            {
                if (auto vecType = as<IRVectorType>(type))
                {
                    if (UInt(getIntVal(vecType->getElementCount())) == value->getOperandCount())
                    {
                        _emitInitializerList(vecType->getElementType(), value->getOperands(), value->getOperandCount());
                        return;
                    }
                }
                else if (auto matType = as<IRMatrixType>(type))
                {
                    const Index colCount = Index(getIntVal(matType->getColumnCount()));
                    const Index rowCount = Index(getIntVal(matType->getRowCount()));

                    // TODO(JS): If num cols = 1, then it *doesn't* actually return a vector.
                    // That could be argued is an error because we want swizzling or [] to work.
                    IRType* rowType = m_typeSet.addVectorType(matType->getElementType(), int(colCount));
                    IRVectorType* rowVectorType = as<IRVectorType>(rowType);
                    const Index operandCount = Index(value->getOperandCount());

                    // Can init, with vectors.
                    // For now special case if the rowVectorType is not actually a vector (when elementSize == 1)
                    if (operandCount == rowCount || rowVectorType == nullptr)
                    {
                        // We have to output vectors

                        // Emit the braces for the Matrix struct, contains an row array.
                        m_writer->emit("{\n");
                        m_writer->indent();
                        _emitInitializerList(rowType, value->getOperands(), rowCount);
                        m_writer->dedent();
                        m_writer->emit("\n}");
                        return;
                    }
                    else if (operandCount == rowCount * colCount)
                    {
                        // Handle if all are explicitly defined
                        IRType* elementType = matType->getElementType();                                        
                        IRUse* operands = value->getOperands();

                        // Emit the braces for the Matrix struct, and the array of rows
                        m_writer->emit("{\n");
                        m_writer->indent();
                        m_writer->emit("{\n");
                        m_writer->indent();
                        for (Index i = 0; i < rowCount; ++i)
                        {
                            if (i != 0) m_writer->emit(", ");
                            _emitInitializerList(elementType, operands, colCount);
                            operands += colCount;
                        }
                        m_writer->dedent();
                        m_writer->emit("\n}");
                        m_writer->dedent();
                        m_writer->emit("\n}");
                        return;
                    }
                }
            }
                      
            break;
        }
    }

    // All other cases we just use the default emitting - might not work on arrays defined in global scope on CUDA though
    emitOperand(value, getInfo(EmitOp::General));
}

void CUDASourceEmitter::_emitInitializerList(IRType* elementType, IRUse* operands, Index operandCount)
{
    m_writer->emit("{\n");
    m_writer->indent();

    for (Index i = 0; i < operandCount; ++i)
    {
        if (i != 0) m_writer->emit(", ");
        _emitInitializerListValue(elementType, operands[i].get());
    }

    m_writer->dedent();
    m_writer->emit("\n}");
}

bool CUDASourceEmitter::tryEmitInstExprImpl(IRInst* inst, const EmitOpInfo& inOuterPrec)
{
    switch(inst->op)
    {
        case kIROp_Construct:
        {
            // Simple constructor call
            // On CUDA some of the built in types can't be used as constructors directly

            IRType* type = inst->getDataType();
            if (auto basicType = as<IRBasicType>(type) && !_isSingleNameBasicType(type->op))
            {
                m_writer->emit("(");
                emitType(inst->getDataType());
                m_writer->emit(")");
                emitArgs(inst);
                return true;
            }
            break;
        }
        case kIROp_makeArray:
        {
            IRType* dataType = inst->getDataType();
            IRArrayType* arrayType = as<IRArrayType>(dataType);

            IRType* elementType = arrayType->getElementType();

            // Emit braces for the FixedArray struct. 
            m_writer->emit("{\n");
            m_writer->indent();

            _emitInitializerList(elementType, inst->getOperands(), Index(inst->getOperandCount()));

            m_writer->dedent();
            m_writer->emit("\n}");
            return true;
        }
        case kIROp_WaveMaskBallot:
        {
            _requireCUDASMVersion(SemanticVersion(7, 0));

            m_writer->emit("__ballot_sync(");
            emitOperand(inst->getOperand(0), getInfo(EmitOp::General));
            m_writer->emit(", ");
            emitOperand(inst->getOperand(1), getInfo(EmitOp::General));
            m_writer->emit(")");
            return true;
        }
        case kIROp_WaveMaskMatch:
        {
            _requireCUDASMVersion(SemanticVersion(7, 0));

            m_writer->emit("__match_any_sync(");
            emitOperand(inst->getOperand(0), getInfo(EmitOp::General));
            m_writer->emit(", ");
            emitOperand(inst->getOperand(1), getInfo(EmitOp::General));
            m_writer->emit(")");
            return true;
        }
        default: break;
    }

    return Super::tryEmitInstExprImpl(inst, inOuterPrec);
}

void CUDASourceEmitter::_requireCUDASMVersion(SemanticVersion const& version)
{
    if (version > m_extensionTracker->m_smVersion)
    {
        m_extensionTracker->m_smVersion = version;
    }
}

void CUDASourceEmitter::handleCallExprDecorationsImpl(IRInst* funcValue)
{
    // Does this function declare any requirements on GLSL version or
    // extensions, which should affect our output?

    auto decoratedValue = funcValue;
    while (auto specInst = as<IRSpecialize>(decoratedValue))
    {
        decoratedValue = getSpecializedValue(specInst);
    }

    for (auto decoration : decoratedValue->getDecorations())
    {
        if( auto smDecoration = as<IRRequireCUDASMVersionDecoration>(decoration))
        {
            SemanticVersion version;
            version.setFromInteger(SemanticVersion::IntegerType(smDecoration->getCUDASMVersion()));
            _requireCUDASMVersion(version);
        }
    }
}

void CUDASourceEmitter::emitLayoutDirectivesImpl(TargetRequest* targetReq)
{
    SLANG_UNUSED(targetReq);
}

void CUDASourceEmitter::emitVectorTypeNameImpl(IRType* elementType, IRIntegerValue elementCount)
{
    m_writer->emit(getVectorPrefix(elementType->op));
    m_writer->emit(elementCount);
}

void CUDASourceEmitter::emitSimpleTypeImpl(IRType* type)
{
    m_writer->emit(_getTypeName(type));
}

void CUDASourceEmitter::emitRateQualifiersImpl(IRRate* rate)
{
    if (as<IRGroupSharedRate>(rate))
    {
        m_writer->emit("__shared__ ");
    }
}

void CUDASourceEmitter::emitSimpleFuncParamsImpl(IRFunc* func)
{
    m_writer->emit("(");

    bool hasEmittedParam = false;
    auto firstParam = func->getFirstParam();
    for (auto pp = firstParam; pp; pp = pp->getNextParam())
    {
        auto varLayout = getVarLayout(pp);
        if (varLayout && varLayout->findSystemValueSemanticAttr())
        {
            // If it has a semantic don't output, it will be accessed via a global
            continue;
        }

        if (hasEmittedParam)
            m_writer->emit(", ");

        emitSimpleFuncParamImpl(pp);
        hasEmittedParam = true;
    }

    m_writer->emit(")");
}

void CUDASourceEmitter::emitSimpleFuncImpl(IRFunc* func)
{
    // Skip the CPP impl - as it does some processing we don't need here for entry points.
    CLikeSourceEmitter::emitSimpleFuncImpl(func);
}

void CUDASourceEmitter::emitSemanticsImpl(IRInst* inst)
{
    Super::emitSemanticsImpl(inst);
}

void CUDASourceEmitter::emitInterpolationModifiersImpl(IRInst* varInst, IRType* valueType, IRVarLayout* layout)
{
    Super::emitInterpolationModifiersImpl(varInst, valueType, layout);
}

void CUDASourceEmitter::emitVarDecorationsImpl(IRInst* varDecl)
{
    Super::emitVarDecorationsImpl(varDecl);
}

void CUDASourceEmitter::emitMatrixLayoutModifiersImpl(IRVarLayout* layout)
{
    Super::emitMatrixLayoutModifiersImpl(layout);
}

void CUDASourceEmitter::emitPreprocessorDirectivesImpl()
{
    SourceWriter* writer = getSourceWriter();

    writer->emit("\n");

    {
        List<IRType*> types;
        m_typeSet.getTypes(IRTypeSet::Kind::Matrix, types);

        // Emit the type definitions
        for (auto type : types)
        {
            emitTypeDefinition(type);
        }
    }

    {
        List<const HLSLIntrinsic*> intrinsics;
        m_intrinsicSet.getIntrinsics(intrinsics);
        // Emit all the intrinsics that were used
        for (auto intrinsic : intrinsics)
        {
            _maybeEmitSpecializedOperationDefinition(intrinsic);
        }
    }
}

void CUDASourceEmitter::emitModuleImpl(IRModule* module)
{
    // Setup all built in types used in the module
    m_typeSet.addAllBuiltinTypes(module);
    // If any matrix types are used, then we need appropriate vector types too.
    m_typeSet.addVectorForMatrixTypes();

    // We need to add some vector intrinsics - used for calculating thread ids 
    {
        IRType* type = m_typeSet.addVectorType(m_typeSet.getBuilder().getBasicType(BaseType::UInt), 3);
        IRType* args[] = { type, type };

        _addIntrinsic(HLSLIntrinsic::Op::Add,  type, args, SLANG_COUNT_OF(args));
        _addIntrinsic(HLSLIntrinsic::Op::Mul,  type, args, SLANG_COUNT_OF(args));
    }

    // TODO(JS): We may need to generate types (for example for matrices)

    // TODO(JS): We need to determine which functions we need to inline

    // The IR will usually come in an order that respects
    // dependencies between global declarations, but this
    // isn't guaranteed, so we need to be careful about
    // the order in which we emit things.

    List<EmitAction> actions;

    computeEmitActions(module, actions);


    _emitForwardDeclarations(actions);

    // Output group shared variables

    {
        for (auto action : actions)
        {
            if (action.level == EmitAction::Level::Definition && action.inst->op == kIROp_GlobalVar && as<IRGroupSharedRate>(action.inst->getRate()))
            {
                emitGlobalInst(action.inst);   
            }
        }
    }

    {
        // Output all the thread locals 
        for (auto action : actions)
        {
            if (action.level == EmitAction::Level::Definition && action.inst->op == kIROp_GlobalVar && !as<IRGroupSharedRate>(action.inst->getRate()))
            {
                emitGlobalInst(action.inst);
            }
        }

        // Finally output the functions as methods on the context
        for (auto action : actions)
        {
            if (action.level == EmitAction::Level::Definition && as<IRFunc>(action.inst))
            {
                emitGlobalInst(action.inst);
            }
        }
    }
}


} // namespace Slang
