/***********************************************************************
*                                                                      *
*               This software is part of the ast package               *
*          Copyright (c) 1982-2007 AT&T Intellectual Property          *
*                      and is licensed under the                       *
*                  Common Public License, Version 1.0                  *
*                    by AT&T Intellectual Property                     *
*                                                                      *
*                A copy of the License is available at                 *
*            http://www.opensource.org/licenses/cpl1.0.txt             *
*         (with md5 checksum 059e8cd6165cb4c31e351f2b69388fd9)         *
*                                                                      *
*              Information and Software Systems Research               *
*                            AT&T Research                             *
*                           Florham Park NJ                            *
*                                                                      *
*                  David Korn <dgk@research.att.com>                   *
*                                                                      *
***********************************************************************/
#pragma prototyped
#ifndef TEST_ARITH
/*
 *	UNIX shell
 *	David Korn
 *	AT&T Labs
 *
 */

#include	"FEATURE/options"
#include	"shtable.h"
/*
 *  These are the valid test operators
 */

#define TEST_ARITH	040	/* arithmetic operators */
#define TEST_BINOP	0200	/* binary operator */
#define TEST_PATTERN	0100	/* turn off bit for pattern compares */

#define TEST_NE		(TEST_ARITH|9)
#define TEST_EQ		(TEST_ARITH|4)
#define TEST_GE		(TEST_ARITH|5)
#define TEST_GT		(TEST_ARITH|6)
#define TEST_LE		(TEST_ARITH|7)
#define TEST_LT		(TEST_ARITH|8)
#define TEST_OR		(TEST_BINOP|1)
#define TEST_AND	(TEST_BINOP|2)
#define TEST_SNE	(TEST_PATTERN|1)
#define TEST_SEQ	(TEST_PATTERN|14)
#define TEST_PNE	1
#define TEST_PEQ	14
#define TEST_EF		3
#define TEST_NT		10
#define TEST_OT		12
#define TEST_SLT	15
#define TEST_SGT	16
#define TEST_END	8
#define TEST_REP	20

extern int test_unop(int, const char*);
extern int test_inode(const char*, const char*);
extern int test_binop(int, const char*, const char*);

extern const char	sh_opttest[];
extern const char	test_opchars[];
extern const char	e_argument[];
extern const char	e_missing[];
extern const char	e_badop[];
extern const char	e_tstbegin[];
extern const char	e_tstend[];

#endif /* TEST_ARITH */
