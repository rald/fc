; ============================================================================
; ARKANOID FOR 128x128 4BPP SWEETIE-16 VM
; ============================================================================
; Memory Map:
;   0x0000 .. 0x1FFF : 128x128 4bpp Framebuffer (8192 bytes, 64 bytes/row)
;   0x2000           : Input register (32-bit bitmask)
;   0x2004           : System register (write 1 to yield frame to SDL2)
;   0x2010           : Game State Base (r13 points here)
;     [r13 + 0]  (0) : pad_x      (paddle left X, 4..100)
;     [r13 + 4]  (1) : ball_x     (ball X, 0..124)
;     [r13 + 8]  (2) : ball_y     (ball Y, 0..124)
;     [r13 + 12] (3) : ball_vx    (ball X velocity: -2, -1, 1, 2)
;     [r13 + 16] (4) : ball_vy    (ball Y velocity: -1, 1)
;     [r13 + 20] (5) : ball_stuck (1 = resting on paddle, 0 = active)
;     [r13 + 24] (6) : lives      (remaining lives, starts at 3)
;     [r13 + 28] (7) : bricks_left(active bricks count, starts at 32)
;     [r13 + 32] (8) : game_over  (0 = playing, 1 = lost, 2 = won)
;   0x2040           : Brick Array Base (32 bricks: 4 rows x 8 cols, 4 bytes each)
;                      Value = Sweetie-16 color index (0 = destroyed)
; ============================================================================

init:
    ; Set r13 = 0x2010 (Game State Base)
    movi r13, 0x20
    shl  r13, 8
    movi r0, 0x10
    add  r13, r13, r0

reset_game:
    ; Initialize lives = 3, game_over = 0
    movi r0, 3
    st32 r0, r13, 6         ; lives = 3
    movi r0, 0
    st32 r0, r13, 8         ; game_over = 0

    ; Initialize 32 bricks (4 rows x 8 cols) at 0x2040
    ; Brick base = r13 + 0x30 (0x2040)
    movi r0, 0x30
    add  r8, r13, r0        ; r8 = brick pointer
    movi r9, 0              ; r9 = row (0..3)
    movi r10, 4             ; max rows
    movi r11, 8             ; cols per row
    movi r12, 1             ; constant 1

init_brick_row:
    cmplt r0, r9, r10
    jz    r0, init_bricks_done

    ; Row color: row 0 -> 2 (Red), row 1 -> 3 (Orange), row 2 -> 4 (Yellow), row 3 -> 5 (Lime)
    movi r1, 2
    add  r1, r1, r9         ; r1 = color for this row

    movi r2, 0              ; col = 0
init_brick_col:
    cmplt r0, r2, r11
    jz    r0, next_brick_row
    st32  r1, r8, 0         ; store brick color
    movi  r0, 4
    add   r8, r8, r0        ; brick_ptr += 4
    add   r2, r2, r12       ; col++
    jmp   init_brick_col

next_brick_row:
    add  r9, r9, r12        ; row++
    jmp  init_brick_row

init_bricks_done:
    movi r0, 32
    st32 r0, r13, 7         ; bricks_left = 32

reset_ball:
    movi r0, 52             ; center paddle X (128/2 - 24/2 = 52)
    st32 r0, r13, 0         ; pad_x = 52
    movi r0, 62
    st32 r0, r13, 1         ; ball_x = 62
    movi r0, 112
    st32 r0, r13, 2         ; ball_y = 112
    movi r0, 1
    st32 r0, r13, 3         ; ball_vx = 1
    movi r0, -1
    st32 r0, r13, 4         ; ball_vy = -1
    movi r0, 1
    st32 r0, r13, 5         ; ball_stuck = 1

; ============================================================================
; MAIN GAME LOOP (Runs once per video frame)
; ============================================================================
main_loop:
    ; Read input register at 0x2000
    movi r8, 0x20
    shl  r8, 8              ; r8 = 0x2000
    ld32 r9, r8, 0          ; r9 = input bitmask

    ; Check if game_over != 0
    ld32 r0, r13, 8
    jz   r0, handle_playing

    ; Game Over or Win state: check START (bit 9 = 512) to restart
    movi r1, 1
    shl  r1, 9              ; r1 = 512 (BTN_START)
    and  r0, r9, r1
    jz   r0, skip_restart
    jmp  reset_game
skip_restart:

