#ifndef KAUAI_CLIPBOARD_VIEW_HOST_H
#define KAUAI_CLIPBOARD_VIEW_HOST_H

#include <exception>
#include <new>

struct ClipboardViewHost
{
    std::exception_ptr exception;

    template <class Function> int Guard(Function function)
    {
        try
        {
            return function();
        }
        catch (const std::bad_alloc &)
        {
            exception = std::current_exception();
            return -1;
        }
        catch (...)
        {
            exception = std::current_exception();
            return -2;
        }
    }

    int Check(int result)
    {
        if (exception)
            std::rethrow_exception(exception);
        return result;
    }

    static int Format(PClipboardObject clipboard, long type, void **owned)
    {
        PDocumentBase document;
        if (!clipboard->FGetFormat(type, owned ? &document : nullptr))
            return 0;
        if (owned)
            *owned = document;
        return 1;
    }
};

#endif
