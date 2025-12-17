.data
_prompt: .asciiz "Enter an integer:"
_ret: .asciiz "\n"
.globl main
.text
read:
  li $v0, 4
  la $a0, _prompt
  syscall
  li $v0, 5
  syscall
  jr $ra
write:
  li $v0, 1
  syscall
  li $v0, 4
  la $a0, _ret
  syscall
  move $v0, $0
  jr $ra
main:
  addi $sp, $sp, -488
  sw $fp, 0($sp)
  sw $ra, 4($sp)
  move $fp, $sp
  addi $t0, $fp, 8
  sw $t0, 20($fp)
  addi $t0, $fp, 24
  sw $t0, 64($fp)
  li $t0, 0
  sw $t0, 68($fp)
  lw $t0, 68($fp)
  sw $t0, 72($fp)
  addi $t0, $fp, 76
  sw $t0, 96($fp)
label1:
  li $t0, 0
  sw $t0, 100($fp)
  lw $t0, 72($fp)
  sw $t0, 104($fp)
  li $t0, 5
  sw $t0, 108($fp)
  lw $t0, 104($fp)
  lw $t1, 108($fp)
  blt $t0, $t1, label4
  j label5
label4:
  li $t0, 1
  sw $t0, 100($fp)
label5:
  lw $t0, 100($fp)
  li $t1, 0
  bne $t0, $t1, label2
  j label3
label2:
  lw $t0, 96($fp)
  sw $t0, 112($fp)
  lw $t0, 72($fp)
  sw $t0, 116($fp)
  lw $t0, 116($fp)
  li $t1, 4
  mul $t2, $t0, $t1
  sw $t2, 120($fp)
  lw $t0, 112($fp)
  lw $t1, 120($fp)
  add $t2, $t0, $t1
  sw $t2, 124($fp)
  jal read
  sw $v0, 128($fp)
  lw $t0, 124($fp)
  lw $t1, 128($fp)
  sw $t1, 0($t0)
  lw $t0, 72($fp)
  sw $t0, 132($fp)
  li $t0, 1
  sw $t0, 136($fp)
  lw $t0, 132($fp)
  lw $t1, 136($fp)
  add $t2, $t0, $t1
  sw $t2, 140($fp)
  lw $t0, 140($fp)
  sw $t0, 72($fp)
  j label1
label3:
  li $t0, 0
  sw $t0, 144($fp)
  lw $t0, 144($fp)
  sw $t0, 72($fp)
label6:
  li $t0, 0
  sw $t0, 148($fp)
  lw $t0, 72($fp)
  sw $t0, 152($fp)
  li $t0, 4
  sw $t0, 156($fp)
  lw $t0, 152($fp)
  lw $t1, 156($fp)
  blt $t0, $t1, label9
  j label10
label9:
  li $t0, 1
  sw $t0, 148($fp)
label10:
  lw $t0, 148($fp)
  li $t1, 0
  bne $t0, $t1, label7
  j label8
label7:
  lw $t0, 72($fp)
  sw $t0, 160($fp)
  li $t0, 1
  sw $t0, 164($fp)
  lw $t0, 160($fp)
  lw $t1, 164($fp)
  add $t2, $t0, $t1
  sw $t2, 168($fp)
  lw $t0, 168($fp)
  sw $t0, 172($fp)
label11:
  li $t0, 0
  sw $t0, 176($fp)
  lw $t0, 172($fp)
  sw $t0, 180($fp)
  li $t0, 5
  sw $t0, 184($fp)
  lw $t0, 180($fp)
  lw $t1, 184($fp)
  blt $t0, $t1, label14
  j label15
label14:
  li $t0, 1
  sw $t0, 176($fp)
label15:
  lw $t0, 176($fp)
  li $t1, 0
  bne $t0, $t1, label12
  j label13
