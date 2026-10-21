// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 WarcraftXL
//
// The shared half of wxl.object: the thunks behind the table, over a runtime's leaf reads.

#include "wxl/runtime/object_service.hpp"

namespace wxl::runtime
{
    ObjectService* ObjectService::instance_ = nullptr;

    wxl_result ObjectService::Gate(wxl_handle handle, uint32_t kinds, void*& native)
    {
        uint32_t kind = 0;
        native        = Object(handle, &kind);
        if (!native) return WXL_E_NOT_FOUND;
        return kind < 32u && (kinds & (1u << kind)) != 0 ? WXL_OK : WXL_E_INVALID;
    }

    template <class V, auto Read, uint32_t Kinds>
    wxl_result WXL_CALL ObjectService::Get(wxl_handle handle, V* out)
    {
        if (!out) return WXL_E_INVALID;

        void*            native = nullptr;
        const wxl_result gated  = Gate(handle, Kinds, native);
        if (gated != WXL_OK) return gated;
        return (instance_->binding_.*Read)(native, *out) ? WXL_OK : WXL_E_INVALID;
    }

    template <class V, auto Read, uint32_t Kinds>
    wxl_result WXL_CALL ObjectService::GetAt(wxl_handle handle, uint32_t index, V* out)
    {
        if (!out) return WXL_E_INVALID;

        void*            native = nullptr;
        const wxl_result gated  = Gate(handle, Kinds, native);
        if (gated != WXL_OK) return gated;
        return (instance_->binding_.*Read)(native, index, *out) ? WXL_OK : WXL_E_INVALID;
    }

    ObjectService::ObjectService(ObjectBinding& binding) : binding_(binding)
    {
        instance_ = this;

        api_.struct_size = sizeof(api_);

        api_.valid        = &ObjectService::ValidThunk;
        api_.guid         = &ObjectService::GuidThunk;
        api_.type         = &ObjectService::TypeThunk;
        api_.from_guid    = &ObjectService::FromGuidThunk;
        api_.local_player = &ObjectService::LocalPlayerThunk;

        api_.name        = &ObjectService::NameThunk;
        api_.unit_target = &ObjectService::UnitTargetThunk;
        api_.unit_reaction = &ObjectService::UnitReactionThunk;
        api_.enumerate   = &ObjectService::EnumerateThunk;
        api_.positions   = &ObjectService::PositionsThunk;

#define WXL_OBJECT_READ(kinds, member, Method, type) \
    api_.member = &Get<type, &ObjectBinding::Method, k##kinds>;
#define WXL_OBJECT_READ_AT(kinds, member, Method, type) \
    api_.member = &GetAt<type, &ObjectBinding::Method, k##kinds>;
#include "wxl/runtime/object_reads.def"
#undef WXL_OBJECT_READ
#undef WXL_OBJECT_READ_AT
    }

    ObjectService::~ObjectService()
    {
        if (instance_ == this) instance_ = nullptr;
    }

    wxl_handle ObjectService::Track(void* object)
    {
        if (!object) return WXL_HANDLE_NONE;
        if (const wxl_handle known = handles_.HandleOf(object)) return known;

        // The guid and the kind are read once, here, and kept with the handle: the guid so that
        // FromGuid is a lookup and not a walk of a set thousands deep in a city, the kind so that
        // no getter has to ask the client for it again.
        return handles_.Track(object, binding_.Guid(object),
                              static_cast<uint32_t>(binding_.Type(object)));
    }

    void ObjectService::Untrack(void* object) { handles_.Untrack(object); }

    void ObjectService::Untrack(wxl_handle handle) { handles_.Untrack(handle); }

    void ObjectService::Clear() { handles_.Clear(); }

    wxl_handle ObjectService::FromGuid(wxl_guid guid) const
    {
        if (guid == WXL_GUID_NONE) return WXL_HANDLE_NONE;
        return handles_.FindByKey(guid);
    }

    void* ObjectService::Object(wxl_handle handle, uint32_t* kind)
    {
        return instance_ ? instance_->handles_.Resolve(handle, kind) : nullptr;
    }

