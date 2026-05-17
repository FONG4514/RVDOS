#include <arch/riscv.h>

.globl kernel_vector
.globl user_vector
.globl user_ret
.globl timervec
.globl fast_user_vector
.globl swtch

.section .text

# --- Machine Mode Timer Trap Vector ---
.align 4
timervec:
    # machine_trap_handler expects sscratch to point to a scratch area
    # scratch[0..2] : space to save a0, a1, a2
    csrrw a0, mscratch, a0
    sd a1, 0(a0)
    sd a2, 8(a0)
    sd a3, 16(a0)

    # 1. Clear SSIP logic would go here if we weren't just delegating
    # But we want to set SSIP (bit 1 of mip)
    li a1, 2
    csrs mip, a1

    # 2. Schedule next interrupt
    # scratch[3] has CLINT_MTIMECMP(hartid)
    # scratch[4] has interval
    ld a1, 24(a0) # CLINT_MTIMECMP
    ld a2, 32(a0) # interval
    ld a3, 0(a1)  # current MTIMECMP
    add a3, a3, a2
    sd a3, 0(a1)

    # Restore registers
    ld a1, 0(a0)
    ld a2, 8(a0)
    ld a3, 16(a0)
    csrrw a0, mscratch, a0
    
    mret

# --- 内核 Trap 向量 ---
.align 4
kernel_vector:
    # 内核 Trap 通常直接借用当前内核栈
    addi sp, sp, -256
    sd ra, 0(sp)
    sd sp, 8(sp)
    sd gp, 16(sp)
    sd tp, 24(sp)
    sd t0, 32(sp)
    sd t1, 40(sp)
    sd t2, 48(sp)
    sd s0, 56(sp)
    sd s1, 64(sp)
    sd a0, 72(sp)
    sd a1, 80(sp)
    sd a2, 88(sp)
    sd a3, 96(sp)
    sd a4, 104(sp)
    sd a5, 112(sp)
    sd a6, 120(sp)
    sd a7, 128(sp)
    sd s2, 136(sp)
    sd s3, 144(sp)
    sd s4, 152(sp)
    sd s5, 160(sp)
    sd s6, 168(sp)
    sd s7, 176(sp)
    sd s8, 184(sp)
    sd s9, 192(sp)
    sd s10, 200(sp)
    sd s11, 208(sp)
    sd t3, 216(sp)
    sd t4, 224(sp)
    sd t5, 232(sp)
    sd t6, 240(sp)

    call kernel_trap_handler

    # 恢复时跳过 sp (8(sp))，因为最后会 addi 恢复
    ld ra, 0(sp)
    ld gp, 16(sp)
    ld tp, 24(sp)
    ld t0, 32(sp)
    ld t1, 40(sp)
    ld t2, 48(sp)
    ld s0, 56(sp)
    ld s1, 64(sp)
    ld a0, 72(sp)
    ld a1, 80(sp)
    ld a2, 88(sp)
    ld a3, 96(sp)
    ld a4, 104(sp)
    ld a5, 112(sp)
    ld a6, 120(sp)
    ld a7, 128(sp)
    ld s2, 136(sp)
    ld s3, 144(sp)
    ld s4, 152(sp)
    ld s5, 160(sp)
    ld s6, 168(sp)
    ld s7, 176(sp)
    ld s8, 184(sp)
    ld s9, 192(sp)
    ld s10, 200(sp)
    ld s11, 208(sp)
    ld t3, 216(sp)
    ld t4, 224(sp)
    ld t5, 232(sp)
    ld t6, 240(sp)
    addi sp, sp, 256
    sret

