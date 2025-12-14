;
; Copyright (c) 2021, Alliance for Open Media. All rights reserved
;
; This source code is subject to the terms of the BSD 3-Clause Clear License and the
; Alliance for Open Media Patent License 1.0. If the BSD 3-Clause Clear License was
; not distributed with this source code in the LICENSE file, you can obtain it
; at aomedia.org/license/software-license/bsd-3-c-c/.  If the Alliance for Open Media Patent
; License 1.0 was not distributed with this source code in the PATENTS file, you
; can obtain it at aomedia.org/license/patent-license/.
;

;


%include "avm_ports/x86_abi_support.asm"

section .text
globalsym(avm_reset_mmx_state)
sym(avm_reset_mmx_state):
    emms
    ret


%if LIBAVM_YASM_WIN64
globalsym(avm_winx64_fldcw)
sym(avm_winx64_fldcw):
    sub   rsp, 8
    mov   [rsp], rcx ; win x64 specific
    fldcw [rsp]
    add   rsp, 8
    ret


globalsym(avm_winx64_fstcw)
sym(avm_winx64_fstcw):
    sub   rsp, 8
    fstcw [rsp]
    mov   rax, [rsp]
    add   rsp, 8
    ret
%endif
