#ifndef __NOIR_VM_H__INCLUDED__
#define __NOIR_VM_H__INCLUDED__

#include "types.h"

#include <stdint.h>

#include <ace/types.h>

#include "mtl/expected.h"
#include "mtl/memory.h"
#include "mtl/utility.h"
#include "mtl/vector.h"

namespace NEONengine
{
    // ─── Tuning ─────────────────────────────────────────────────────────────────
    //
    // The numbers worth changing, together so they can be found.

    /// Stack of one script session, in bytes.
    constexpr u16 NOIR_STACK_SIZE = 1024;

    /// Instructions one session may run in one frame.
    constexpr u16 NOIR_SESSION_BUDGET = 10;

    /// Instructions all sessions together may run in one frame. The session
    /// budget is the smaller of the two by design, so no one script can take
    /// the frame from the rest.
    constexpr u16 NOIR_FRAME_BUDGET = 40;

    /// Instructions one execute_now run may spend. It runs outside a frame, so
    /// the frame budget cannot bound it and it needs a cap of its own.
    constexpr u16 NOIR_IMMEDIATE_CAP = 2048;

    /// Longest error message, terminator included. The VM hands the bridge a
    /// buffer this size and keeps nothing; anything longer is cut.
    constexpr size_t NOIR_ERROR_MESSAGE_SIZE = 96;

    // ─── Results ────────────────────────────────────────────────────────────────

    /**
     * @brief Why a session died.
     *
     * update(), execute() and execute_now() hand this back in place of their
     * result.
     */
    enum class noir_error : u8
    {
        none,
        invalid_opcode,    ///< Unknown opcode byte
        invalid_func,      ///< Function ID out of range
        duplicate_script,  ///< A live session is already running that script
        stack_underflow,   ///< Popped more than pushed
        stack_overflow,    ///< Pushed past the end of the session's stack
        division_zero,     ///< Division or modulo by zero
        invalid_address,   ///< PC out of bytecode bounds
        value_oob,         ///< VALUE index out of range
        flag_oob,          ///< FLAG index out of range
        out_of_memory,     ///< No memory for another session's stack
        nested_immediate,  ///< execute_now called from inside another
        immediate_modal,   ///< A deferred modal bridge call under execute_now
        runaway,           ///< An execute_now script ran past its instruction cap
        halt,              ///< The script ran HALT: a case its author says cannot happen
    };

    /**
     * @brief What a call came back with.
     *
     * Two callers share this: an instruction and update(). An error kills the
     * session that raised it; the rest keep running.
     */
    enum class noir_result : u8
    {
        ok,         ///< Nothing to report
        completed,  ///< update(): the run list emptied
        yielded,    ///< update(): sessions remain — call it again next frame
        terminated, ///< update(): a terminal call ran; nothing runs until reset_execution()
        deferred,   ///< An instruction: a deferred bridge call ended the walk
        failed,     ///< An instruction: the session died. update() reports the reason instead.
    };

    /**
     * @brief The machine's own state, as opposed to a session's.
     *
     * It lasts one update, except @c terminated, which is a latch only
     * reset_execution() lifts.
     */
    enum class noir_vm_state : u8
    {
        ok,          ///< Nothing went wrong this update
        failed,      ///< A session died this update
        terminated,  ///< A terminal call ran; nothing runs until reset_execution()
    };


    // ─── Debug statistics ───────────────────────────────────────────────────────

#ifdef GAME_DEBUG
    /**
     * @brief Runtime statistics for tuning. Debug builds only.
     */
    struct noir_debug_stats
    {
        u32 max_stack_used;    ///< Most stack any one session has used, in bytes
        u16 max_stack_script;  ///< The script that used it
        u32 opcode_histogram[256];  ///< Execution count per opcode

        /// Number of consecutive frames that ran out of frame budget. Back to 0 when one
        /// finishes with instructions to spare.
        u16 consecutive_saturated_updates;

        void reset() noexcept;
    };
#endif

    // ─── Engine bridge ──────────────────────────────────────────────────────────

    /**
     * @brief What a bridge call does to the walk.
     *
     * The kind belongs to the function, so the bridge table declares it there
     * and nowhere else. noir_vm::call() reads it and returns the matching
     * result, which stops anyone adding a new deferred call as though it were
     * immediate.
     */
    template<typename Sig>
    struct immediate  ///< Acts inside the opcode and returns
    {
        Sig* fn;
    };

