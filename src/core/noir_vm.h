#ifndef __NOIR_VM_H__INCLUDED__
#define __NOIR_VM_H__INCLUDED__

#include "types.h"

#include <stdint.h>

#include <ace/types.h>

#include "mtl/memory.h"
#include "mtl/utility.h"
#include "mtl/vector.h"

namespace NEONengine
{
    // ─── Status codes ───────────────────────────────────────────────────────────

    /**
     * @brief Status codes returned by update() and execute().
     *
     * Non-error statuses (Ok, Yielded, Completed) indicate normal operation.
     * Error statuses cause the VM to halt and stop executing.
     */
    enum class noir_status : u8
    {
        ok,         ///< Execution proceeded normally
        yielded,    ///< Yielded — call update() again next frame
        completed,  ///< Entry function returned, check exit_code()

        // Errors (VM halts)
        err_invalid_opcode,   ///< Unknown opcode byte encountered
        err_invalid_func,     ///< Function ID out of range
        err_duplicate_script, ///< A live session is already running that script
        err_stack_underflow,  ///< Popped more than pushed
        err_stack_overflow,   ///< Pushed past the end of the session's stack
        err_division_zero,    ///< Division or modulo by zero
        err_invalid_address,  ///< PC or address out of bytecode bounds
        err_value_oob,        ///< VALUE index out of range
        err_flag_oob,         ///< FLAG index out of range
    };

    // ─── Debug statistics ───────────────────────────────────────────────────────

#ifdef GAME_DEBUG
    /**
     * @brief Runtime statistics for tuning.
     * Only available in debug builds.
     */
    struct noir_debug_stats
    {
        u32 max_stack_used;         ///< High-water mark of stack usage (bytes)
        u16 max_call_depth;         ///< Deepest CALL nesting reached
        u32 opcode_histogram[256];  ///< Execution count per opcode

        void reset() noexcept;
    };
#endif

    // ─── Engine bridge ──────────────────────────────────────────────────────────

    /**
     * @brief Function pointer table bridging the VM to engine systems.
     *
     * The VM calls engine systems through this struct, keeping the VM fully
     * decoupled from engine headers. All function pointers must be valid for
     * the lifetime of the VM.
     *
     * A blocking bridge call, marked below, leaves the VM blocked. The engine
     * must call noir_vm::decrement_blocking() when the operation completes, or
     * the script never resumes.
     *
     * @see noir_vm
     */
    struct noir_bridge
    {
        // Inventory
        void (*inv_add)(u16 item_id);
        void (*inv_remove)(u16 item_id);
        bool (*inv_has)(u16 item_id);  ///< true = has item
        u16 (*inv_count)();
        void (*inv_clear)();

        // Scenes
        void (*goto_scene)(u16 scene_id);  ///< blocking
        u16 (*get_scene)();
        void (*hotspot_on)(u16 id);
        void (*hotspot_off)(u16 id);
        void (*set_pointer)(u16 type);

        // Dialogue / Text
        void (*say)(u16 string_id, u16 speaker_id);  ///< blocking
        void (*show_dialog)(u16 dialogue_id);             ///< blocking
        void (*show_text)(u16 string_id, u16 x, u16 y);
        void (*clear_text)();

        // Audio
        void (*play_music)(u16 music_id);
        void (*stop_music)();
        void (*play_sfx)(u16 sfx_id);

        // Screen
        void (*fade_out)(u16 duration_frames);  ///< blocking
        void (*fade_in)(u16 duration_frames);   ///< blocking
        void (*screen_clear)(u16 color_index);

        // Animation
        void (*anim_play)(u16 anim_id);
        void (*anim_stop)(u16 anim_id);
        void (*anim_wait)(u16 anim_id);  ///< blocking

        // System
        void (*set_lang)(u16 lang_id);
        void (*save_game)(u16 slot);
        void (*load_game)(u16 slot);
        void (*new_game)();
        void (*quit_game)();

#ifdef GAME_DEBUG
        void (*debug_log)(u16 string_id);              ///< Log string table entry
        void (*debug_print)(char const* str, u8 len);  ///< Log raw string
#endif
    };

    // ─── VM states ──────────────────────────────────────────────────────────────

    enum class noir_state : u8
    {
        idle,     ///< No function running
        running,  ///< Actively executing opcodes
        yielded,  ///< Waiting for next update()
        waiting,  ///< WAIT timer active (wait_counter > 0)
        blocked,  ///< Waiting for engine ops (blocking_counter > 0)
        halted,   ///< Finished or errored
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
        noir_state state;
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
     * instructions until the frame's pool is spent or nothing can run.
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
            u16 stack_size) noexcept;

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
         * function the request is dropped.
         *
         * @param func_id Function index in the TOC (0-based)
         * @return noir_status::ok on success, or an error status
         */
        noir_status execute(u16 func_id) noexcept;

        /**
         * @brief Run one update tick.
         *
         * Walks the run list in passes, giving each session that can run a
         * slice of instructions, until the frame pool is spent or nothing can
         * run. Sessions that ended are removed at the end.
         *
         * Call this once per frame. Keep calling while status is `yielded`.
         *
         * @return `yielded` while sessions remain, otherwise how the last one
         *         ended; `ok` when there was nothing to run.
         */
        noir_status update() noexcept;

