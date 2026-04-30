#ifndef _LIBC_CTYPE_H
#define _LIBC_CTYPE_H

int isdigit(int c);
int isalpha(int c);
int isalnum(int c);
int isspace(int c);
int isupper(int c);
int islower(int c);
int isxdigit(int c);
int ispunct(int c);
int isgraph(int c);
int isprint(int c);
int iscntrl(int c);

int toupper(int c);
int tolower(int c);

#endif
