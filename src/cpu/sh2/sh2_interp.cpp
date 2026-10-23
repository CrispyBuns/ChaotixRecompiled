// Reference SH-2 interpreter (validation + fallback for code not covered by
// the static recompiler).
#include "sh2_interp.h"
#include "sh2_ops.h"

namespace sh2 {

void reset(State* c, uint32_t pc, uint32_t sp, uint32_t vbr) {
    Bus bus = c->bus;
    auto ack = c->irq_ack;
    auto user = c->irq_user;
    auto hook = c->hook;
    auto hook_user = c->hook_user;
    auto hook_pcs = c->hook_pcs;
    auto hook_count = c->hook_count;
    uint8_t id = c->id;
    *c = State{};
    c->bus = bus;
    c->irq_ack = ack;
    c->irq_user = user;
    c->hook = hook;
    c->hook_user = hook_user;
    c->hook_pcs = hook_pcs;
    c->hook_count = hook_count;
    c->id = id;
    c->pc = pc;
    c->r[15] = sp;
    c->vbr = vbr;
    c->sr = SR_MASK & 0xF0;  // interrupts masked
}

void exec_branch(State* c, const Insn& in) {
    uint32_t target = branch_target(c, in);
    c->cycles += branch_cycle_adjust(in, target);
    if (in.flags & IF_DELAYED) {
        uint32_t spc = in.pc + 2;
        Insn slot = decode(spc, uint16_t(rd16(c, spc)));
        c->cycles -= slot.cycles;
        exec_simple(c, slot);
    }
    c->pc = target;
}

// The loop is instantiated twice so that, without a hook installed, it is
// exactly the loop it was: the test is made once a block, not once an
// instruction.
template <bool kHooked>
static void run_block(State* c) {
    for (;;) {
        uint32_t pc = c->pc;
        if (kHooked && is_hook_pc(c, pc)) c->hook(c, pc);
        Insn in = decode(pc, uint16_t(rd16(c, pc)));
        c->cycles -= in.cycles;
        if (in.flags & IF_BRANCH) {
            exec_branch(c, in);
            return;
        }
        c->pc = pc + 2;
        exec_simple(c, in);
        if (ends_block(in)) return;
    }
}

void interp_block(State* c) {
    if (c->hook) run_block<true>(c);
    else run_block<false>(c);
}

} // namespace sh2
