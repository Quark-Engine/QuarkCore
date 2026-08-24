#include "QuarkD3D11ShaderCompiler.hpp"

#include <cstring>

#if defined(_WIN32)
namespace qc {

Microsoft::WRL::ComPtr<ID3DBlob> D3D11ShaderCompiler::Compile(const char *source,
                                                              const char *entryPoint,
                                                              const char *profile) const
{
    TraceLog(LogLevel::Trace,
             "SHADER",
             TextFormat("[D3D11] Compiling HLSL shader (Entry: %s, Profile: %s, Source: %zu bytes)",
                        entryPoint,
                        profile,
                        std::strlen(source)));

    Microsoft::WRL::ComPtr<ID3DBlob> bytecode;
    Microsoft::WRL::ComPtr<ID3DBlob> errors;

    const HRESULT result = D3DCompile(source, std::strlen(source), nullptr, nullptr, nullptr,
                                      entryPoint, profile, 0, 0, &bytecode, &errors);

    if (FAILED(result) && errors)
    {
        TraceLog(LogLevel::Error, "SHADER", static_cast<const char *>(errors->GetBufferPointer()));
    }

    d3d11::ThrowIfFailed(result, "D3DCompile");

    TraceLog(LogLevel::Info,
             "SHADER",
             TextFormat("[D3D11] HLSL shader compiled successfully (Profile: %s, Bytecode: %zu bytes)",
                        profile,
                        bytecode->GetBufferSize()));

    return bytecode;
}

} // namespace qc
#endif