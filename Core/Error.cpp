#include "Error.hpp"
#include "Log.hpp"
#include "String.hpp"

#include <comdef.h>

namespace Xor
{
    namespace detail
    {
        bool checkImpl(bool cond, const char * fmt, ...)
        {
            if (!cond)
            {
                print("ERROR: ");

                if (fmt)
                {
                    va_list ap;
                    va_start(ap, fmt);
                    vprint(fmt, ap);
                    va_end(ap);
                }
                else
                {
                    print("Unknown error");
                }

                print("\n");

                return false;
            }
            else
            {
                return true;
            }
        }

        bool checkHRImpl(HRESULT hr)
        {
            if (!SUCCEEDED(hr))
            {
                _com_error err(hr);
                return checkImpl(false, "%s", err.ErrorMessage());
            }
            else
            {
                return true;
            }
        }

        bool checkLastErrorImpl(bool cond)
        {
            if (!cond)
                return checkHRImpl(HRESULT_FROM_WIN32(GetLastError()));
            else
                return true;
        }

    }

    String errorMessage(HRESULT hr)
    {
        _com_error err(hr);
        return String(err.ErrorMessage());
    }
}
