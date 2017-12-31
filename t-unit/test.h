#ifndef PACHI_T_UNIT_TEST_H
#define PACHI_T_UNIT_TEST_H

/* run single unit test */
int unit_test_cmd(struct board *b, char *line);

/* run all unit tests in file */
void unit_test(char *filename);

#endif