        /**
         * @brief Abandon every session and return to a clean idle state.
         *
         * Clears the run list and all per-run execution state - registers, SR,
         * stack pointer, frame pointer, program counter, call depth, the
         * wait/blocking counters and the result. Borrowed memory (FLAGS,
         * VALUES, BYTECODE, TOC, bridge), the stack allocation and the
         * configured budgets are all preserved.
         *
         * Used when control transfers away from the running scripts - a scene
         * change, say - and the current call chains must be discarded rather
         * than resumed. Game state in FLAGS/VALUES is untouched.
         */
        void reset_execution() noexcept;

        // ── Blocking counter (for engine use) ────────────────────────────────

        /**
         * @brief Blocks the VM until decrement_blocking() balances this.
         */
        void increment_blocking() noexcept
        {
            ++_blocking_counter;
            _cur.state = noir_state::blocked;
        }

        /**
         * @brief Resumes the VM on the next update() once the count hits zero.
         */
        void decrement_blocking() noexcept { --_blocking_counter; }

        // ── Accessors ────────────────────────────────────────────────────────

        noir_state state() const noexcept { return _cur.state; }
        noir_status last_error() const noexcept { return _last_error; }
        u16 exit_code() const noexcept { return _exit_code; }
        u16 reg_d(u8 i) const noexcept { return _cur.d[i & 3]; }
        u8 sr() const noexcept { return _cur.sr; }

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
        void set_frame_pool(u16 count) noexcept { _frame_pool = count; }

#ifdef GAME_DEBUG
        noir_debug_stats const& debug_stats() const noexcept { return _debug_stats; }
        void reset_debug_stats() noexcept { _debug_stats.reset(); }
#endif

        private:  //////////////////////////////////////////////////////////////////////////////////
        // ── Opcode dispatch ──────────────────────────────────────────────────

        noir_status execute_one_opcode() noexcept;

        // ── Opcode category handlers ─────────────────────────────────────────

        noir_status op_script_exec(u8 op) noexcept;
        noir_status op_data_move(u8 op) noexcept;
        noir_status op_arithmetic(u8 op) noexcept;
        noir_status op_logic(u8 op) noexcept;
        noir_status op_control_flow(u8 op) noexcept;
        noir_status op_flag_ops(u8 op) noexcept;
        noir_status op_value_ops(u8 op) noexcept;
        noir_status op_game_inventory(u8 op) noexcept;
        noir_status op_game_timeline(u8 op) noexcept;
        noir_status op_game_scenes(u8 op) noexcept;
        noir_status op_game_dialogue(u8 op) noexcept;
        noir_status op_game_menu(u8 op) noexcept;
        noir_status op_debug(u8 op) noexcept;

        // ── Stack operations ─────────────────────────────────────────────────

        void push16(u16 value) noexcept;
        void push32(u32 value) noexcept;
        u16 pop16() noexcept;
        u32 pop32() noexcept;
        u16 peek16() noexcept;

        /**
         * @brief True if pushing this many bytes would overrun the session's stack.
         * Kills the session with err_stack_overflow when it would. Records a new
         * high-water mark when it would not.
         */
        bool stack_overflow(u16 bytes) noexcept;

        /**
         * @brief True if popping this many bytes would underrun the session's stack.
         * Kills the session with err_stack_underflow when it would.
         */
        bool stack_underflow(u16 bytes) noexcept;

        u8* stack_at(u16 offset) noexcept { return _stack_base + offset; }
        u8 const* stack_at(u16 offset) const noexcept { return _stack_base + offset; }

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

        /// True while a session can still be given instructions.
        static bool session_live(noir_session const& s) noexcept
        {
            return s.state != noir_state::idle && s.state != noir_state::halted;
        }

        /// Lowest stack slot no live session holds.
        u16 alloc_stack_slot() const noexcept;

        /// Makes room for @p slots stacks. False if the memory was refused.
        bool ensure_stack_slots(u16 slots) noexcept;

        /// Loads run list entry @p index into the register window.
        void load_session(size_t index) noexcept;

        /// Writes the register window back to run list entry @p index.
        void store_session(size_t index) noexcept;

        /// Hands every session its budget for this update and ages the waits.
        void begin_update_budgets() noexcept;

        /// Runs the loaded session until its slice or its state runs out.
        void run_slice() noexcept;

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
        u8* _stack_base;   ///< Base of the running session's stack
        u16 _stack_bytes;  ///< Size of one session's stack
        u16 _stack_slots;  ///< Stacks the allocation currently holds

        // ── The run list ─────────────────────────────────────────────────────

        mtl::vector<noir_session, mtl::MemF::Fast> _sessions;

        // ── The running session ──────────────────────────────────────────────

        noir_session _cur;

        // ── Execution state ──────────────────────────────────────────────────

        u16 _call_depth;       ///< Current internal CALL nesting
        u16 _session_budget;   ///< Instructions per session per frame (default: 10)
        u16 _frame_pool;       ///< Instructions per frame, all sessions (default: 40)
        u16 _rotation;         ///< Run list position the next update starts at
        u8 _blocking_counter;  ///< >0 = waiting for engine ops
        bool _run_list_dropped;  ///< reset_execution() ran inside an update

        // ── Result ───────────────────────────────────────────────────────────

        u16 _exit_code;
        noir_status _last_error;

        // ── Cached destination for read-modify-write opcodes ─────────────────

        u8 _cached_dst_mode;    ///< Mode of last read_operand_cached() call
        u16 _cached_dst_param;  ///< Inline param (offset or index) from that call

        // ── Debug stats ──────────────────────────────────────────────────────

#ifdef GAME_DEBUG
        noir_debug_stats _debug_stats;
#endif
    };

    using noir_vm_ptr = mtl::unique_ptr<noir_vm>;

}  // namespace NEONengine

#endif  // __NOIR_VM_H__INCLUDED__