handle_playing:
    ; --- Paddle Movement ---
    ld32 r1, r13, 0         ; r1 = pad_x
    movi r2, 3              ; paddle speed = 3 px/frame

    ; Check LEFT (bit 2 = 4)
    movi r3, 4
    and  r0, r9, r3
    jz   r0, check_right
    sub  r1, r1, r2
    movi r4, 4              ; min X = 4
    cmplt r0, r1, r4
    jz   r0, check_right
    mov  r1, r4

check_right:
    ; Check RIGHT (bit 3 = 8)
    movi r3, 8
    and  r0, r9, r3
    jz   r0, store_paddle
    add  r1, r1, r2
    movi r4, 100            ; max X = 128 - 4 - 24 = 100
    cmplt r0, r4, r1
    jz   r0, store_paddle
    mov  r1, r4

store_paddle:
    st32 r1, r13, 0         ; save updated pad_x

    ; --- Ball State ---
    ld32 r0, r13, 5         ; r0 = ball_stuck
    jz   r0, move_ball

    ; Ball is stuck on paddle: follow pad_x + 10
    movi r2, 10
    add  r2, r1, r2
    st32 r2, r13, 1         ; ball_x = pad_x + 10
    movi r2, 112
    st32 r2, r13, 2         ; ball_y = 112

    ; Launch ball on X (16), O (32), UP (1), or START (512)
    movi r3, 49             ; 16 | 32 | 1 = 49
    movi r4, 1
    shl  r4, 9              ; 512
    or   r3, r3, r4
    and  r0, r9, r3
    jz   r0, skip_launch

    movi r0, 0
    st32 r0, r13, 5         ; ball_stuck = 0
skip_launch:
    jmp  render_frame

move_ball:
    ld32 r1, r13, 1         ; r1 = ball_x
    ld32 r2, r13, 2         ; r2 = ball_y
    ld32 r3, r13, 3         ; r3 = ball_vx
    ld32 r4, r13, 4         ; r4 = ball_vy

    add  r1, r1, r3         ; ball_x += ball_vx
    add  r2, r2, r4         ; ball_y += ball_vy

    ; Left wall bounce (x < 4)
    movi r5, 4
    cmplt r0, r1, r5
    jz   r0, check_wall_right
    mov  r1, r5
    movi r5, 0
    sub  r3, r5, r3         ; ball_vx = -ball_vx

check_wall_right:
    ; Right wall bounce (x > 120, ball width = 4 -> right edge 124)
    movi r5, 120
    cmplt r0, r5, r1
    jz   r0, check_wall_top
    mov  r1, r5
    movi r5, 0
    sub  r3, r5, r3         ; ball_vx = -ball_vx

check_wall_top:
    ; Top wall bounce (y < 4)
    movi r5, 4
    cmplt r0, r2, r5
    jz   r0, check_bottom
    mov  r2, r5
    movi r5, 0
    sub  r4, r5, r4         ; ball_vy = -ball_vy

check_bottom:
    ; Bottom out-of-bounds (y > 124)
    movi r5, 124
    cmplt r0, r5, r2
    jz   r0, check_paddle_hit

    ; Lost a life
    ld32 r6, r13, 6         ; lives
    movi r7, 1
    sub  r6, r6, r7
    st32 r6, r13, 6
    jz   r6, set_game_lost
    jmp  reset_ball

set_game_lost:
    movi r0, 1
    st32 r0, r13, 8         ; game_over = 1 (lost)
    jmp  render_frame

check_paddle_hit:
    ; Only collide if ball moving down (ball_vy > 0 -> 0 < ball_vy)
    movi r5, 0
    cmplt r0, r5, r4
    jz   r0, check_bricks

    ; Paddle Y range: 116..119 (ball bottom is r2 + 4)
    movi r5, 112            ; ball_y >= 112 (so bottom >= 116)
    cmplt r0, r2, r5
    jz   r0, check_pad_y2
    jmp  check_bricks
check_pad_y2:
    movi r5, 119
    cmplt r0, r5, r2        ; ball_y > 119 -> missed paddle
    jz   r0, check_pad_x
    jmp  check_bricks

