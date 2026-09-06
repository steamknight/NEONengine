#ifndef __MODAL_WAIT_H__INCLUDED__
#define __MODAL_WAIT_H__INCLUDED__

#include "core/noir_vm.h"

namespace NEONengine
{
    class modal_wait
    {
        public:
        modal_wait() = default;
        explicit modal_wait(noir_vm* vm) noexcept
            : _vm(vm), _id(vm ? vm->modal_wait_id() : 0) {}

        void complete() noexcept
        {
            if (_vm) _vm->complete_modal_wait(_id);
            _vm = nullptr;
        }
        bool owns(noir_vm& vm) const noexcept
        {
            return _vm == &vm && _id == vm.modal_wait_id();
        }

        private:
        noir_vm* _vm{ nullptr };
        u32 _id{ 0 };
    };
}

#endif
