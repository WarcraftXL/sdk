# Publish a service

A service is a table of C functions an extension publishes under a name and a version, for other
extensions to call. The core publishes its own the same way (`wxl.storage`, `wxl.objects`,
`wxl.settings`...).

Example: `examples/service` publishes `example.tally` and uses it. Here both sides are in one
extension; another extension would hold only the "use" side and a copy of the header.

## 1. The header

A C struct of function pointers, `structSize` and `apiVersion` first, every function `__cdecl`, only
C types in the signatures:

<!-- quote: examples/service/include/TallyApi.h -->
```c
#define TALLY_API_NAME    "example.tally"
#define TALLY_API_VERSION 1

typedef struct TallyApi
{
    uint32_t structSize; ///< members are only ever appended: check it before using a newer one
    uint32_t apiVersion;
    void(__cdecl* Add)(int32_t amount);
    int32_t(__cdecl* Total)(void);
} TallyApi;
```

Beside it, the C++ side: a small class over the table, and the `ServiceTraits` that give its name and
version, so users write `Service<Tally>` and never touch the table:

<!-- quote: examples/service/include/Tally.hpp -->
```cpp
class Tally
{
public:
    Tally() = default;
    explicit Tally(const TallyApi* api) : m_api(api) {}

    void    Add(int32_t amount) const { m_api->Add(amount); }
    int32_t Total() const { return m_api->Total(); }

private:
    const TallyApi* m_api = nullptr;
};

template <>
struct wxl::ServiceTraits<Tally>
{
    using Table                           = TallyApi;
    static constexpr const char* kName    = TALLY_API_NAME;
    static constexpr uint32_t    kVersion = TALLY_API_VERSION;
};
```

Ship both headers with the extension that publishes the service; users copy them into their own
sources. A service without a wrapper names its table instead (`using Table = TallyApi` in
`ServiceTraits<TallyApi>`), and `Service<TallyApi>` hands out the table itself.

## 2. Publish

A table that lives as long as the process, published from `OnStart`:

<!-- quote: examples/service/src/Publish.cpp -->
```cpp
    const TallyApi g_tally = { sizeof(TallyApi), TALLY_API_VERSION, &Add, &Total };
}

bool PublishTally()
{
    return Publish<Tally>(&g_tally);
}
```

## 3. Use

`Service<T>` looks the service up on first use and keeps it: `Get()`, `operator->` and `if (service)`.
Extensions load one after the other, so a service may not be published yet when yours starts:
`OnReady` runs its callback once it is, now or later (when every extension has loaded, or on a later
frame).

<!-- quote: examples/service/src/Use.cpp -->
```cpp
class TallyUser : public Extension
{
    Service<Tally> tally;

    void OnStart() override
    {
        // Not published yet: the callback runs once it is, here when every extension has loaded.
        tally.OnReady([](Tally& t)
        {
            t.Add(2);
            t.Add(3);
            Log::Info("total {}", t.Total());
        });
        PublishTally();
    }
};
```

The core's services have their wrappers in the SDK: `Service<Storage>` (`wxl/Storage.hpp`),
`Service<Profiles>` (`wxl/Profiles.hpp`), `Service<AllSettings>` (`wxl/AllSettings.hpp`); the objects
service is behind the object handles, and an extension's own settings behind `Settings<S>`.

The fake host of `wxl_add_load_test` lists what an extension publishes and asks for
(`publishes: example.tally v1`, `asks: example.tally v1`).

## Versions

A lookup answers the highest published version **at or above** the one asked for.

- **Adding** functions: append members at the end of the struct and raise the version. Old users,
  asking for the old version, get the new table and never see the new members. A user of a new member
  asks for the new version, or checks `structSize` before calling it.
- **Changing** or removing a function breaks every user: publish a new name instead (for example
  `example.tally2`), and keep the old one while it has users.
- `wxl_add_abi_lock` (README, "CMake functions") keeps the table's layout in a checked-in lock file
  and fails a test when it changes without its version moving.
- When an extension is disabled, the core withdraws its services from later lookups; a
  `Service<T>` that has already found the table keeps it.
