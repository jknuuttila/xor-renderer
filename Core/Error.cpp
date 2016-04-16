#include "Error.hpp"
#include "Log.hpp"

#include <comdef.h>

namespace xor
{
    namespace detail
    {
        bool checkImpl(bool cond, const char * fmt, ...)
        {
            if (!cond)
            {
                log("ERROR: ");

                if (fmt)
                {
                    va_list ap;
                    va_start(ap, fmt);
                    vlog(fmt, ap);
                    va_end(ap);
                }
                else
                {
                    log("Unknown error");
                }

                log("\n");

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

        bool checkLastErrorImpl()
        {
            return checkHRImpl(HRESULT_FROM_WIN32(GetLastError()));
        }

    }

}
