// main.cpp

#include <stdio.h>

// This file implements an extremely simple example of loading and
// executing a Slang shader program on the CPU. 
//
// More information about generation C++ or CPU code can be found in docs/cpu-target.md
//
// NOTE! This test will only run on a system correctly where slang can find a suitable
// C++ compiler - such as clang/gcc/visual studio
//
// The comments in the file will attempt to explain concepts as
// they are introduced.
//
// Of course, in order to use the Slang API, we need to include
// its header. We have set up the build options for this project
// so that it is as simple as:
#include <slang.h>

// Allows use of ComPtr - which we can use to scope any 'com-like' pointers easily
#include <slang-com-ptr.h>
// Provides macros for handling SlangResult values easily 
#include <slang-com-helper.h>

// This includes a useful small function for setting up the prelude (described more further below).
#include "../../source/core/slang-test-tool-util.h"

// Slang namespace is used for elements support code (like core) which we use here
// for ComPtr<> and TestToolUtil
using namespace Slang;

// Slang source is converted into C++ code which is compiled by a backend compiler.
// That process uses a 'prelude' which defines types and functions that are needed
// for everything else to work.
// 
// We include the prelude here, so we can directly use the types as were used by the
// compiled code. It is not necessary to include the prelude, as long as memory is
// laid out in the manner that the generated slang code expects. 
#define SLANG_PRELUDE_NAMESPACE CPPPrelude
#include "../../prelude/slang-cpp-types.h"
#include "gfx/render.h"
#include "gfx/cuda/render-cuda.h"
#include "tools/graphics-app-framework/window.h"

int gWindowWidth = 1024;
int gWindowHeight = 768;

ComPtr<gfx::IRenderer>      gRenderer;
gfx::Window*                gWindow;
RefPtr<gfx::BufferResource> gBuffer;

RefPtr<gfx::ShaderProgram>  gProgram;

RefPtr<gfx::PipelineState>  gPipelineState;

struct UniformState;

