#ifndef __DIALOGUE_PRESENTATION_H__INCLUDED__
#define __DIALOGUE_PRESENTATION_H__INCLUDED__

#include "types.h"

#include <ace++/bitmap.h>
#include <ace++/font.h>

#include <mtl/expected.h>
#include <mtl/utility.h>

#include "core/dialogue_portraits.h"
#include "core/dialogue_session.h"
#include "core/screen.h"
#include "utils/bstr_view.h"

namespace NEONengine
{
    class engine;

    /**
     * @brief What a frame of dialogue input asks the conversation to do.
     */
    enum class dialogue_action_kind : u8
    {
        NONE,
        ADVANCE,
        TAKE_CHOICE,
        ROTATE_CHOICES
    };

    /**
     * @brief One frame's input, in the conversation's own terms.
     */
    struct dialogue_action
    {
        dialogue_action_kind kind{ dialogue_action_kind::NONE };

        /// The available-choice index TAKE_CHOICE asks for; NO_CHOICE otherwise.
        u8 choice{ dialogue_session::NO_CHOICE };
    };

    /**
     * @brief Owns the dialogue's drawing, its input and display restoration.
     */
    class dialogue_presentation
    {
        public:  //////////////////////////////////////////////////////////////////////////////////

        using page_result = mtl::expected<bool, char const*>;

        /**
         * @brief Borrows the engine without acquiring resources; the engine must outlive this.
         */
        explicit dialogue_presentation(engine& engine) noexcept;

        NO_COPY(dialogue_presentation)
        NO_MOVE(dialogue_presentation)

        /**
         * @brief Closes any remaining presentation and releases its resources.
         */
        ~dialogue_presentation();

        /**
         * @brief Loads and draws the first page. Call once at entry.
         * Consumes the opening input only on a visible entry. Returns false
         * without acquiring the display when no page is drawable.
         */
        page_result open(dialogue_session& session);

        /**
         * @brief Redraws the current page after a successful open.
         * Returns false when the conversation should close.
         */
        page_result refresh(dialogue_session& session);

        /**
         * @brief Redraws a choice set using the page's retained glyphs and width.
         * Requires the same successfully built page, with only its choice set changed.
         * Use refresh() after page navigation.
         */
        page_result redraw(dialogue_session const& session);

        /**
         * @brief Reports whether this presentation owns the frozen display.
         */
        bool is_open() const noexcept { return !!_display; }

        /**
         * @brief Reads one frame of input and says what the player asked for.
         * Reads and consumes nothing before a successful open. A keyboard
         * selection is held highlighted for DIALOGUE_CHOICE_HOLD_FRAMES,
         * answering NONE until the last of them.
         */
        dialogue_action update();

        /**
         * @brief Restores pixels and colors before releasing the display and resources.
         * Safe to call before opening or more than once.
         */
        void close();

        private:  /////////////////////////////////////////////////////////////////////////////////

        /// No drawn slot; also clears the highlight.
        static constexpr u8 NO_SLOT = 0xFF;

        // Slots borrow glyphs from the page and numbers from the engine.
        struct drawn_slot
        {
            tTextBitMap* _number{ nullptr };
            tTextBitMap* _text{ nullptr };
            bool _rotates{ false };
            u8 _choice{ 0 };
            u16 _x{ 0 };
            u16 _text_x{ 0 };
            u16 _y{ 0 };
            u16 _width{ 0 };
            u16 _height{ 0 };
        };

        /**
         * @brief Finds a slot at display-space coordinates, or returns NO_SLOT.
         * Coordinates include the screen's vertical offset.
         */
        u8 hit_test(u16 x, u16 y) const;

        /**
         * @brief Highlights a drawn slot, or clears the highlight for NO_SLOT.
         */
        void set_highlight(u8 slot);

        /**
         * @brief Turns a drawn slot into the action it stands for.
         */
        dialogue_action action_for_slot(u8 slot) const noexcept;

        /**
         * @brief The room a page's content has inside the frame.
         */
        struct page_space
        {
            u16 max_content_width{ 0 };  ///< The widest a line of content may be drawn.
            u16 content_height{ 0 };     ///< The tallest all of the content together may be drawn.
        };

        /**
         * @brief Builds page layout and glyphs, returning false when nothing is drawable.
         */
        page_result build_page(dialogue_session& session);

        /**
         * @brief Places the frame beside any portrait and reports the room left inside it.
         */
        page_space measure_page_space(data::page const& page);

        /**
         * @brief Builds the name and body, clipping whole lines at the screen limit.
         */
        mtl::expected<void, char const*> build_page_text(data::page const& page,
            bstr_view const& text_string, page_space const& space);

        /**
         * @brief Retains every drawable choice and fits them below the name and body.
         */
        mtl::expected<void, char const*> build_choices(dialogue_session& session,
            page_space const& space);

        /**
         * @brief Maps the current set to retained glyphs and returns its height with gaps.
         */
        u16 build_slots(dialogue_session const& session);

        /**
         * @brief Sizes the frame around the built content and renders it when there is one.
         */
        mtl::expected<void, char const*> build_frame(u16 choices_height);

        /**
         * @brief Publishes speaker colors and draws the page, optionally updating the portrait.
         */
        void draw_page(bool draw_portrait);

        /**
         * @brief Draws a slot's retained number and text glyphs in the given color.
         */
        void draw_slot(u8 slot, u8 color);

        /**
         * @brief Restores the scene beneath the frame and clears its stored bounds.
         */
        void erase_frame();

        /**
         * @brief Restores the scene beneath the portrait without changing palette colors.
         */
        void erase_portrait();

        /**
         * @brief Frees page glyphs and the frame, clearing slots, highlight and any held selection.
         */
        void release_page();

        /**
         * @brief Clears slot mappings, highlight and held input without releasing glyphs.
         */
        void clear_slots();

        /**
         * @brief Looks up the localized speaker name, returning an empty view for an invalid ID.
         */
        bstr_view speaker_name(u16 speaker_id) const;

        /**
         * @brief Publishes the speaker's text color, or the saved color when no speaker exists.
         */
        void apply_speaker_color(u16 speaker_id);

        engine& _engine;
        freeze_display _display;
        dialogue_portraits_ptr _portraits{ nullptr };
        ace::text_bitmap_ptr _text{ nullptr };
        ace::text_bitmap_ptr _heading{ nullptr };
        // unique_ptr requires explicit null initialization.
        ace::text_bitmap_ptr _choices[dialogue_session::MAX_CHOICES]{
            nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr,
            nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr,
            nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr,
            nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr
        };
        ace::text_bitmap_ptr _more{ nullptr };
        ace::bitmap_ptr _frame{ nullptr };
        drawn_slot _slots[dialogue_session::MAX_CHOICES_SHOWN]{};
        u16 _frame_x{ 0 };
        u16 _frame_y{ 0 };
        u16 _frame_width{ 0 };
        u16 _frame_height{ 0 };
        u16 _content_width{ 0 };
        u16 _heading_height{ 0 };
        u16 _choice_text_x{ 0 };
        u16 _portrait_width{ 0 };
        u16 _speaker{ data::NO_ID };
        ULONG _held_text_color{ 0 };
        u8 _slot_count{ 0 };
        u8 _highlight{ NO_SLOT };
        u8 _pending{ NO_SLOT };   ///< The slot a keyboard selection is holding.
        u8 _hold_frames{ 0 };     ///< Frames left before the held slot is taken.
    };
}  // namespace NEONengine

#endif  // __DIALOGUE_PRESENTATION_H__INCLUDED__