label12:
  lw $t0, 96($fp)
  sw $t0, 188($fp)
  lw $t0, 72($fp)
  sw $t0, 192($fp)
  lw $t0, 192($fp)
  li $t1, 4
  mul $t2, $t0, $t1
  sw $t2, 196($fp)
  lw $t0, 188($fp)
  lw $t1, 196($fp)
  add $t2, $t0, $t1
  sw $t2, 200($fp)
  lw $t0, 200($fp)
  lw $t1, 0($t0)
  sw $t1, 204($fp)
  lw $t0, 96($fp)
  sw $t0, 208($fp)
  lw $t0, 172($fp)
  sw $t0, 212($fp)
  lw $t0, 212($fp)
  li $t1, 4
  mul $t2, $t0, $t1
  sw $t2, 216($fp)
  lw $t0, 208($fp)
  lw $t1, 216($fp)
  add $t2, $t0, $t1
  sw $t2, 220($fp)
  lw $t0, 220($fp)
  lw $t1, 0($t0)
  sw $t1, 224($fp)
  lw $t0, 204($fp)
  lw $t1, 224($fp)
  bgt $t0, $t1, label16
  j label17
label16:
  lw $t0, 96($fp)
  sw $t0, 228($fp)
  lw $t0, 72($fp)
  sw $t0, 232($fp)
  lw $t0, 232($fp)
  li $t1, 4
  mul $t2, $t0, $t1
  sw $t2, 236($fp)
  lw $t0, 228($fp)
  lw $t1, 236($fp)
  add $t2, $t0, $t1
  sw $t2, 240($fp)
  lw $t0, 240($fp)
  lw $t1, 0($t0)
  sw $t1, 244($fp)
  lw $t0, 244($fp)
  sw $t0, 248($fp)
  lw $t0, 96($fp)
  sw $t0, 252($fp)
  lw $t0, 72($fp)
  sw $t0, 256($fp)
  lw $t0, 256($fp)
  li $t1, 4
  mul $t2, $t0, $t1
  sw $t2, 260($fp)
  lw $t0, 252($fp)
  lw $t1, 260($fp)
  add $t2, $t0, $t1
  sw $t2, 264($fp)
  lw $t0, 96($fp)
  sw $t0, 268($fp)
  lw $t0, 172($fp)
  sw $t0, 272($fp)
  lw $t0, 272($fp)
  li $t1, 4
  mul $t2, $t0, $t1
  sw $t2, 276($fp)
  lw $t0, 268($fp)
  lw $t1, 276($fp)
  add $t2, $t0, $t1
  sw $t2, 280($fp)
  lw $t0, 280($fp)
  lw $t1, 0($t0)
  sw $t1, 284($fp)
  lw $t0, 264($fp)
  lw $t1, 284($fp)
  sw $t1, 0($t0)
  lw $t0, 96($fp)
  sw $t0, 288($fp)
  lw $t0, 172($fp)
  sw $t0, 292($fp)
  lw $t0, 292($fp)
  li $t1, 4
  mul $t2, $t0, $t1
  sw $t2, 296($fp)
  lw $t0, 288($fp)
  lw $t1, 296($fp)
  add $t2, $t0, $t1
  sw $t2, 300($fp)
  lw $t0, 248($fp)
  sw $t0, 304($fp)
  lw $t0, 300($fp)
  lw $t1, 304($fp)
  sw $t1, 0($t0)
label17:
  lw $t0, 172($fp)
  sw $t0, 308($fp)
  li $t0, 1
  sw $t0, 312($fp)
  lw $t0, 308($fp)
  lw $t1, 312($fp)
  add $t2, $t0, $t1
  sw $t2, 316($fp)
  lw $t0, 316($fp)
  sw $t0, 172($fp)
  j label11
label13:
  lw $t0, 72($fp)
  sw $t0, 320($fp)
  li $t0, 1
  sw $t0, 324($fp)
  lw $t0, 320($fp)
  lw $t1, 324($fp)
  add $t2, $t0, $t1
  sw $t2, 328($fp)
  lw $t0, 328($fp)
  sw $t0, 72($fp)
  j label6
label8:
  li $t0, 0
  sw $t0, 332($fp)
  lw $t0, 332($fp)
  sw $t0, 72($fp)
label18:
  li $t0, 0
  sw $t0, 336($fp)
  lw $t0, 72($fp)
  sw $t0, 340($fp)
  li $t0, 5
  sw $t0, 344($fp)
  lw $t0, 340($fp)
  lw $t1, 344($fp)
  blt $t0, $t1, label21
  j label22
label21:
  li $t0, 1
  sw $t0, 336($fp)
label22:
  lw $t0, 336($fp)
  li $t1, 0
  bne $t0, $t1, label19
  j label20