# --- Context Switch ---
# void swtch(struct context *old, struct context *new);
# Save current registers in old, load from new.
.globl swtch
.align 4
swtch:
    sd ra, 0(a0)
    sd sp, 8(a0)
    sd s0, 16(a0)
    sd s1, 24(a0)
    sd s2, 32(a0)
    sd s3, 40(a0)
    sd s4, 48(a0)
    sd s5, 56(a0)
    sd s6, 64(a0)
    sd s7, 72(a0)
    sd s8, 80(a0)
    sd s9, 88(a0)
    sd s10, 96(a0)
    sd s11, 104(a0)

    ld ra, 0(a1)
    ld sp, 8(a1)
    ld s0, 16(a1)
    ld s1, 24(a1)
    ld s2, 32(a1)
    ld s3, 40(a1)
    ld s4, 48(a1)
    ld s5, 56(a1)
    ld s6, 64(a1)
    ld s7, 72(a1)
    ld s8, 80(a1)
    ld s9, 88(a1)
    ld s10, 96(a1)
    ld s11, 104(a1)

    ret

# --- 用户 Trap 向量 ---
.align 4
user_vector:
    # 1. 暂存用户 a0 到 sscratch，腾出 a0 寄存器
    csrw sscratch, a0
    
    # 2. 将 TRAPFRAME 的虚拟地址载入 a0
    li a0, TRAPFRAME

    # 3. 保存除 a0 外的其他寄存器
    sd ra, 40(a0)
    sd sp, 48(a0)
    sd gp, 56(a0)
    sd tp, 64(a0)
    sd t0, 72(a0)
    sd t1, 80(a0)
    sd t2, 88(a0)
    sd s0, 96(a0)
    sd s1, 104(a0)
    sd a1, 120(a0)
    sd a2, 128(a0)
    sd a3, 136(a0)
    sd a4, 144(a0)
    sd a5, 152(a0)
    sd a6, 160(a0)
    sd a7, 168(a0)
    sd s2, 176(a0)
    sd s3, 184(a0)
    sd s4, 192(a0)
    sd s5, 200(a0)
    sd s6, 208(a0)
    sd s7, 216(a0)
    sd s8, 224(a0)
    sd s9, 232(a0)
    sd s10, 240(a0)
    sd s11, 248(a0)
    sd t3, 256(a0)
    sd t4, 264(a0)
    sd t5, 272(a0)
    sd t6, 280(a0)

    # 4. 从 sscratch 读回原始 a0，并存入 Trapframe
    csrr t0, sscratch
    sd t0, 112(a0)

    # 保存触发 Trap 时的 EPC
    csrr t1, sepc
    sd t1, 24(a0)

    # 5. 载入内核环境
    ld tp, 32(a0)      # hartid
    ld sp, 8(a0)       # 内核栈指针
    ld t0, 16(a0)      # usertrap() C 函数地址

    # 6. 跳转到内核处理函数
    jr t0

# --- 从内核返回用户态 ---
.globl user_ret
.align 4
user_ret:
    # a0: Trapframe 指针
    
    # 1. 恢复 EPC
    ld t1, 24(a0)
    csrw sepc, t1

    # 2. 恢复绝大部分寄存器
    ld ra, 40(a0)
    ld sp, 48(a0)
    ld gp, 56(a0)
    ld tp, 64(a0)
    ld t0, 72(a0)
    ld t1, 80(a0)
    ld t2, 88(a0)
    ld s0, 96(a0)
    ld s1, 104(a0)
    ld a1, 120(a0)
    ld a2, 128(a0)
    ld a3, 136(a0)
    ld a4, 144(a0)
    ld a5, 152(a0)
    ld a6, 160(a0)
    ld a7, 168(a0)
    ld s2, 176(a0)
    ld s3, 184(a0)
    ld s4, 192(a0)
    ld s5, 200(a0)
    ld s6, 208(a0)
    ld s7, 216(a0)
    ld s8, 224(a0)
    ld s9, 232(a0)
    ld s10, 240(a0)
    ld s11, 248(a0)
    ld t3, 256(a0)
    ld t4, 264(a0)
    ld t5, 272(a0)
    ld t6, 280(a0)

    # 3. 最后恢复 a0
    ld a0, 112(a0)
    
    # 4. 返回用户态
    sret