    template<typename Sig>
    struct deferred_modal  ///< Records a request; the scene shows it and holds the VM
    {
        Sig* fn;
    };

    template<typename Sig>
    struct deferred_terminal  ///< Records a request; the scene tears the VM down
    {
        Sig* fn;
    };

    /**
     * @brief Function pointer table bridging the VM to engine systems.
     *
     * The VM calls engine systems through this struct, so it includes no engine
     * headers. Every function pointer must stay valid as long as the VM does.
     *
     * A deferred call records what the script asked for and returns; the opcode
     * then spends the rest of the frame budget, so no session runs again this
     * update. The scene acts on the request at the top of the next frame. A
     * terminal one also stops the VM until the teardown it asked for has
     * happened.
     *
     * @see noir_vm
     */
    struct noir_bridge
    {
        // Inventory
        immediate<void(u16 item_id)> inv_add;
        immediate<void(u16 item_id)> inv_remove;
        immediate<bool(u16 item_id)> inv_has;  ///< true = has item
        immediate<u16(u16 item_id)> inv_count;
        immediate<void(u16 item_id)> inv_clear_item;

        // Scenes
        deferred_terminal<void(u16 scene_id)> goto_scene;
        immediate<u16()> get_scene;
        immediate<void(u16 id)> hotspot_on;
        immediate<void(u16 id)> hotspot_off;
        immediate<void(u16 type)> set_pointer;

        // Dialogue / Text
        deferred_modal<void(u16 string_id, u16 speaker_id)> say;
        deferred_modal<void(u16 dialogue_id)> show_dialog;
        immediate<void(u16 string_id, u16 x, u16 y)> show_text;
        immediate<void()> clear_text;

        // Audio
        immediate<void(u16 music_id)> play_music;
        immediate<void()> stop_music;
        immediate<void(u16 sfx_id)> play_sfx;

        // Screen
        deferred_modal<void(u16 duration_frames)> fade_out;
        deferred_modal<void(u16 duration_frames)> fade_in;
        immediate<void(u16 color_index)> screen_clear;

        // Animation
        immediate<void(u16 anim_id)> anim_play;
        immediate<void(u16 anim_id)> anim_stop;

        // System
        immediate<void(u16 lang_id)> set_lang;
        deferred_modal<void(u16 slot)> save_game;
        deferred_terminal<void(u16 slot)> load_game;
        deferred_terminal<void()> new_game;
        deferred_terminal<void()> quit_game;

        // Diagnostics
        /**
         * @brief A session died; tell the player what happened.
         *
         * No opcode reaches this, so it carries no kind: the VM calls it
         * itself, from the one place a session can die. The message is a
         * NUL-terminated string on the VM's stack, so anything that outlives
         * the call has to be a copy.
         */
        void (*report_error)(char const* message);

#ifdef GAME_DEBUG
        immediate<void(u16 string_id)> debug_log;              ///< Log string table entry
        immediate<void(char const* str, u8 len)> debug_print;  ///< Log raw string
#endif
    };

    // ─── Session state ──────────────────────────────────────────────────────────

    enum class noir_session_state : u8
    {
        running,  ///< Running instructions
        waiting,  ///< WAIT timer active (wait_counter > 0)
        ended,    ///< Ran to its end
        died,     ///< An error killed it. Only fail() puts a session here.
    };

    // ─── noir_session ───────────────────────────────────────────────────────────

    /**
     * @brief One running script: its registers, its stack cursor, its state.
     *
     * FLAGS, VALUES, the bytecode, the function table and the bridge belong to
     * the VM, not to a session.
     *
     * The two byte-sized fields sit last so every wider field lands on an even
     * address and the struct needs no padding.
     */
    struct noir_session
    {
        u16 d[4];               ///< D0–D3
        u16 sp;                 ///< Offset into the session's stack; grows downward
        u16 fp;                 ///< Frame offset into the session's stack
        u16 pc;                 ///< Offset into the shared bytecode
        u16 instructions_left;  ///< Budget remaining, reset each frame
        u16 wait_counter;       ///< >0 = waiting N updates
        u16 script_id;          ///< TOC index this session entered through
        u16 stack_index;        ///< Which stack this session owns
        u8 sr;                  ///< Status register (NZCV----)
        noir_session_state state;
    };

    static_assert(sizeof(noir_session) == 24, "noir_session must stay 24 bytes with no padding");

    // ─── noir_vm ────────────────────────────────────────────────────────────────