check_pad_x:
    ld32 r6, r13, 0         ; r6 = pad_x (width = 24)
    ; Ball right edge (r1 + 4) > pad_x -> pad_x < r1 + 4
    movi r7, 4
    add  r7, r1, r7
    cmplt r0, r6, r7
    jz   r0, check_bricks

    ; Ball left edge (r1) < pad_x + 24
    movi r7, 24
    add  r7, r6, r7
    cmplt r0, r1, r7
    jz   r0, check_bricks

    ; Paddle hit! Bounce up and angle based on hit offset
    movi r2, 112
    movi r4, -1

    ; Offset from pad_x: diff = (r1 + 2) - (r6 + 12) = r1 - r6 - 10
    sub  r7, r1, r6
    movi r5, 10
    sub  r7, r7, r5         ; negative = left side, positive = right side

    ; If diff < -4 -> vx = -2; else if diff < 0 -> vx = -1; else if diff < 5 -> vx = 1; else vx = 2
    movi r5, -4
    cmplt r0, r7, r5
    jz   r0, pad_ang_1
    movi r3, -2
    jmp  check_bricks
pad_ang_1:
    movi r5, 0
    cmplt r0, r7, r5
    jz   r0, pad_ang_2
    movi r3, -1
    jmp  check_bricks
pad_ang_2:
    movi r5, 5
    cmplt r0, r7, r5
    jz   r0, pad_ang_3
    movi r3, 1
    jmp  check_bricks
pad_ang_3:
    movi r3, 2

check_bricks:
    ; Save ball_x, ball_y, ball_vx, ball_vy temporarily
    st32 r1, r13, 1
    st32 r2, r13, 2
    st32 r3, r13, 3
    st32 r4, r13, 4

    ; Check brick grid: 4 rows (y = 14, 21, 28, 35; height = 5)
    ;                   8 cols (x = 8 + col*14; width = 12)
    movi r0, 0x30
    add  r8, r13, r0        ; r8 = brick pointer
    movi r9, 0              ; r9 = row (0..3)

brick_row_loop:
    movi r5, 4
    cmplt r0, r9, r5
    jz   r0, bricks_done
    jmp  brick_row_body
bricks_done:
    jmp  render_frame
brick_row_body:

    ; brick_y = 14 + row * 7
    mov  r6, r9
    shl  r6, 3              ; row * 8
    sub  r6, r6, r9         ; row * 7
    movi r5, 14
    add  r6, r6, r5         ; r6 = brick_y

    movi r10, 0             ; r10 = col (0..7)
brick_col_loop:
    movi r5, 8
    cmplt r0, r10, r5
    jz   r0, next_b_row

    ; Check if brick is active
    ld32 r7, r8, 0
    jz   r7, advance_brick

    ; brick_x = 8 + col * 14
    mov  r7, r10
    shl  r7, 4              ; col * 16
    sub  r7, r7, r10        ; col * 15
    sub  r7, r7, r10        ; col * 14
    movi r5, 8
    add  r7, r7, r5         ; r7 = brick_x

    ; AABB check between ball (r1, r2, 4x4) and brick (r7, r6, 12x5)
    ; Check brick_x < ball_x + 4
    movi r5, 4
    add  r5, r1, r5
    cmplt r0, r7, r5
    jz   r0, advance_brick

    ; Check ball_x < brick_x + 12
    movi r5, 12
    add  r5, r7, r5
    cmplt r0, r1, r5
    jz   r0, advance_brick

    ; Check brick_y < ball_y + 4
    movi r5, 4
    add  r5, r2, r5
    cmplt r0, r6, r5
    jz   r0, advance_brick

    ; Check ball_y < brick_y + 5
    movi r5, 5
    add  r5, r6, r5
    cmplt r0, r2, r5
    jz   r0, advance_brick

    ; Brick Hit! Destroy brick
    movi r0, 0
    st32 r0, r8, 0

    ; Invert ball_vy
    ld32 r4, r13, 4
    movi r5, 0
    sub  r4, r5, r4
    st32 r4, r13, 4

    ; Decrement bricks_left
    ld32 r5, r13, 7
    movi r0, 1
    sub  r5, r5, r0
    st32 r5, r13, 7
    jz   r5, do_game_won
    jmp  render_frame
do_game_won:
    jmp  set_game_won

set_game_won:
    movi r0, 2
    st32 r0, r13, 8         ; game_over = 2 (won)
    jmp  render_frame

advance_brick:
    movi r5, 4
    add  r8, r8, r5
    movi r5, 1
    add  r10, r10, r5
    jmp  brick_col_loop

next_b_row:
    movi r5, 1
    add  r9, r9, r5
    jmp  brick_row_loop

