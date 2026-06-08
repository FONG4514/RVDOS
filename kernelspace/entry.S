# Derived from xv6-riscv (https://github.com/mit-pdos/xv6-riscv)
# Copyright (c) 2006-2023 Frans Kaashoek, Robert Morris, Russ Cox, 
#                         Massachusetts Institute of Technology
#
# Permission is hereby granted, free of charge, to any person obtaining
# a copy of this software and associated documentation files (the
# "Software"), to deal in the Software without restriction, including
# without limitation the rights to use, copy, modify, merge, publish,
# distribute, sublicense, and/or sell copies of the Software, and to
# permit persons to whom the Software is furnished to do so, subject to
# the following conditions:
#
# The above copyright notice and this permission notice shall be
# included in all copies or substantial portions of the Software.
#
# THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
# EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
# MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
# NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE
# LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION
# OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION
# WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.

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
