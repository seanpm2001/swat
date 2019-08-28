#include <glib.h>
#include "syscalls.h"
#include "plugins.h"
#include "regnum.h"
#include "syscall-functions.h"

#define DEBUG
#ifdef DEBUG
#define DPRINTF qemulib_log
#else
static inline void DPRINTF(const char *fmt, ...) {}
#endif

#define S(name, id) [id] = SYS_##name,

static const uint32_t win10x64_syscall_map[] = {
#include "syscall-table-win10x64.h"
};

static const uint32_t linux_x86_64_syscall_map[] = {
#include "syscall-table-linux-x86_64.h"
};

#undef S

#define GET_SYSCALL(map, id)                                           \
    (((id) < sizeof(map##_syscall_map) / sizeof(map##_syscall_map[0])) \
        ? map##_syscall_map[id]                                        \
        : SYS_Unknown)

bool is_syscall_x86_64(address_t pc, cpu_t cpu)
{
    uint8_t code = 0;
    if (!qemulib_read_memory(cpu, pc, &code, 1)) {
        if (code == 0x0f) {
            if (qemulib_read_memory(cpu, pc + 1, &code, 1)) {
                return false;
            }
            if (code == 0x05) {
                /* syscall */
                return true;
            }
        } else if (code == 0x48) {
            if (qemulib_read_memory(cpu, pc + 1, &code, 1)) {
                return false;
            }
            if (code != 0x0f) {
                return false;
            }
            if (qemulib_read_memory(cpu, pc + 2, &code, 1)) {
                return false;
            }
            if (code == 0x07) {
                /* sysret */
                return true;
            }
        }
    } 

    return false;
}

void syscall_x86_64(address_t pc, cpu_t cpu)
{
    context_t ctx = vmi_get_context(cpu);
    uint8_t code = 0;
    qemulib_read_memory(cpu, pc + 1, &code, 1);
    if (code == 0x05) {
        /* syscall */
        uint32_t id = vmi_get_register(cpu, AMD64_RAX_REGNUM);
        DPRINTF("%llx: syscall %x\n", ctx, id);
        void *params = NULL;
        if (os_type == OS_WIN10x64) {
            id = GET_SYSCALL(win10x64, id);
            if (id != SYS_Unknown) {
                params = syscall_enter_win64(id, pc, cpu);
            }
        } else if (os_type == OS_LINUX) {
            id = GET_SYSCALL(linux_x86_64, id);
            if (id != SYS_Unknown) {
                params = syscall_enter_linux64(id, pc, cpu);
            }
        }
        if (params) {
            sc_insert(ctx, vmi_get_stack_pointer(cpu), id, params);
        }
    } else {
        /* sysret */
        uint64_t sp = vmi_get_stack_pointer(cpu);
        SCData *sc = sc_find(ctx, sp);
        if (sc) {
            //DPRINTF("%llx: sysret %x\n", vmi_get_context(cpu), sc->num);
            if (os_type == OS_WIN10x64) {
                syscall_exit_win64(sc, pc, cpu);
            } else if (os_type == OS_LINUX) {
                syscall_exit_linux64(sc, pc, cpu);
            }
            sc_erase(ctx, sp);
        }
    }
}
