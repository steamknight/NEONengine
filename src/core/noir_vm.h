#ifndef __NOIR_VM_H__INCLUDED__
#define __NOIR_VM_H__INCLUDED__

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
    enum class noir_status : uint8_t
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
        uint32_t max_stack_used;         ///< High-water mark of stack usage (bytes)
        uint16_t max_call_depth;         ///< Deepest CALL nesting reached
        uint8_t max_reentry_level;       ///< Highest re-entrancy level reached
        uint32_t opcode_histogram[256];  ///< Execution count per opcode

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
        void (*inv_add)(uint16_t item_id);
        void (*inv_remove)(uint16_t item_id);
        bool (*inv_has)(uint16_t item_id);  ///< true = has item
        uint16_t (*inv_count)();
        void (*inv_clear)();

        // Scenes
        void (*goto_scene)(uint16_t scene_id);  ///< blocking
        uint16_t (*get_scene)();
        void (*hotspot_on)(uint16_t id);
        void (*hotspot_off)(uint16_t id);
        void (*set_pointer)(uint16_t type);

        // Dialogue / Text
        void (*say)(uint16_t string_id, uint16_t speaker_id);  ///< blocking
        void (*show_dialog)(uint16_t dialogue_id);             ///< blocking
        void (*show_text)(uint16_t string_id, uint16_t x, uint16_t y);
        void (*clear_text)();

        // Audio
        void (*play_music)(uint16_t music_id);
        void (*stop_music)();
        void (*play_sfx)(uint16_t sfx_id);

        // Screen
        void (*fade_out)(uint16_t duration_frames);  ///< blocking
        void (*fade_in)(uint16_t duration_frames);   ///< blocking
        void (*screen_clear)(uint16_t color_index);

        // Animation
        void (*anim_play)(uint16_t anim_id);
        void (*anim_stop)(uint16_t anim_id);
        void (*anim_wait)(uint16_t anim_id);  ///< blocking

        // System
        void (*set_lang)(uint16_t lang_id);
        void (*save_game)(uint16_t slot);
        void (*load_game)(uint16_t slot);
        void (*new_game)();
        void (*quit_game)();

#ifdef GAME_DEBUG
        void (*debug_log)(uint16_t string_id);              ///< Log string table entry
        void (*debug_print)(char const* str, uint8_t len);  ///< Log raw string
#endif
    };

    // ─── VM states ──────────────────────────────────────────────────────────────

    enum class noir_state : uint8_t
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
     * - SP/FP: 32-bit stack and frame pointers (raw addresses, stack grows down)
     * - PC: 32-bit program counter (points to next byte to read)
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
         * @param func_toc        Borrowed array of uint32_t offsets into bytecode
         * @param func_count      Number of entries in the function TOC
         * @param bridge          Borrowed engine bridge (must outlive VM)
         * @param stack_size      Initial stack capacity in bytes (e.g. 256)
         */
        noir_vm(uint8_t* flags,
            uint16_t flags_size,
            uint16_t* values,
            uint16_t values_count,
            uint8_t const* bytecode,
            uint32_t bytecode_size,
            uint32_t const* func_toc,
            uint16_t func_count,
            noir_bridge const* bridge,
            uint16_t stack_size) noexcept;

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
        noir_status execute(uint16_t func_id) noexcept;

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
        uint16_t exit_code() const noexcept { return _exit_code; }
        uint16_t reg_d(uint8_t i) const noexcept { return _d[i & 3]; }
        uint8_t sr() const noexcept { return _sr; }

        /**
         * @brief Set the number of opcodes executed per update() call.
         * @param count Opcodes per update (default: 10)
         */
        void set_opcodes_per_update(uint16_t count) noexcept { _opcodes_per_update = count; }

#ifdef GAME_DEBUG
        noir_debug_stats const& debug_stats() const noexcept { return _debug_stats; }
        void reset_debug_stats() noexcept { _debug_stats.reset(); }
