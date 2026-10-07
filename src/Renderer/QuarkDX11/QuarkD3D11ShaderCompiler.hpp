#ifndef __QUARK_D3D11_SHADER_COMPILER__
#define __QUARK_D3D11_SHADER_COMPILER__

#if defined(_WIN32)
#include "QuarkD3D11Common.hpp"
#include <d3dcompiler.h>
#include <wrl/client.h>
namespace qci {
class D3D11ShaderCompiler {
public:
    Microsoft::WRL::ComPtr<ID3DBlob> Compile(const char *source, const char *entryPoint,
                                             const char *profile) const;
};
} // namespace qci
#endif
#endif // __QUARK_D3D11_SHADER_COMPILER__