label19:
  lw $t0, 96($fp)
  sw $t0, 348($fp)
  lw $t0, 72($fp)
  sw $t0, 352($fp)
  lw $t0, 352($fp)
  li $t1, 4
  mul $t2, $t0, $t1
  sw $t2, 356($fp)
  lw $t0, 348($fp)
  lw $t1, 356($fp)
  add $t2, $t0, $t1
  sw $t2, 360($fp)
  lw $t0, 360($fp)
  lw $t1, 0($t0)
  sw $t1, 364($fp)
  lw $a0, 364($fp)
  jal write
  lw $t0, 72($fp)
  sw $t0, 368($fp)
  li $t0, 1
  sw $t0, 372($fp)
  lw $t0, 368($fp)
  lw $t1, 372($fp)
  add $t2, $t0, $t1
  sw $t2, 376($fp)
  lw $t0, 376($fp)
  sw $t0, 72($fp)
  j label18
label20:
  lw $t0, 96($fp)
  sw $t0, 380($fp)
  lw $t0, 380($fp)
  sw $t0, 20($fp)
  lw $t0, 96($fp)
  sw $t0, 384($fp)
  lw $t0, 384($fp)
  sw $t0, 64($fp)
  li $t0, 0
  sw $t0, 388($fp)
  lw $t0, 388($fp)
  sw $t0, 72($fp)
label23:
  li $t0, 0
  sw $t0, 392($fp)
  lw $t0, 72($fp)
  sw $t0, 396($fp)
  li $t0, 5
  sw $t0, 400($fp)
  lw $t0, 396($fp)
  lw $t1, 400($fp)
  blt $t0, $t1, label26
  j label27
label26:
  li $t0, 1
  sw $t0, 392($fp)
label27:
  lw $t0, 392($fp)
  li $t1, 0
  bne $t0, $t1, label24
  j label25
label24:
  lw $t0, 72($fp)
  sw $t0, 404($fp)
  li $t0, 3
  sw $t0, 408($fp)
  lw $t0, 404($fp)
  lw $t1, 408($fp)
  blt $t0, $t1, label28
  j label29
label28:
  lw $t0, 20($fp)
  sw $t0, 412($fp)
  lw $t0, 72($fp)
  sw $t0, 416($fp)
  lw $t0, 416($fp)
  li $t1, 4
  mul $t2, $t0, $t1
  sw $t2, 420($fp)
  lw $t0, 412($fp)
  lw $t1, 420($fp)
  add $t2, $t0, $t1
  sw $t2, 424($fp)
  lw $t0, 424($fp)
  lw $t1, 0($t0)
  sw $t1, 428($fp)
  lw $a0, 428($fp)
  jal write
  lw $t0, 64($fp)
  sw $t0, 432($fp)
  lw $t0, 72($fp)
  sw $t0, 436($fp)
  lw $t0, 436($fp)
  li $t1, 4
  mul $t2, $t0, $t1
  sw $t2, 440($fp)
  lw $t0, 432($fp)
  lw $t1, 440($fp)
  add $t2, $t0, $t1
  sw $t2, 444($fp)
  lw $t0, 444($fp)
  lw $t1, 0($t0)
  sw $t1, 448($fp)
  lw $a0, 448($fp)
  jal write
  j label30
label29:
  lw $t0, 64($fp)
  sw $t0, 452($fp)
  lw $t0, 72($fp)
  sw $t0, 456($fp)
  lw $t0, 456($fp)
  li $t1, 4
  mul $t2, $t0, $t1
  sw $t2, 460($fp)
  lw $t0, 452($fp)
  lw $t1, 460($fp)
  add $t2, $t0, $t1
  sw $t2, 464($fp)
  lw $t0, 464($fp)
  lw $t1, 0($t0)
  sw $t1, 468($fp)
  lw $a0, 468($fp)
  jal write
label30:
  lw $t0, 72($fp)
  sw $t0, 472($fp)
  li $t0, 1
  sw $t0, 476($fp)
  lw $t0, 472($fp)
  lw $t1, 476($fp)
  add $t2, $t0, $t1
  sw $t2, 480($fp)
  lw $t0, 480($fp)
  sw $t0, 72($fp)
  j label23
label25:
  li $t0, 0
  sw $t0, 484($fp)
  lw $t0, 484($fp)
  move $v0, $t0
  move $sp, $fp
  lw $fp, 0($sp)
  lw $ra, 4($sp)
  addi $sp, $sp, 488
  jr $ra
