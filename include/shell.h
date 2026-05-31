#ifndef SHELL_H
#define SHELL_H

void shell_init(void);         
void shell_secondary_init(void); 
void shell_run(void);  

/* Command-history access (used by the `history` command). */
void shell_history_dump(void);
void shell_history_clear(void);


#endif