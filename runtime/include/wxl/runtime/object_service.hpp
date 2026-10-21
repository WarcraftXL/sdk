// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 WarcraftXL
//
// The shared half of wxl.object: handles, type gating, enumeration, over a runtime's leaf reads.

#ifndef WXL_RUNTIME_OBJECT_SERVICE_HPP
#define WXL_RUNTIME_OBJECT_SERVICE_HPP

#include <cstdint>

#include "wxl/abi.h"
#include "wxl/events.h"
#include "wxl/runtime/handles.hpp"
#include "wxl/services/object.h"

namespace wxl::runtime
{
    namespace detail
    {
        /// One bit per wxl_object_type, for the kinds a getter accepts.
        constexpr uint32_t KindBit(wxl_object_type type) { return 1u << static_cast<uint32_t>(type); }
    } // namespace detail

    /**
     * @brief The leaf reads a runtime supplies: the five below, and one per row of
     *        object_reads.def.
     *
     * Each read takes the native object a handle resolved to, never a handle and never a null,
     * and is only asked about an object of a kind its row accepts, so an implementation may cast
     * without checking. A read that does not apply answers false and leaves @p out alone.
     */
    class ObjectBinding
    {
    public:
        virtual ~ObjectBinding() = default;

        virtual wxl_guid        Guid(void* object) const = 0;
        virtual wxl_object_type Type(void* object) const = 0;

        /// The display name. The binding owns the storage and keeps it valid until its next
        /// call, which is what the service promises a mod.
        virtual bool Name(void* object, const char*& out, uint32_t& size) const = 0;

        /// The guid a unit targets; WXL_GUID_NONE for nothing. The service resolves it.
        virtual bool UnitTarget(void* object, wxl_guid& out) const = 0;

        /// The object the local player controls, or nullptr while out of the world.
        virtual void* LocalPlayer() const = 0;

        /// How one unit regards another (wxl_object_api::unit_reaction). A binding that cannot
        /// tell answers false.
        virtual bool UnitReaction(void* unit, void* other, uint32_t& out) const
        {
            (void)unit;
            (void)other;
            (void)out;
            return false;
        }

#define WXL_OBJECT_READ(kinds, member, Method, type) \
    virtual bool Method(void* object, type& out) const = 0;
#define WXL_OBJECT_READ_AT(kinds, member, Method, type) \
    virtual bool Method(void* object, uint32_t index, type& out) const = 0;
#include "wxl/runtime/object_reads.def"
#undef WXL_OBJECT_READ
#undef WXL_OBJECT_READ_AT
    };

    /**
     * @brief Serves wxl.object on top of a binding.
     *
     * One per process. The C struct a mod receives holds plain function pointers with nowhere to
     * carry a context, so the thunks reach the single live instance; a client has one object
     * manager, so this costs nothing real.
     *
     * An object's kind is read once, when it is tracked, and kept with its handle: the client
     * never changes it, and every getter checks it.
     *
     * Not thread-safe, like the handle table under it: the runtime drives it from the thread
     * that owns the client's objects, and a mod reads it from there.
     */
    class ObjectService
    {
    public:
        explicit ObjectService(ObjectBinding& binding);
        ~ObjectService();

        ObjectService(const ObjectService&)            = delete;
        ObjectService& operator=(const ObjectService&) = delete;

        /// The table to publish under WXL_OBJECT_SERVICE.
        const wxl_object_api* Api() const { return &api_; }

        // --- what the runtime drives ----------------------------------------------------------
        //
        // The runtime calls these from wherever its client creates and destroys objects. The
        // service does not go looking: it is told.

        /// Starts tracking an object and returns its handle.
        wxl_handle Track(void* object);

        /// Stops tracking it. Outstanding handles to it go stale from here on.
        void Untrack(void* object);

        /// The same, for a caller that has the handle already and need not be charged a lookup.
        void Untrack(wxl_handle handle);

        /// Drops everything, as when leaving the world.
        void Clear();

        /// The handle an object already has, for a runtime filling in an event's args.
        wxl_handle HandleOf(void* object) const { return handles_.HandleOf(object); }

        /// The handle of the tracked object carrying a guid, or WXL_HANDLE_NONE.
        wxl_handle FromGuid(wxl_guid guid) const;

        const HandleTable& Handles() const { return handles_; }

    private:
        static ObjectService* instance_;

        /// The kinds a getter accepts, one bit per wxl_object_type; the `kinds` column of
        /// object_reads.def names one of these.
        static constexpr uint32_t kAny         = 0xFFu;
        static constexpr uint32_t kUnits       = detail::KindBit(WXL_OBJECT_UNIT) | detail::KindBit(WXL_OBJECT_PLAYER);
        static constexpr uint32_t kPlayers     = detail::KindBit(WXL_OBJECT_PLAYER);
        static constexpr uint32_t kItems       = detail::KindBit(WXL_OBJECT_ITEM) | detail::KindBit(WXL_OBJECT_CONTAINER);
        static constexpr uint32_t kContainers  = detail::KindBit(WXL_OBJECT_CONTAINER);
        static constexpr uint32_t kGameObjects = detail::KindBit(WXL_OBJECT_GAMEOBJECT);
        static constexpr uint32_t kDynamics    = detail::KindBit(WXL_OBJECT_DYNAMIC);
        static constexpr uint32_t kCorpses     = detail::KindBit(WXL_OBJECT_CORPSE);

        /// Resolves a handle to its object, or nullptr; and, when asked, its kind.
        static void* Object(wxl_handle handle, uint32_t* kind = nullptr);

        /// Resolves a handle whose kind is in a mask. WXL_E_NOT_FOUND for a handle that does not
        /// resolve, WXL_E_INVALID for one of another kind.
        static wxl_result Gate(wxl_handle handle, uint32_t kinds, void*& native);

        /**
         * @brief One getter of the table: resolves, gates on kind, reads through the binding.
         *
         * @tparam V      the out type the getter writes.
         * @tparam Read   the ObjectBinding member that supplies it, as bool (void*, V&) const.
         * @tparam Kinds  the kinds the getter accepts.
         */
        template <class V, auto Read, uint32_t Kinds>
        static wxl_result WXL_CALL Get(wxl_handle handle, V* out);

        /// The same for an indexed read, as bool (void*, uint32_t, V&) const.
        template <class V, auto Read, uint32_t Kinds>
        static wxl_result WXL_CALL GetAt(wxl_handle handle, uint32_t index, V* out);

        static wxl_bool   WXL_CALL ValidThunk(wxl_handle object);
        static wxl_guid   WXL_CALL GuidThunk(wxl_handle object);
        static uint32_t   WXL_CALL TypeThunk(wxl_handle object);
        static wxl_handle WXL_CALL FromGuidThunk(wxl_guid guid);
        static wxl_handle WXL_CALL LocalPlayerThunk(void);
        static wxl_result WXL_CALL NameThunk(wxl_handle object, wxl_str* out);
        static wxl_result WXL_CALL UnitTargetThunk(wxl_handle unit, wxl_handle* out);
        static wxl_result WXL_CALL UnitReactionThunk(wxl_handle unit, wxl_handle other, uint32_t* out);
        static uint32_t   WXL_CALL EnumerateThunk(uint32_t type, wxl_handle* out, uint32_t cap);
        static wxl_result WXL_CALL PositionsThunk(const wxl_handle* objects, uint32_t count, wxl_vec3* out);

        ObjectBinding& binding_;
        HandleTable    handles_;
        wxl_object_api api_{};
    };
} // namespace wxl::runtime

#endif // WXL_RUNTIME_OBJECT_SERVICE_HPP