    /**
     * @brief NoirScript bytecode virtual machine.
     *
     * Register-based VM for driving game scripts — dialogue, inventory, scene
     * transitions, animations, and game flow. Designed for the Amiga 1200
     * (68020 CPU, big-endian, limited RAM).
     *
     * The VM borrows external memory (FLAGS, VALUES, BYTECODE, TOC, bridge)
     * and owns only the session stacks. All borrowed memory must outlive
     * the VM instance.
     *
     * @par Registers
     * - D0–D3: 16-bit general-purpose data registers
     * - SP/FP: 16-bit offsets into the session's own stack; it grows downward
     * - PC: 16-bit offset into the bytecode, at the next byte to read
     * - SR: 8-bit status register (NZCV----)
     *
     * @par Lifecycle
     * 1. Construct via noir_vm(...)
     * 2. Call execute(func_id) to start a script function
     * 3. Call update() each frame until completed or error
     *
     * @par Sessions
     * Several scripts run at once, each in its own session with its own
     * registers and stack. update() walks the run list handing out slices of
     * instructions until the frame budget is spent or nothing can run.
     *
     * @see noir_bridge
     * @see noir_opcodes.h
     */
    class noir_vm
    {
        public:  ///////////////////////////////////////////////////////////////////////////////////
        /**
         * @brief Construct a VM instance.
         *
         * @param flags           Borrowed bitfield array (game state flags)
         * @param flags_size      Size of flags array in bytes (total bits = flags_size * 8)
         * @param values          Borrowed 16-bit value array (game state values)
         * @param values_count    Number of entries in the values array
         * @param bytecode        Borrowed read-only bytecode blob (all scripts)
         * @param bytecode_size   Size of bytecode blob in bytes
         * @param func_toc        Borrowed array of u32 offsets into bytecode
         * @param func_count      Number of entries in the function TOC
         * @param bridge          Borrowed engine bridge (must outlive VM)
         * @param stack_size      Stack capacity of one session, in bytes
         */
        noir_vm(u8* flags,
            u16 flags_size,
            u16* values,
            u16 values_count,
            u8 const* bytecode,
            u32 bytecode_size,
            u32 const* func_toc,
            u16 func_count,
            noir_bridge const* bridge,
            u16 stack_size = NOIR_STACK_SIZE) noexcept;

        ~noir_vm() noexcept = default;

        NO_COPY(noir_vm)
        USE_DEFAULT_MOVE(noir_vm)

        // ── Execution ────────────────────────────────────────────────────────

        /**
         * @brief Start a script in a new session at the back of the run list.
         *
         * The session runs on the next update(), which may be in this same
         * frame if the VM has not run yet.
         *
         * One session per script: if a live session is already running this
         * function, the VM drops the request.
         *
         * @param func_id Function index in the TOC (0-based)
         * @return Nothing on success, or why the request was turned down
         */
        mtl::expected<void, noir_error> execute(u16 func_id) noexcept;

        /**
         * @brief Run a script to its end here and now, and hand back its D0.
         *
         * For the case where the VM is stopped and something must happen
         * anyway - a conversation choice that changes the music. It ignores the
         * run list, the duplicate check, the session budget and the frame
         * budget, and runs in the one reserved immediate context.
         *
         * Only engine code may call it, and only from outside an update: a
         * bridge function reaching it from inside an opcode is re-entry, and
         * there is nothing here to save a second context with.
         *
         * @param func_id Function index in the TOC (0-based)
         * @return What the script left in D0, or why it failed
         */
        mtl::expected<i16, noir_error> execute_now(u16 func_id) noexcept;

        /**
         * @brief Run one update tick.
         *
         * Walks the run list in passes, giving each session that can run a
         * slice of instructions, until the frame budget is spent or nothing can
         * run. Sessions that ended are removed at the end.
         *
         * Call this once per frame. Keep calling while the result is `yielded`.
         *
         * Does nothing once a terminal call has run: the scene it was running
         * in is over, so resuming a session in it would be wrong. Only
         * reset_execution() lifts that.
         *
         * @return `yielded` while sessions remain, `terminated` while a
         *         teardown is owed, `completed` when the run list emptied, `ok`
         *         when there was nothing to run — or why a session died, which
         *         outranks all of them.
         */
        mtl::expected<noir_result, noir_error> update() noexcept;