#endif

        private:  //////////////////////////////////////////////////////////////////////////////////
        // ── Opcode dispatch ──────────────────────────────────────────────────

        noir_status execute_one_opcode() noexcept;

        // ── Opcode category handlers ─────────────────────────────────────────

        noir_status op_script_exec(uint8_t op) noexcept;
        noir_status op_data_move(uint8_t op) noexcept;
        noir_status op_arithmetic(uint8_t op) noexcept;
        noir_status op_logic(uint8_t op) noexcept;
        noir_status op_control_flow(uint8_t op) noexcept;
        noir_status op_flag_ops(uint8_t op) noexcept;
        noir_status op_value_ops(uint8_t op) noexcept;
        noir_status op_game_inventory(uint8_t op) noexcept;
        noir_status op_game_timeline(uint8_t op) noexcept;
        noir_status op_game_scenes(uint8_t op) noexcept;
        noir_status op_game_dialogue(uint8_t op) noexcept;
        noir_status op_game_menu(uint8_t op) noexcept;
        noir_status op_debug(uint8_t op) noexcept;

        // ── Stack operations ─────────────────────────────────────────────────

        void push16(uint16_t value) noexcept;
        void push32(uint32_t value) noexcept;
        uint16_t pop16() noexcept;
        uint32_t pop32() noexcept;
        uint16_t peek16() const noexcept;
        void ensure_stack(uint16_t bytes_needed) noexcept;

        // ── Bytecode reading ─────────────────────────────────────────────────

        uint8_t read_u8() noexcept;
        uint16_t read_u16() noexcept;
        int16_t read_s16() noexcept;

        // ── Mode byte helpers ────────────────────────────────────────────────

        uint16_t read_operand(uint8_t mode) noexcept;
        void write_operand(uint8_t mode, uint16_t value) noexcept;
        uint16_t read_operand_cached(uint8_t mode) noexcept;
        void write_operand_cached(uint16_t value) noexcept;

        // ── SR flag helpers ──────────────────────────────────────────────────

        void set_flags_nz(uint16_t result) noexcept;
        void set_flags_nzcv_add(uint16_t a, uint16_t b, uint32_t result) noexcept;
        void set_flags_nzcv_sub(uint16_t a, uint16_t b, uint32_t result) noexcept;

        // ── RET implementation (shared by RET, RETZ, RETC) ───────────────────

        void do_ret() noexcept;

        // ── Flag/Value helpers ───────────────────────────────────────────────

        bool get_flag(uint16_t index) const noexcept;
        void set_flag(uint16_t index, bool value) noexcept;
        bool validate_flag_index(uint16_t index) const noexcept;
        bool validate_value_index(uint16_t index) const noexcept;

        // ── Stack high-water ─────────────────────────────────────────────────

#ifdef GAME_DEBUG
        void update_stack_high_water() noexcept;
#endif

        private:  //////////////////////////////////////////////////////////////////////////////////
        // ── Borrowed memory (caller manages lifetime) ────────────────────────

        uint8_t* _flags;
        uint16_t _flags_size;
        uint16_t* _values;
        uint16_t _values_count;
        uint8_t const* _bytecode;
        uint32_t _bytecode_size;
        uint32_t const* _func_toc;
        uint16_t _func_count;
        noir_bridge const* _bridge;

        // ── Owned memory ─────────────────────────────────────────────────────

        mtl::vector<uint8_t, mtl::MemF::Fast> _stack;

        // ── Registers ────────────────────────────────────────────────────────

        uint16_t _d[4];      ///< D0–D3
        uint8_t* _sp;        ///< Stack pointer (raw, grows downward)
        uint8_t* _fp;        ///< Frame pointer (raw)
        uint8_t const* _pc;  ///< Program counter (raw, next byte to read)
        uint8_t _sr;         ///< Status register (NZCV----)

        // ── Execution state ──────────────────────────────────────────────────

        uint8_t _reentry_level;        ///< 0 = not running, 1+ = nested calls
        uint16_t _call_depth;          ///< Current internal CALL nesting
        uint16_t _opcodes_per_update;  ///< Default: 10
        uint16_t _opcodes_remaining;   ///< Decremented each opcode
        uint8_t _blocking_counter;     ///< >0 = waiting for engine ops
        uint16_t _wait_counter;        ///< >0 = waiting N frames

        // ── Result ───────────────────────────────────────────────────────────

        uint16_t _exit_code;
        noir_status _last_error;
        noir_state _state;

        // ── Cached destination for read-modify-write opcodes ─────────────────

        uint8_t _cached_dst_mode;    ///< Mode of last read_operand_cached() call
        uint16_t _cached_dst_param;  ///< Inline param (offset or index) from that call

        // ── Debug stats ──────────────────────────────────────────────────────

#ifdef GAME_DEBUG
        noir_debug_stats _debug_stats;
#endif
    };

    using noir_vm_ptr = mtl::unique_ptr<noir_vm>;

}  // namespace NEONengine

#endif  // __NOIR_VM_H__INCLUDED__
