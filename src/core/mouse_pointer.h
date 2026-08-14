#ifndef __MOUSE_POINTER_H__INCLUDED__
#define __MOUSE_POINTER_H__INCLUDED__

#include "types.h"

#include <stdint.h>

#include <ace/managers/sprite.h>

#include <mtl/memory.h>

#include "core/asset_cache.h"
#include "core/game_data.h"
#include "game_data_format.h"

namespace NEONengine
{
    /**
     * @brief The fixed cursor roles, in the order 'CURS' assigns them.
     *
     * The enumerators are the record's own ids, so a hotspot's cursor field
     * converts straight to one of these. Only the engine selects WAIT; a
     * hotspot may name any of the roles before it.
     */
    enum class pointer_role : u16
    {
        DEFAULT,
        LOOK,
        USE,
        TALK,
        GOTO,
        WAIT,
        COUNT
    };

    class mouse_pointer;
    using mouse_pointer_ptr = mtl::unique_ptr<mouse_pointer>;

    /**
     * @brief The hardware pointer, and one cut frame per role.
     */
    class mouse_pointer
    {
        public:  //////////////////////////////////////////////////////////////////////////////////
        /**
         * @brief Builds the cursors from the project's own 'CURS' sheet.
         *
         * Reads the sheet named by 'UIST', cuts the frame each role points at,
         * and records that role's click offset. The sheet's colors are baked
         * against the shared UI palette the engine loads at startup, so none
         * travels with it.
         *
         * Reads the pak and takes Chip RAM, so construct this under a system-use
         * guard. A project with no usable sheet leaves the pointer inert rather
         * than failing: ready() says which happened.
         */
        mouse_pointer(game_data const& data, asset_cache& cache);

        /** @brief Frees the cursors. Hands memory back to the OS. */
        ~mouse_pointer();

        mouse_pointer(mouse_pointer const&)            = delete;
        mouse_pointer& operator=(mouse_pointer const&) = delete;

        /** @brief Whether there are cursors to show. */
        bool ready() const noexcept { return _ready; }

        /**
         * @brief Shows @p role, keeping its click offset aligned.
         */
        void switch_to(pointer_role role);

        /**
         * @brief Shows or hides the hardware pointer.
         *
         * A scene that shows no UI shows no cursor either, which is what SCEN's
         * flags bit 1 describes. Hiding matters beyond taste there: such a scene
         * may author its background across the pointer's palette band, so a
         * cursor left on screen would draw itself in the background's colors.
         *
         * The sprites are published by the next update(), so nothing has to be
         * done here to make it take effect.
         */
        void set_visible(bool visible);

        /** @brief Moves the pointer to the mouse. Call once per frame. */
        void update();

        private:  /////////////////////////////////////////////////////////////////////////////////
        static constexpr int ROLE_COUNT = to<int>(pointer_role::COUNT);

        tBitMap* _low[ROLE_COUNT]{};
        tBitMap* _high[ROLE_COUNT]{};
        tSprite* _sprite0{ nullptr };
        tSprite* _sprite1{ nullptr };  ///< Attached to _sprite0.

        /// Each role's click offset within its frame, from its CURS record.
        data::point<u16> _hotspots[ROLE_COUNT]{};

        pointer_role _current{ pointer_role::DEFAULT };
        bool         _ready{ false };
        bool         _visible{ false };  ///< Tracks spriteSetEnabled to avoid needless regens.
    };
}  // namespace NEONengine

#endif  // __MOUSE_POINTER_H__INCLUDED__
