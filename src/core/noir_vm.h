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
        err_stack_underflow,  ///< Popped more than pushed
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
        u8 max_reentry_level;       ///< Highest re-entrancy level reached
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

    // ─── noir_vm ────────────────────────────────────────────────────────────────

    /**
     * @brief NoirScript bytecode virtual machine.
     *
     * Register-based VM for driving game scripts — dialogue, inventory, scene
     * transitions, animations, and game flow. Designed for the Amiga 1200
     * (68020 CPU, big-endian, limited RAM).
     *
     * The VM borrows external memory (FLAGS, VALUES, BYTECODE, TOC, bridge)
     * and owns only its execution stack. All borrowed memory must outlive
     * the VM instance.
     *
     * @par Registers
     * - D0–D3: 16-bit general-purpose data registers
     * - SP/FP: 16-bit offsets from the stack base; the stack grows downward
     * - PC: 16-bit offset into the bytecode, at the next byte to read
     * - SR: 8-bit status register (NZCV----)
     *
     * @par Lifecycle
     * 1. Construct via noir_vm(...)
     * 2. Call execute(func_id) to start a script function
     * 3. Call update() each frame until completed or error
     *
     * @par Re-Entrancy
     * The engine may call execute() while the VM is already running (from a
     * bridge callback). All registers are saved/restored automatically.
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
         * @param stack_size      Initial stack capacity in bytes (e.g. 256)
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
         * @brief Start executing a script function by its TOC index.
         *
         * May be called while the VM is already running (re-entry). In that
         * case, all registers are saved to the stack and restored when the
         * function returns.
         *
         * @param func_id Function index in the TOC (0-based)
         * @return noir_status::ok on success, or an error status
         */
        noir_status execute(u16 func_id) noexcept;

        /**
         * @brief Run one update tick.
         *
         * Executes up to opcodes_per_update opcodes, handles wait/block
         * counters, and returns the resulting status.
         *
         * Call this once per frame. Keep calling while status is `yielded`.
         *
         * @return Current VM status after the update
         */
        noir_status update() noexcept;

        /**
         * @brief Abandon the current execution and return to a clean idle state.
         *
         * Clears all per-run execution state - registers, SR, stack pointer,
         * frame pointer, program counter, call depth, re-entry level, the
         * wait/blocking counters and the result - so the next execute() begins a
         * fresh top-level function. Borrowed memory (FLAGS, VALUES, BYTECODE,
         * TOC, bridge), the owned stack allocation and the configured
         * opcodes-per-update are all preserved.
         *
         * Used when control transfers away from the running script - a scene
         * change, say - and the current call chain must be discarded rather than
         * resumed. Game state in FLAGS/VALUES is untouched.
         */
        void reset_execution() noexcept;

        // ── Blocking counter (for engine use) ────────────────────────────────

        /**
         * @brief Blocks the VM until decrement_blocking() balances this.
         */
        void increment_blocking() noexcept
        {
            ++_blocking_counter;
            _state = noir_state::blocked;
        }

        /**
         * @brief Resumes the VM on the next update() once the count hits zero.
         */
        void decrement_blocking() noexcept { --_blocking_counter; }

        // ── Accessors ────────────────────────────────────────────────────────

        noir_state state() const noexcept { return _state; }
        noir_status last_error() const noexcept { return _last_error; }
        u16 exit_code() const noexcept { return _exit_code; }
        u16 reg_d(u8 i) const noexcept { return _d[i & 3]; }
        u8 sr() const noexcept { return _sr; }

        /**
         * @brief Set the number of opcodes executed per update() call.
         * @param count Opcodes per update (default: 10)
         */
        void set_opcodes_per_update(u16 count) noexcept { _opcodes_per_update = count; }

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
        u16 peek16() const noexcept;
        void ensure_stack(u16 bytes_needed) noexcept;

        u8* stack_at(u16 offset) noexcept { return _stack.data() + offset; }
        u8 const* stack_at(u16 offset) const noexcept { return _stack.data() + offset; }

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

        // ── Flag/Value helpers ───────────────────────────────────────────────

        bool get_flag(u16 index) const noexcept;
        void set_flag(u16 index, bool value) noexcept;
        bool validate_flag_index(u16 index) const noexcept;
        bool validate_value_index(u16 index) const noexcept;

        // ── Stack high-water ─────────────────────────────────────────────────

#ifdef GAME_DEBUG
        void update_stack_high_water() noexcept;
#endif

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

        // ── Registers ────────────────────────────────────────────────────────

        u16 _d[4];  ///< D0–D3
        u16 _sp;    ///< Stack offset from the base; the stack grows downward
        u16 _fp;    ///< Frame offset from the base
        u16 _pc;    ///< Bytecode offset of the next byte to read
        u8 _sr;     ///< Status register (NZCV----)

        // ── Execution state ──────────────────────────────────────────────────

        u8 _reentry_level;        ///< 0 = not running, 1+ = nested calls
        u16 _call_depth;          ///< Current internal CALL nesting
        u16 _opcodes_per_update;  ///< Default: 10
        u16 _opcodes_remaining;   ///< Decremented each opcode
        u8 _blocking_counter;     ///< >0 = waiting for engine ops
        u16 _wait_counter;        ///< >0 = waiting N frames

        // ── Result ───────────────────────────────────────────────────────────

        u16 _exit_code;
        noir_status _last_error;
        noir_state _state;

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
