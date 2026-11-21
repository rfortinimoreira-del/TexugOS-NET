#ifndef TEXUGOS_DESKTOP_H
#define TEXUGOS_DESKTOP_H

void desktop_run(void);

/* 1 enquanto desktop_run() esta em execucao. */
int desktop_is_running(void);

/* Redesenha o desktop agora (usado pelo Terminal durante comandos). */
void desktop_refresh(void);

#endif