    wxl_bool WXL_CALL ObjectService::ValidThunk(wxl_handle object)
    {
        return Object(object) ? WXL_TRUE : WXL_FALSE;
    }

    wxl_guid WXL_CALL ObjectService::GuidThunk(wxl_handle object)
    {
        void* native = Object(object);
        return native ? instance_->binding_.Guid(native) : WXL_GUID_NONE;
    }

    uint32_t WXL_CALL ObjectService::TypeThunk(wxl_handle object)
    {
        uint32_t kind = WXL_OBJECT_UNKNOWN;
        return Object(object, &kind) ? kind : static_cast<uint32_t>(WXL_OBJECT_UNKNOWN);
    }

    wxl_handle WXL_CALL ObjectService::FromGuidThunk(wxl_guid guid)
    {
        return instance_ ? instance_->FromGuid(guid) : WXL_HANDLE_NONE;
    }

    wxl_handle WXL_CALL ObjectService::LocalPlayerThunk(void)
    {
        if (!instance_) return WXL_HANDLE_NONE;

        void* player = instance_->binding_.LocalPlayer();
        return player ? instance_->handles_.HandleOf(player) : WXL_HANDLE_NONE;
    }

    wxl_result WXL_CALL ObjectService::NameThunk(wxl_handle object, wxl_str* out)
    {
        if (!out) return WXL_E_INVALID;

        void* native = Object(object);
        if (!native) return WXL_E_NOT_FOUND;

        const char* text = nullptr;
        uint32_t    size = 0;
        if (!instance_->binding_.Name(native, text, size)) return WXL_E_INVALID;

        out->data = text;
        out->size = size;
        return WXL_OK;
    }

    wxl_result WXL_CALL ObjectService::UnitTargetThunk(wxl_handle unit, wxl_handle* out)
    {
        if (!out) return WXL_E_INVALID;

        void*            native = nullptr;
        const wxl_result gated  = Gate(unit, kUnits, native);
        if (gated != WXL_OK) return gated;

        wxl_guid target = WXL_GUID_NONE;
        if (!instance_->binding_.UnitTarget(native, target)) return WXL_E_INVALID;
        *out = instance_->FromGuid(target);
        return WXL_OK;
    }

    wxl_result WXL_CALL ObjectService::UnitReactionThunk(wxl_handle unit, wxl_handle other, uint32_t* out)
    {
        if (!out) return WXL_E_INVALID;

        void*      native = nullptr;
        void*      seen   = nullptr;
        wxl_result gated  = Gate(unit, kUnits, native);
        if (gated != WXL_OK) return gated;
        gated = Gate(other, kUnits, seen);
        if (gated != WXL_OK) return gated;

        if (!instance_->binding_.UnitReaction(native, seen, *out)) return WXL_E_UNSUPPORTED;
        return WXL_OK;
    }

    uint32_t WXL_CALL ObjectService::EnumerateThunk(uint32_t type, wxl_handle* out, uint32_t cap)
    {
        if (!instance_) return 0;

        uint32_t matched = 0;
        instance_->handles_.ForEach(
            [&](wxl_handle handle, void*, uint32_t kind)
            {
                if (type != WXL_OBJECT_UNKNOWN && kind != type) return;

                if (out && matched < cap) out[matched] = handle;
                ++matched;
            });
        return matched;
    }

    wxl_result WXL_CALL ObjectService::PositionsThunk(const wxl_handle* objects, uint32_t count,
                                                      wxl_vec3* out)
    {
        if (!objects || !out) return WXL_E_INVALID;
        if (!instance_) return WXL_E_UNSUPPORTED;

        for (uint32_t i = 0; i < count; ++i)
        {
            out[i] = wxl_vec3{0.0f, 0.0f, 0.0f};

            void* native = Object(objects[i]);
            if (native) instance_->binding_.Position(native, out[i]);
        }
        return WXL_OK;
    }
} // namespace wxl::runtime
