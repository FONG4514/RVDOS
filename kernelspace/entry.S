.section .text.entry
.global _entry
_entry:
        # 进入条件：
        #   主核由 OpenSBI 引导进入，a0 = hartid, a1 = dtb
        #   次核由 SBI HSM (sbi_hart_start) 唤醒进入，a0 = hartid, a1 = opaque
        # 两者均以 S 模式、关分页进入

        # 保存 hartid 到 tp 寄存器 (thread pointer)
        mv tp, a0

        # 每核独立栈: sp = stack0 + 4096 * (hartid + 1)
        la sp, stack0
        li t0, 4096
        addi t1, tp, 1
        mul t0, t0, t1
        add sp, sp, t0

        # 进入内核主函数
        call main

spin:
        j spin