        /**
         * @brief Abandon every session and empty the run list.
         *
         * Clears the run list and all per-run execution state - registers, SR,
         * stack pointer, frame pointer, program counter, call depth, the wait
         * counter and the result. It keeps the stack allocation, the configured
         * budgets and the borrowed memory: FLAGS, VALUES, BYTECODE, TOC and the
         * bridge. Game state survives.
         *
         * Call this when control leaves the running scripts - a scene change,
         * say - and their call chains should end rather than resume.
         *
         * This is the teardown a terminal call asks for, and the only thing
         * that lets the VM run again after one.
         */
        void reset_execution() noexcept;

        // ── Accessors ────────────────────────────────────────────────────────

        noir_session_state state() const noexcept { return _current_session.state; }
        u16 exit_code() const noexcept { return _exit_code; }
        u16 reg_d(u8 i) const noexcept { return _current_session.d[i & 3]; }
        u8 sr() const noexcept { return _current_session.sr; }

        /// True while a terminal call is waiting for its teardown.
        bool terminated() const noexcept { return _vm_state == noir_vm_state::terminated; }

        /// Sessions in the run list, live or waiting to be removed.
        u16 session_count() const noexcept { return to<u16>(_sessions.size()); }

        /**
         * @brief Set the most instructions one session may run in one frame.
         * @param count Instructions per session per frame (default: 10)
         */
        void set_session_budget(u16 count) noexcept { _session_budget = count; }

        /**
         * @brief Set the most all sessions together may run in one frame.
         * @param count Instructions per frame (default: 40)
         */
        void set_frame_budget(u16 count) noexcept { _frame_budget = count; }

        /**
         * @brief Set the most instructions one execute_now script may run.
         * @param count Instructions per immediate run (default: 2048)
         */
        void set_immediate_cap(u16 count) noexcept { _immediate_cap = count; }

#ifdef GAME_DEBUG
        noir_debug_stats const& debug_stats() const noexcept { return _debug_stats; }
        void reset_debug_stats() noexcept { _debug_stats.reset(); }
#endif

        private:  //////////////////////////////////////////////////////////////////////////////////
        /**
         * @brief What the VM is running, if anything.
         *
         * One session sits in _current_session at a time and the VM cannot save
         * a second, so execute_now needs this idle. That covers both of its
         * rules: no call from inside an update, and no call from inside
         * another.
         */
        enum class run_mode : u8
        {
            idle,
            update,     ///< update() is walking the run list
            immediate,  ///< execute_now() is running a script
        };

        // ── Instruction dispatch ─────────────────────────────────────────────
        // An opcode is the byte; an instruction is one step. This counts the
        // step, whatever the step turns out to do.

        noir_result execute_instruction() noexcept;

        // ── Opcode category handlers ─────────────────────────────────────────

        noir_result op_script_exec(u8 op) noexcept;
        noir_result op_data_move(u8 op) noexcept;
        noir_result op_arithmetic(u8 op) noexcept;
        noir_result op_logic(u8 op) noexcept;
        noir_result op_control_flow(u8 op) noexcept;
        noir_result op_flag_ops(u8 op) noexcept;
        noir_result op_value_ops(u8 op) noexcept;
        noir_result op_game_inventory(u8 op) noexcept;
        noir_result op_game_timeline(u8 op) noexcept;
        noir_result op_game_scenes(u8 op) noexcept;
        noir_result op_game_dialogue(u8 op) noexcept;
        noir_result op_game_menu(u8 op) noexcept;
        noir_result op_debug(u8 op) noexcept;

        // ── Bridge calls ─────────────────────────────────────────────────────
        // One overload per kind, so the kind declared in noir_bridge decides
        // what the opcode returns and no call site can disagree with it.
        // call_result and call_flag have no deferred overload: a call that does
        // not act cannot answer.
        //
        // Inlined on purpose: otherwise the compiler gives each instantiation a
        // body of its own, which adds a second call in front of every bridge
        // call and costs more than it saves.

        template<typename Sig, typename... Args>
        [[gnu::always_inline]] noir_result call(immediate<Sig> const& b, Args... args) noexcept
        {
            b.fn(args...);
            return noir_result::ok;
        }

        template<typename Sig, typename... Args>
        [[gnu::always_inline]] noir_result call(deferred_modal<Sig> const& b, Args... args) noexcept
        {
            // Checked before the call, so nothing records the request. No frame
            // draws until execute_now returns, so the box would never appear
            // and the player could never dismiss it.
            if (_run_mode == run_mode::immediate)
                return fail(noir_error::immediate_modal, "a modal call under execute_now");

            b.fn(args...);
            return noir_result::deferred;
        }

