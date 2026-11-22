#ifndef __QUARK_TEXTURE__
#define __QUARK_TEXTURE__

#include <cstdint>

namespace qci
{
    struct ITexture
    {
        uint32_t m_Id = 0;
        int m_Width = 0;
        int m_Height = 0;
        int m_Mipmaps = 1;
        int m_Format = 0;
        bool m_Valid = false;

        bool IsValid() const
        {
            return m_Id != 0 && m_Width > 0 && m_Height > 0 && m_Valid;
        }
    };

    struct IRenderTexture
    {
        unsigned int m_Id = 0;
        ITexture m_Texture;
        unsigned int m_DepthId = 0;
    };
} // qci

#endif // __QUARK_TEXTURE__