; ============================================================================
; RENDER FRAME TO 128x128 4BPP FRAMEBUFFER (0x0000 .. 0x1FFF)
; ============================================================================
render_frame:
    ; 1. Clear framebuffer to color 0 (Dark Void) - 2048 32-bit words
    movi r8, 0              ; r8 = fb ptr
    movi r9, 0x20
    shl  r9, 8              ; r9 = 0x2000 (end of fb)
    movi r0, 0
    movi r1, 4
clear_loop:
    st32  r0, r8, 0
    add   r8, r8, r1
    cmplt r2, r8, r9
    jz    r2, draw_borders
    jmp   clear_loop

draw_borders:
    ; Top border (x=0..127, y=0..2, color=14 Light/Med Slate)
    movi r1, 0
    movi r2, 0
    movi r3, 128
    movi r4, 3
    movi r5, 14
    call fill_rect

    ; Left border (x=0..2, y=0..127, color=14)
    movi r1, 0
    movi r2, 0
    movi r3, 3
    movi r4, 128
    movi r5, 14
    call fill_rect

    ; Right border (x=125..127, y=0..127, color=14)
    movi r1, 125
    movi r2, 0
    movi r3, 3
    movi r4, 128
    movi r5, 14
    call fill_rect

    ; Draw lives indicator in top-left border (y=0..1, 4px each, color=11 Cyan Ice)
    ld32 r6, r13, 6         ; lives
    movi r7, 6              ; start x = 6
draw_lives_loop:
    jz   r6, draw_bricks
    mov  r1, r7
    movi r2, 0
    movi r3, 4
    movi r4, 2
    movi r5, 11
    call fill_rect
    movi r0, 6
    add  r7, r7, r0
    movi r0, 1
    sub  r6, r6, r0
    jmp  draw_lives_loop

draw_bricks:
    movi r0, 0x30
    add  r8, r13, r0        ; r8 = brick pointer
    movi r9, 0              ; row = 0

render_b_row:
    movi r0, 4
    cmplt r1, r9, r0
    jz   r1, render_b_done
    jmp  render_b_body
render_b_done:
    jmp  draw_entities
render_b_body:

    movi r10, 0             ; col = 0
render_b_col:
    movi r0, 8
    cmplt r1, r10, r0
    jz   r1, render_next_row

    ld32 r5, r8, 0          ; brick color
    jz   r5, skip_render_brick

    ; r1 = x = 8 + col * 14
    mov  r1, r10
    shl  r1, 4
    sub  r1, r1, r10
    sub  r1, r1, r10
    movi r0, 8
    add  r1, r1, r0

    ; r2 = y = 14 + row * 7
    mov  r2, r9
    shl  r2, 3
    sub  r2, r2, r9
    movi r0, 14
    add  r2, r2, r0

    movi r3, 12             ; width = 12
    movi r4, 5              ; height = 5
    call fill_rect

skip_render_brick:
    movi r0, 4
    add  r8, r8, r0
    movi r0, 1
    add  r10, r10, r0
    jmp  render_b_col

render_next_row:
    movi r0, 1
    add  r9, r9, r0
    jmp  render_b_row

draw_entities:
    ; Draw Paddle (pad_x, 116, 24x4, color=10 Sky Blue)
    ld32 r1, r13, 0
    movi r2, 116
    movi r3, 24
    movi r4, 4
    movi r5, 10
    call fill_rect

    ; Paddle highlight (pad_x+2, 116, 20x1, color=12 Pure White)
    ld32 r1, r13, 0
    movi r0, 2
    add  r1, r1, r0
    movi r2, 116
    movi r3, 20
    movi r4, 1
    movi r5, 12
    call fill_rect

    ; Draw Ball (ball_x, ball_y, 4x4, color=12 Pure White)
    ld32 r1, r13, 1
    ld32 r2, r13, 2
    movi r3, 4
    movi r4, 4
    movi r5, 12
    call fill_rect

    ; If game_over != 0, draw status banner in center
    ld32 r0, r13, 8
    jz   r0, do_sync_frame
    jmp  prep_banner
do_sync_frame:
    jmp  sync_frame
prep_banner:

    ; Banner color: 2 (Crimson Red) if lost (1), 6 (Emerald Green) if won (2)
    movi r5, 2
    movi r1, 1
    cmplt r2, r1, r0        ; if 1 < game_over (i.e. game_over == 2)
    jz   r2, draw_banner
    movi r5, 6

draw_banner:
    movi r1, 32
    movi r2, 56
    movi r3, 64
    movi r4, 16
    call fill_rect

