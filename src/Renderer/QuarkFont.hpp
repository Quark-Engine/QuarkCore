#ifndef __QUARK_FONT__
#define __QUARK_FONT__

#include <cstdint>

namespace qci
{
    struct IFont
    {
        uint32_t m_Id = 0;

        bool IsValid() const
        {
            return m_Id != 0;
        }
    };

    struct FontMetrics
    {
        float m_Ascent = 0.0f;
        float m_Descent = 0.0f;
        float m_LineHeight = 0.0f;
        float m_PixelSize = 0.0f;
    };
} // qci

#endif // __QUARK_FONT__
