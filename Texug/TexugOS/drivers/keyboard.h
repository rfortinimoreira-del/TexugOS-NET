#ifndef TEXUGOS_KEYBOARD_H
#define TEXUGOS_KEYBOARD_H

void keyboard_initialize(void);
void keyboard_handle_interrupt(void);
int keyboard_read_character(char *character);

#endif