        template<typename Sig, typename... Args>
        [[gnu::always_inline]] noir_result call(deferred_terminal<Sig> const& b, Args... args) noexcept
        {
            // Set before the call, not after: a bridge function that tears the
            // VM down on the spot has to be able to clear it again.
            _vm_state = noir_vm_state::terminated;
            b.fn(args...);
            return noir_result::deferred;
        }

        /// Immediate call whose answer lands in D0.
        template<typename Sig, typename... Args>
        [[gnu::always_inline]] noir_result call_result(immediate<Sig> const& b, Args... args) noexcept
        {
            _current_session.d[0] = b.fn(args...);
            return noir_result::ok;
        }

        /// Immediate call whose answer lands in Z, for the branch that follows.
        template<typename Sig, typename... Args>
        [[gnu::always_inline]] noir_result call_flag(immediate<Sig> const& b, Args... args) noexcept
        {
            set_flag_result(b.fn(args...));
            return noir_result::ok;
        }

        /// Z=0 when true, Z=1 when false. Defined where the SR bits are visible.
        void set_flag_result(bool value) noexcept;

        /**
         * @brief Kills the running session and says why. Every death goes here.
         *
         * Records the reason, writes the message where the player will read it,
         * and logs it. Turning a script down without killing anything is not a
         * failure and does not belong here: execute() hands its refusals back
         * to the caller instead.
         */
        noir_result fail(noir_error reason, char const* fmt, ...) noexcept
            __attribute__((format(printf, 3, 4)));

        // ── Stack operations ─────────────────────────────────────────────────

        void push16(u16 value) noexcept;
        void push32(u32 value) noexcept;
        u16 pop16() noexcept;
        u32 pop32() noexcept;
        u16 peek16() noexcept;

        /**
         * @brief True if pushing this many bytes would overrun the session's stack.
         * Kills the session when it would. Records a new peak when it would not.
         */
        bool stack_overflow(u16 bytes) noexcept;

        /**
         * @brief True if popping this many bytes would underrun the session's stack.
         * Kills the session when it would.
         */
        bool stack_underflow(u16 bytes) noexcept;

        u8* stack_at(u16 offset) noexcept { return _stack_base + offset; }
        u8 const* stack_at(u16 offset) const noexcept { return _stack_base + offset; }

        /**
         * @brief Where a LOCAL offset points, or null if that is outside the stack.
         *
         * The offset is signed and the frame pointer can be anywhere in the
         * stack, so the pair can name an address outside it. The stacks lie end
         * to end, so that address would read or write the next session's stack
         * and nothing would notice. Kills the session instead.
         */
        u8* local_at(i8 offset) noexcept;

        u8* stack_base_for(u16 index) noexcept
        {
            return _stack.data() + to<size_t>(index) * _stack_bytes;
        }

        // ── Bytecode reading ─────────────────────────────────────────────────

        u8 read_u8() noexcept;
        u16 read_u16() noexcept;
        i16 read_s16() noexcept;

        // ── Mode byte helpers ────────────────────────────────────────────────

        u16 read_operand(u8 mode) noexcept;
        void write_operand(u8 mode, u16 value) noexcept;
        u16 read_operand_cached(u8 mode) noexcept;
        void write_operand_cached(u16 value) noexcept;

        // ── SR flag helpers ──────────────────────────────────────────────────

        void set_flags_nz(u16 result) noexcept;
        void set_flags_nzcv_add(u16 a, u16 b, u32 result) noexcept;
        void set_flags_nzcv_sub(u16 a, u16 b, u32 result) noexcept;

        // ── RET implementation (shared by RET, RETZ, RETC) ───────────────────

        void do_ret() noexcept;

        // ── Session lifecycle ────────────────────────────────────────────────

        void reset_session() noexcept;

        /**
         * @brief Loads a session for @p func_id on stack @p slot and opens its
         *        first frame.
         *
         * Leaves it in the register window, dead if the frame did not fit. Both
         * ways into a script come through here, so one place builds the frame
         * do_ret() unwinds.
         */
        void enter_script(u16 func_id, u16 slot) noexcept;

        /// True while a session can still be given instructions.
        static bool session_live(noir_session const& s) noexcept
        {
            return s.state == noir_session_state::running
                || s.state == noir_session_state::waiting;
        }

