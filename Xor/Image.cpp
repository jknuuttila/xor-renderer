#include "Image.hpp"
#include "Core/Core.hpp"

#include "external/FreeImage/FreeImage.h"

namespace xor
{
    struct Image::State
    {
        FIBITMAP *bitmap = nullptr;

        State() = default;
        ~State()
        {
            release();
        }

        State(const State &) = delete;
        State &operator=(const State &) = delete;
        State(State &&) = delete;
        State &operator=(State &&) = delete;

        void release()
        {
            if (bitmap)
            {
                FreeImage_Unload(bitmap);
                bitmap = nullptr;
            }
        }
    };

    static int defaultFlagsForFormat(FREE_IMAGE_FORMAT format)
    {
        switch (format)
        {
        case FIF_PNG:
            return PNG_IGNOREGAMMA;
        default:
            return 0;
        }
    }

    Image::Image(const String & filename)
    {
        auto format = FreeImage_GetFileType(filename.cStr());
        auto flags  = defaultFlagsForFormat(format);

        m_state         = std::make_shared<State>();
        m_state->bitmap = FreeImage_Load(format, filename.cStr(), flags);
    }

    uint2 Image::size() const
    {
        return 
        {
            FreeImage_GetWidth(m_state->bitmap),
            FreeImage_GetHeight(m_state->bitmap)
        };
    }
}
