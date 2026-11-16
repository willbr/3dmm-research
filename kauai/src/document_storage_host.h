#ifndef DOCUMENT_STORAGE_HOST_H
#define DOCUMENT_STORAGE_HOST_H
// Typed allocator boundary shared by owning document collections. Read-only
// operations can omit binding so an activation callback may destroy its owner.
struct DocumentStorageHost
{
    MMCollectionStorage *storage;
    MMStorageAllocator oldAllocator, provider;
    std::exception_ptr exception;
    int status = 1;
    explicit DocumentStorageHost(MMCollectionStorage *p)
        : storage(p), oldAllocator(p ? p->allocator : MMStorageAllocator{}), provider(Group::CollectionAllocator())
    {
        if (!storage)
            return;
        storage->allocator = {this,
                              [](void *v, void *data, MMU32 oldBytes, MMU32 newBytes, int priority) -> void * {
                                  auto &h = *static_cast<DocumentStorageHost *>(v);
                                  if (h.status < 0)
                                      return nullptr;
                                  void *out = nullptr;
                                  h.Guard([&] {
                                      out = h.provider.resize(h.provider.context, data, oldBytes, newBytes, priority);
                                      return 1;
                                  });
                                  return out;
                              },
                              [](void *v, void *data, MMU32 bytes) {
                                  auto &h = *static_cast<DocumentStorageHost *>(v);
                                  h.Guard([&] {
                                      h.provider.release(h.provider.context, data, bytes);
                                      return 1;
                                  });
                              }};
    }
    ~DocumentStorageHost()
    {
        if (storage)
            storage->allocator = oldAllocator;
    }
    template <class F> int Guard(F f)
    {
        try
        {
            return f();
        }
        catch (const std::bad_alloc &)
        {
            exception = std::current_exception();
            status = -1;
            return status;
        }
        catch (...)
        {
            exception = std::current_exception();
            status = -2;
            return status;
        }
    }
    void Rethrow()
    {
        if (exception)
            std::rethrow_exception(exception);
    }
};
#endif