        /// Lowest stack slot no live session holds.
        u16 alloc_stack_slot() const noexcept;

        /// Makes room for @p slots stacks. False if the allocator refused.
        bool ensure_stack_slots(u16 slots) noexcept;

        /// Loads run list entry @p index into the register window.
        void load_session(size_t index) noexcept;

        /// Writes the register window back to run list entry @p index.
        void store_session(size_t index) noexcept;

        /// Hands every session its budget for this update and ages the waits.
        void begin_update_budgets() noexcept;

#ifdef GAME_DEBUG
        /// What one walk did, for report_saturation().
        struct walk_tally
        {
            u16 live{0};       ///< Sessions running or waiting out a WAIT
            u16 ready{0};      ///< Of those, the ones the walk could run
            u16 ran_fully{0};  ///< Ready sessions given everything they asked for
            u16 cut_short{0};  ///< Ready sessions given less, the frame having run out

            /// The walk ended on an empty frame budget rather than a deferred call.
            bool frame_ran_out{false};
        };

        /**
         * @brief Logs the frame budget running out, and running out no longer.
         *
         * One line when it starts, one every 50 updates it lasts, one when it
         * ends. A budget that runs out every frame starves whichever sessions
         * the walk reaches last, so it needs raising.
         */
        void report_saturation(walk_tally const& tally) noexcept;
#endif

        /**
         * @brief Runs the loaded session until its slice or its state runs out.
         * @return `deferred` when a deferred call ended it, `failed` when the
         *         session died, otherwise `ok`.
         */
        noir_result run_slice() noexcept;

        /// Drops ended sessions, keeping the order of the rest.
        void remove_dead_sessions() noexcept;

        // ── Flag/Value helpers ───────────────────────────────────────────────

        bool get_flag(u16 index) const noexcept;
        void set_flag(u16 index, bool value) noexcept;
        bool validate_flag_index(u16 index) const noexcept;
        bool validate_value_index(u16 index) const noexcept;

        private:  //////////////////////////////////////////////////////////////////////////////////
        // ── Borrowed memory (caller manages lifetime) ────────────────────────

        u8* _flags;
        u16 _flags_size;
        u16* _values;
        u16 _values_count;
        u8 const* _bytecode;
        u32 _bytecode_size;
        u32 const* _func_toc;
        u16 _func_count;
        noir_bridge const* _bridge;

        // ── Owned memory ─────────────────────────────────────────────────────

        mtl::vector<u8, mtl::MemF::Fast> _stack;
        u8* _stack_base{nullptr};  ///< Base of the running session's stack
        u16 _stack_bytes;          ///< Size of one session's stack
        u16 _stack_slots;          ///< Stacks the allocation currently holds

        // ── The run list ─────────────────────────────────────────────────────

        mtl::vector<noir_session, mtl::MemF::Fast> _sessions;

        // ── The running session ──────────────────────────────────────────────

        noir_session _current_session{};

        // ── Execution state ──────────────────────────────────────────────────

        /// Instructions one session may run in one frame.
        u16 _session_budget{NOIR_SESSION_BUDGET};

        /// Instructions all sessions together may run in one frame.
        u16 _frame_budget{NOIR_FRAME_BUDGET};

        /// Instructions one execute_now run may spend.
        u16 _immediate_cap{NOIR_IMMEDIATE_CAP};

        u16 _rotation{0};  ///< Run list position the next update starts at

        run_mode _run_mode{run_mode::idle};              ///< What the VM is running
        noir_vm_state _vm_state{noir_vm_state::ok};      ///< How this update ended, or the latch

        // ── Result ───────────────────────────────────────────────────────────

        u16 _exit_code{0};

        /// Why the last session died. update(), execute() and execute_now()
        /// report it.
        noir_error _last_error{noir_error::none};

        // ── Cached destination for read-modify-write opcodes ─────────────────

        u8 _cached_dst_mode{0};    ///< Mode of last read_operand_cached() call
        u16 _cached_dst_param{0};  ///< Inline param (offset or index) from that call

        // ── Debug stats ──────────────────────────────────────────────────────

#ifdef GAME_DEBUG
        noir_debug_stats _debug_stats{};
#endif
    };

    using noir_vm_ptr = mtl::unique_ptr<noir_vm>;

}  // namespace NEONengine

#endif  // __NOIR_VM_H__INCLUDED__
