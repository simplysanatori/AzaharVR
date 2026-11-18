@ EUR original code (code.bin md5 b107c9b1): what the literals of stubs.s name.
        .equ    IMAGE_VERSION,  5
        .equ    MAILBOX,        0x00C95F00  @ the cave's last 0x100 bytes
        .equ    ACTOR_VTABLE,   0x00CA046C  @ the game's generic actor
        .equ    SINK_CONTINUE,  0x006340A0  @ the camera hand-over's second instruction
        .equ    SPAWN,          0x00485638  @ the bullet spawn
        .equ    PATH_RESTART,   0x003E8CDC  @ the path end's "fly it again" code
        .equ    PATH_VANISH,    0x003E8D00  @ the path end's vanish code
        .equ    SHADOW_CONTINUE, 0x0057B058 @ the round shadow's second instruction
        .equ    FOV_CONTINUE,   0x0045E050  @ the camera node angle setter's second instruction
