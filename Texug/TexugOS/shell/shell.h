#ifndef TEXUGOS_SHELL_H
#define TEXUGOS_SHELL_H

void shell_run(void) __attribute__((noreturn));

/* Usados pelo app Terminal do desktop: executa uma linha de comando com a
 * mesma logica do shell de boot (a saida segue o console_redirect). */
void shell_execute_line(char *line);
void shell_print_prompt(void);

/* Funcao chamada a cada espera do shell (sti; hlt). O desktop a usa para
 * continuar redesenhando a tela durante comandos demorados. */
void shell_set_idle_hook(void (*hook)(void));

#endif