sync_frame:
    ; Write 1 to VM_SYS_ADDR (0x2004) to signal SDL2 host to present frame
    movi r8, 0x20
    shl  r8, 8              ; 0x2000
    movi r0, 1
    st32 r0, r8, 1          ; mem32[0x2004] = 1
    jmp  main_loop

; ============================================================================
; SUBROUTINE: fill_rect
; Inputs: r1 = x, r2 = y, r3 = w, r4 = h, r5 = color (0..15)
; Clobbers: r0, r1, r2, r3, r4, r6, r7
; Preserves: r8, r9, r10, r11, r12, r13, r14, r15
; ============================================================================
fill_rect:
    ; Save registers on stack (sp = r15)
    movi r0, 20
    sub  r15, r15, r0
    st32 r8,  r15, 0
    st32 r9,  r15, 1
    st32 r10, r15, 2
    st32 r11, r15, 3
    st32 r14, r15, 4

    mov  r8, r1             ; start_x
    add  r9, r1, r3         ; end_x
    mov  r10, r2            ; cur_y
    add  r11, r2, r4        ; end_y

fr_y_loop:
    cmplt r0, r10, r11
    jz    r0, fr_done

    ; Bounds check y (0 <= cur_y < 128)
    movi  r0, 0
    cmplt r0, r10, r0
    jz    r0, fr_y_ok1
    jmp   fr_next_y
fr_y_ok1:
    movi  r0, 127
    cmplt r0, r0, r10
    jz    r0, fr_y_ok2
    jmp   fr_next_y

fr_y_ok2:
    mov  r1, r8             ; cur_x = start_x
fr_x_loop:
    cmplt r0, r1, r9
    jz    r0, fr_next_y

    ; Bounds check x (0 <= cur_x < 128)
    movi  r0, 0
    cmplt r0, r1, r0
    jz    r0, fr_x_ok1
    jmp   fr_next_x
fr_x_ok1:
    movi  r0, 127
    cmplt r0, r0, r1
    jz    r0, fr_x_ok2
    jmp   fr_next_x

fr_x_ok2:
    ; Compute byte address = (cur_y * 64) + (cur_x >> 1)
    mov  r6, r10
    shl  r6, 6              ; cur_y * 64
    mov  r7, r1
    shr  r7, 1              ; cur_x >> 1
    add  r6, r6, r7         ; byte_addr

    ; Aligned 32-bit word address = byte_addr & ~3
    movi r0, -4
    and  r7, r6, r0         ; word_addr

    ; Bit shift within 32-bit word = (byte_addr & 3) * 8 + (cur_x & 1) * 4
    movi r0, 3
    and  r6, r6, r0         ; byte_offset (0..3)
    shl  r6, 3              ; byte_offset * 8
    movi r0, 1
    and  r0, r1, r0         ; cur_x & 1
    shl  r0, 2              ; * 4
    add  r6, r6, r0         ; r6 = total bit shift (0, 4, 8, ..., 28)

    ; Build nibble mask = 0xF << r6
    movi r0, 0x0F
    mov  r2, r6
fr_shift_mask:
    jz   r2, fr_mask_ready
    shl  r0, 1
    movi r3, 1
    sub  r2, r2, r3
    jmp  fr_shift_mask

fr_mask_ready:
    ; Invert mask: ~mask = mask ^ -1
    movi r3, -1
    xor  r0, r0, r3         ; r0 = ~(0xF << shift)

    ; Shift color into position: (color & 0xF) << r6
    movi r3, 0x0F
    and  r3, r5, r3
    mov  r2, r6
fr_shift_col:
    jz   r2, fr_col_ready
    shl  r3, 1
    movi r4, 1
    sub  r2, r2, r4
    jmp  fr_shift_col

fr_col_ready:
    ; Read-Modify-Write 32-bit word at r7
    ld32 r2, r7, 0
    and  r2, r2, r0         ; clear target nibble
    or   r2, r2, r3         ; set new color nibble
    st32 r2, r7, 0

fr_next_x:
    movi r0, 1
    add  r1, r1, r0
    jmp  fr_x_loop

fr_next_y:
    movi r0, 1
    add  r10, r10, r0
    jmp  fr_y_loop

fr_done:
    ld32 r8,  r15, 0
    ld32 r9,  r15, 1
    ld32 r10, r15, 2
    ld32 r11, r15, 3
    ld32 r14, r15, 4
    movi r0, 20
    add  r15, r15, r0
    jr   r14