static SlangResult _innerMain(int argc, char** argv)
{
    ComPtr<slang::IGlobalSession> slangGlobalSession;
    slangGlobalSession.attach(spCreateSession(NULL));

    TestToolUtil::setSessionDefaultPreludeFromExePath(argv[0], slangGlobalSession);

    SlangCompileRequest* slangPTXRequest = spCreateCompileRequest(slangGlobalSession);

    int targetIndex = spAddCodeGenTarget(slangPTXRequest, SLANG_PTX);
    spSetTargetFlags(slangPTXRequest, targetIndex, SLANG_TARGET_FLAG_GENERATE_WHOLE_PROGRAM);

    int translationUnitIndex = spAddTranslationUnit(slangPTXRequest, SLANG_SOURCE_LANGUAGE_SLANG, nullptr);
    spAddTranslationUnitSourceFile(slangPTXRequest, translationUnitIndex, "shader.slang");
    const SlangResult compilePTXRes = spCompile(slangPTXRequest);

    if (auto diagnostics = spGetDiagnosticOutput(slangPTXRequest))
    {
        printf("%s", diagnostics);
    }

    // If compilation failed, there is no point in continuing any further.
    if (SLANG_FAILED(compilePTXRes))
    {
        spDestroyCompileRequest(slangPTXRequest);
        return compilePTXRes;
    }

    // We build up another compilation request to get the raw CUDA code
    // This raw CUDA code is necessary to build the module description
    slang::TargetDesc targetDesc;
    targetDesc.format = SLANG_CUDA_SOURCE;
    targetDesc.profile = spFindProfile(slangGlobalSession, "sm_5_0");

    slang::SessionDesc sessionDesc;
    sessionDesc.targetCount = 1;
    sessionDesc.targets = &targetDesc;

    ComPtr<slang::ISession> slangSession;
    slangGlobalSession->createSession(sessionDesc, slangSession.writeRef());

    SlangCompileRequest* slangCUDARequest;
    slangSession->createCompileRequest(&slangCUDARequest);

    int translationCUDAUnitIndex = spAddTranslationUnit(slangCUDARequest, SLANG_SOURCE_LANGUAGE_SLANG, nullptr);
    spAddTranslationUnitSourceFile(slangCUDARequest, translationCUDAUnitIndex, "shader.slang");
    const SlangResult compileCUDARes = spCompile(slangCUDARequest);

    if (auto diagnostics = spGetDiagnosticOutput(slangCUDARequest))
    {
        printf("%s", diagnostics);
    }

    // If compilation failed, there is no point in continuing any further.
    if (SLANG_FAILED(compileCUDARes))
    {
        spDestroyCompileRequest(slangCUDARequest);
        return compileCUDARes;
    }

    // Here we use the actual raw CUDA code to get the module information
    ComPtr<slang::IModule> slangModule;
    spCompileRequest_getModule(slangCUDARequest, translationCUDAUnitIndex, slangModule.writeRef());

    ComPtr<slang::IEntryPoint> entryPoint;
    slangModule->findEntryPointByName("computeMain", entryPoint.writeRef());
    if (!entryPoint) return SLANG_FAIL;

    ComPtr<slang::IComponentType> linkedProgram;
    entryPoint->link(linkedProgram.writeRef());
    if (!linkedProgram) return SLANG_FAIL;

    ISlangBlob* computeShaderBlob = nullptr;
    spGetTargetCodeBlob(slangPTXRequest, 0, &computeShaderBlob);
    if (!computeShaderBlob) return SLANG_FAIL;

    char const* computeCode = (char const*) computeShaderBlob->getBufferPointer();
    char const* computeCodeEnd = computeCode + computeShaderBlob->getBufferSize();

    spDestroyCompileRequest(slangPTXRequest);

    struct UniformState
    {
        CPPPrelude::RWStructuredBuffer<float> ioBuffer;
    };

    UniformState uniformState;

    // The contents of the buffer are modified, so we'll copy it
    const float startBufferContents[] = { 3.0f, -7.0f, -4.0f, 9.0f };
    float bufferContents[SLANG_COUNT_OF(startBufferContents)];
    memcpy(bufferContents, startBufferContents, sizeof(startBufferContents));

    uniformState.ioBuffer.data = bufferContents;
    uniformState.ioBuffer.count = SLANG_COUNT_OF(bufferContents);

    const CPPPrelude::uint3 startGroupID = { 0, 0, 0};
    const CPPPrelude::uint3 endGroupID = { 1, 1, 1 };

    gfx::createCUDARenderer(gRenderer.writeRef());
    
    gfx::WindowDesc windowDesc;
    windowDesc.title = "CUDA Hello, World!";
    windowDesc.width = gWindowWidth;
    windowDesc.height = gWindowHeight;
    gWindow = gfx::createWindow(windowDesc);

    gfx::IRenderer::Desc rendererDesc;
    rendererDesc.width = gWindowWidth;
    rendererDesc.height = gWindowHeight;
    {
        Result res = gRenderer->initialize(rendererDesc, getPlatformWindowHandle(gWindow));
        if(SLANG_FAILED(res)) return res;
    }

    gfx::ShaderProgram::KernelDesc kernelDescs[] =
    {
        { gfx::StageType::Compute, computeCode, computeCodeEnd },
    };

    kernelDescs[0].entryPointName = "computeMain";
    gfx::ShaderProgram::Desc programDesc;
    programDesc.pipelineType = gfx::PipelineType::Compute;
    programDesc.kernels = &kernelDescs[0];
    programDesc.kernelCount = 1;

    gProgram = gRenderer->createProgram(programDesc);

    // Actually build the shader object using the layout specified by the module
    auto programLayout = linkedProgram->getLayout();
    if (!programLayout) return SLANG_FAIL;
    auto shaderObjectLayout = gRenderer->createRootShaderObjectLayout(programLayout);
    if (!shaderObjectLayout) return SLANG_FAIL;
    auto shaderObject = gRenderer->createRootShaderObject(shaderObjectLayout);
    if (!shaderObject) return SLANG_FAIL;

    int bufferSize = 4 * sizeof(float);
    gfx::BufferResource::Desc bufferDesc;
    bufferDesc.init(bufferSize);
    bufferDesc.setDefaults(gfx::Resource::Usage::UnorderedAccess);
    bufferDesc.cpuAccessFlags = gfx::Resource::AccessFlag::Read | gfx::Resource::AccessFlag::Write;
    // bufferContents holds the output
    gBuffer = gRenderer->createBufferResource(
        gfx::Resource::Usage::UnorderedAccess,
        bufferDesc,
        bufferContents);
    if (!gBuffer) return SLANG_FAIL;

    gfx::ResourceView::Desc bufferViewDesc;
    bufferViewDesc.type = gfx::ResourceView::Type::UnorderedAccess;
    auto bufferView = gRenderer->createBufferView(gBuffer, bufferViewDesc);

    gfx::ShaderOffset offset;
    shaderObject->setResource(offset, bufferView);

    gfx::ComputePipelineStateDesc desc;
    desc.program = gProgram;
    desc.rootShaderObjectLayout = shaderObjectLayout;
    auto pipelineState = gRenderer->createComputePipelineState(desc);
    if(!pipelineState) return SLANG_FAIL;

    gPipelineState = pipelineState;

    gRenderer->bindRootShaderObject(gfx::PipelineType::Compute, shaderObject);

    gRenderer->setPipelineState(gfx::PipelineType::Compute, gPipelineState);
    gRenderer->dispatchCompute(4, 1, 1);
    gRenderer->waitForGpu();

    // Print out the values before the computation
    printf("Before:\n");
    for (float v : startBufferContents)
    {
        printf("%f, ", v);
    }
    printf("\n");

    // Print out the values the the kernel produced
    printf("After: \n");
    for (float v : bufferContents)
    {
        printf("%f, ", v);
    }
    printf("\n");

    return SLANG_OK;
}

int main(int argc, char** argv)
{
    return SLANG_SUCCEEDED(_innerMain(argc, argv)) ? 0 : -1;
}
