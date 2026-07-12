.syntax unified
.cpu cortex-m7
.thumb
.text
.align 2

  add.w r2, r3, #0x4000
  subs.w r0, r1, #0xff
  and.w r9, r9, #0xf0000000
  orr.w r4, r4, #0x00AB00AB
  bic.w r5, r5, #0xABABABAB
  cmp.w r7, #0
  tst.w r8, #0x20
  add.w r1, r2, r3, lsl #7
  subs.w r4, r5, r6, asr #31
  adc.w r8, r8, r9, ror #1
  rsb.w r0, r1, r2
  cmn.w r3, r4, lsl #2
  lsr.w r2, r12, #31
  lsls.w r3, r3, #1
  mvn.w r6, r7
  mov r10, r3
  mov r2, r11
  lsls.w r4, r5, r6
  ror.w r7, r8, r9
  movw r0, #0x1234
  movt r1, #0xABCD
  mul r3, r4, r5
  mla r6, r7, r8, r9
  mls r0, r1, r2, r3
  smull r2, r3, r4, r5
  umull r6, r7, r8, r9
  smlal r0, r1, r2, r3
  umlal r4, r5, r6, r7
  clz r0, r1
  usat r2, #4, r3, asr #24
  ldr.w r0, [r11, #68]
  str.w r9, [r11, #64]
  ldr.w r0, [r11, #0x8D0]
  ldr.w r1, [r2, #-12]
  str.w r3, [r4, #-8]
  ldr.w r2, [r11, r2, lsl #2]
  str.w r1, [r0, r2]
  mrs r9, apsr
  msr apsr_nzcvq, r9
  bx lr
  blx r2
  it ne
  movne r1, r2
  nop
  push.w {r2, r3, r12, lr}
  pop.w {r2, r3, r12, lr}
  cbnz r0, 99f
  nop
  nop
  nop
  nop
  nop
99:
b1:
  b.w b1 + 64
b2:
  bl b2 + 64
b3:
  beq.w b3 + 64
b4:
  blt.w b4 - 64
b5:
  b.w b5 - 1024
  addw r0, r1, #4095
  subw r2, r3, #0x8D0
  nop
